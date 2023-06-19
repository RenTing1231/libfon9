// \file fon9/rc/RcFuncConnClient.cpp
// \author fonwinz@gmail.com
#include "fon9/rc/RcFuncConn.hpp"
#include "fon9/rc/RcSession.hpp"
#include "fon9/auth/SaslClient.hpp"

namespace fon9 { namespace rc {

void RcFuncConnClient::SendConnectionMessage(RcSession& ses) {
   RevBufferList  rbuf{64 + static_cast<BufferNodeSize>(this->ApVersion_.size())};
   ToBitv(rbuf, this->Description_);
   ToBitv(rbuf, this->ApVersion_);
   ses.SendConnecting(std::move(rbuf));
}
void RcFuncConnClient::OnSessionLinkReady(RcSession& ses) {
   if (IsEnumContains(ses.Role_, RcSessionRole::ProtocolInitiator))
      this->SendConnectionMessage(ses);
}
void RcFuncConnClient::OnSessionProtocolReady(RcSession& ses) {
   if (IsEnumContains(ses.Role_, RcSessionRole::ProtocolAcceptor))
      this->SendConnectionMessage(ses);
}
void RcFuncConnClient::OnRecvFunctionCall(RcSession& ses, RcFunctionParam& param) {
   if (ses.GetSessionSt() > RcSessionSt::Connecting) {
      ses.ForceLogout("Dup FuncConnClient");
      return;
   }
   // 收到 Server 送來的 Connection 訊息.
   // 取出 Server 的 ApVersion, Description, SaslMechList.
   CharVector apVer, description, saslMechList;
   BitvTo(param.RecvBuffer_, apVer);
   BitvTo(param.RecvBuffer_, description);
   BitvTo(param.RecvBuffer_, saslMechList);
   ses.SetRemoteApVer(std::move(apVer));
   // 通知 Sasl Client 送出登入要求.
   if (RcFuncSaslClient* saslClient = dynamic_cast<RcFuncSaslClient*>(ses.FunctionMgr_->Get(f9rc_FunctionCode_SASL)))
      saslClient->OnSessionConnected(ses, ToStrView(saslMechList));
   else
      ses.ForceLogout("Internal err: RcFuncSaslClient not found.");
}
//--------------------------------------------------------------------------//
struct RcClientNote_Sasl : public RcFunctionNote {
   fon9_NON_COPY_NON_MOVE(RcClientNote_Sasl);
   auth::SaslClientSP   SaslClient_;
public:
   RcClientNote_Sasl(auth::SaslClientSP&& saslClient) : SaslClient_{std::move(saslClient)} {
   }
   void OnRecvFunctionCall(RcSession& ses, RcFunctionParam& param) {
      if (ses.GetSessionSt() > RcSessionSt::Logoning) {
         ses.ForceLogout("Dup FuncSaslClient");
         return;
      }
      auth::AuthR rcode;
      std::string strSuccess;
      BitvTo(param.RecvBuffer_, rcode.RCode_);
      BitvTo(param.RecvBuffer_, rcode.Info_);
      if (rcode.RCode_ == fon9_Auth_NeedsMore
          || rcode.RCode_ == fon9_Auth_Success
          || rcode.RCode_ == fon9_Auth_PassChanged) {
         if (rcode.RCode_ != fon9_Auth_NeedsMore) // Success or PassChanged: 載入 Server 回覆的成功訊息.
            BitvTo(param.RecvBuffer_, strSuccess);
         rcode = this->SaslClient_->OnChallenge(ToStrView(rcode.Info_));
         if (rcode.RCode_ == fon9_Auth_NeedsMore) {
            RevBufferList rbuf{64};
            ToBitv(rbuf, rcode.Info_);
            ses.SendSasl(std::move(rbuf));
            return;
         }
      }
      if (!strSuccess.empty() && (rcode.RCode_ == fon9_Auth_Success || rcode.RCode_ == fon9_Auth_PassChanged))
         rcode.Info_ = std::move(strSuccess);
      ses.OnSaslDone_AtClient(rcode);
   }
};
void RcFuncSaslClient::OnSessionConnected(RcSession& ses, StrView saslMechList) {
   auth::SaslClientR sasl{auth::CreateSaslClient(saslMechList, ' ', ToStrView(ses.GetAuthzId()), ToStrView(ses.GetUserId()), ses.GetAuthPassword())};
   if (!sasl.SaslClient_) {
      ses.ForceLogout("Could not find a suitable SASL mechanism.");
      return;
   }
   RevBufferList  rbuf{128};
   ToBitv(rbuf, sasl.SaslClient_->GetFirstMessage());
   ToBitv(rbuf, sasl.MechName_);
   ses.ResetNote(f9rc_FunctionCode_SASL,
                 RcFunctionNoteSP{new RcClientNote_Sasl{std::move(sasl.SaslClient_)}});
   ses.SendSasl(std::move(rbuf));
}
void RcFuncSaslClient::OnRecvFunctionCall(RcSession& ses, RcFunctionParam&) {
   // 不應該走到這兒, 因為已在 RcFuncSaslClient::OnSessionConnected 建立了 RcClientNote_Sasl
   // 應該只會呼叫 RcClientNote_Sasl::OnRecvFunctionCall();
   ses.ForceLogout("Must call the connection function first.");
}

} } // namespace
