// \file fon9/rc/RcFuncConnServer.cpp
//
// 提供 f9rc 的 Connection & SASL 認證服務.
//
// \author fonwinz@gmail.com
#include "fon9/rc/RcFuncConnServer.hpp"
#include "fon9/rc/RcSession.hpp"
#include "fon9/seed/Plugins.hpp"
#include "fon9/framework/SessionFactoryConfigWithAuthMgr.hpp"
#include "fon9/framework/IoManager.hpp"

namespace fon9 { namespace rc {

void RcFuncConnServer::OnRecvFunctionCall(RcSession& ses, RcFunctionParam& param) {
   if (ses.GetSessionSt() > RcSessionSt::Connecting) {
      ses.ForceLogout("Dup FuncConnServer");
      return;
   }
   // 收到 Client 的 ApVersion, Description.
   CharVector apVer, description;
   BitvTo(param.RecvBuffer_, apVer);
   BitvTo(param.RecvBuffer_, description);
   ses.SetRemoteApVer(std::move(apVer));
   // 回覆 Server 的 ApVersion, Description, SaslMechList.
   RevBufferList  rbuf{64 + static_cast<BufferNodeSize>(this->ApVersion_.size() + this->Description_.size())};
   ToBitv(rbuf, this->AuthMgr_->GetSaslMechList());
   ToBitv(rbuf, this->Description_);
   ToBitv(rbuf, this->ApVersion_);
   ses.SendConnecting(std::move(rbuf));
}
//--------------------------------------------------------------------------//
RcServerNote_SaslAuth::RcServerNote_SaslAuth(RcSession& ses, auth::AuthMgr& authMgr, StrView mechName, std::string&& firstMessage,
                                             auth::PoDupLogonAgentSP onlineMgr)
   : RcSession_(ses)
   , OnlineMgr_(std::move(onlineMgr)) {
   this->AuthRequest_.UserFrom_ = ses.GetRemoteIp().ToString();
   this->AuthRequest_.Response_ = std::move(firstMessage);
   using namespace std::placeholders;
   this->AuthSession_ = authMgr.CreateAuthSession(mechName, std::bind(&RcServerNote_SaslAuth::OnAuthVerifyCB, this, _1, _2));
}
RcServerNote_SaslAuth::~RcServerNote_SaslAuth() {
   if (this->AuthSession_)
      this->AuthSession_->Dispose();
}
unsigned RcServerNote_SaslAuth::PoDupLogonClient_AddRef() const {
   intrusive_ptr_add_ref(&this->RcSession_);
   return thread_safe_counter::increment(this->OnlineUsingCount_);
}
unsigned RcServerNote_SaslAuth::PoDupLogonClient_Release() const {
   intrusive_ptr_release(&this->RcSession_);
   if (unsigned int res = thread_safe_counter::decrement(this->OnlineUsingCount_))
      return res;
   delete this;
   return 0;
}
void RcServerNote_SaslAuth::PoDupLogonClient_ForceLogout(StrView reason) {
   this->RcSession_.ForceLogout(reason.ToString());
}
void RcServerNote_SaslAuth::OnAuthVerifyCB(auth::AuthR rcode, auth::AuthSessionSP authSession) {
   RevBufferList     rbuf{static_cast<BufferNodeSize>(64 + rcode.Info_.size())};
   auth::AuthResult& authr = authSession->GetAuthResult();
   if (rcode.RCode_ == fon9_Auth_Success || rcode.RCode_ == fon9_Auth_PassChanged) {
      if (this->OnlineMgr_) {
         this->OnlineMgr_->CheckLogon(authr, "Rc",
                                      ToStrView(this->RcSession_.GetDevice()->WaitGetDeviceId()),
                                      this, rcode);
      }
      ToBitv(rbuf, authr.ExtInfo_);
   }
   ToBitv(rbuf, rcode.Info_);
   ToBitv(rbuf, rcode.RCode_);
   this->RcSession_.SendSasl(std::move(rbuf));
   if (rcode.RCode_ == fon9_Auth_NeedsMore)
      return;
   if (rcode.RCode_ == fon9_Auth_Success || rcode.RCode_ == fon9_Auth_PassChanged) {
      rcode.Info_ = authr.ExtInfo_;
      authr.UpdateRoleConfig();
   }
   this->RcSession_.OnSaslDone_AtServer(rcode, ToStrView(authr.AuthcId_), ToStrView(authr.AuthzId_));
}
void RcServerNote_SaslAuth::OnRecvFunctionCall(RcSession& ses, RcFunctionParam& param) {
   if (ses.GetSessionSt() > RcSessionSt::Logoning)
      ses.ForceLogout("Dup FuncSaslServer.");
   else {
      BitvTo(param.RecvBuffer_, this->AuthRequest_.Response_);
      this->VerifyRequest();
   }
}
void RcServerNote_SaslAuth::RcFunctionNote_FreeThis() {
   this->PoDupLogonClient_Release();
}
//--------------------------------------------------------------------------//
void RcFuncSaslServer::OnRecvFunctionCall(RcSession& ses, RcFunctionParam& param) {
   CharVector  mechName;
   std::string firstMessage;
   BitvTo(param.RecvBuffer_, mechName);
   BitvTo(param.RecvBuffer_, firstMessage);
   RcServerNote_SaslAuth*  note;
   RcFunctionNoteSP noteSP{
      note = new RcServerNote_SaslAuth(ses, *this->AuthMgr_,
                                       ToStrView(mechName), std::move(firstMessage),
                                       this->OnlineMgr_)
   };
   if (!note->AuthSession_)
      ses.ForceLogout("SASL mech not found.");
   else {
      note->PoDupLogonClient_AddRef();
      ses.ResetNote(f9rc_FunctionCode_SASL, std::move(noteSP));
      note->VerifyRequest();
   }
}
void RcFuncSaslServer::OnSessionLinkBroken(RcSession& ses) {
   ses.ClearUserId();
   if (this->OnlineMgr_) {
      if (RcServerNote_SaslAuth* authNote = ses.GetNote<RcServerNote_SaslAuth>(f9rc_FunctionCode_SASL)) {
         this->OnlineMgr_->OnLogonClientClosed(*authNote);
      }
   }
   base::OnSessionLinkBroken(ses);
}
//--------------------------------------------------------------------------//
class RcSessionDeSb : public RcSession {
   fon9_NON_COPY_NON_MOVE(RcSessionDeSb);
   using base = RcSession;

   struct DeSbNode : public BufferNodeVirtual {
      fon9_NON_COPY_NON_MOVE(DeSbNode);
      using base = BufferNodeVirtual;
      friend class BufferNode;// for BufferNode::Alloc();
   protected:
      const intrusive_ptr<RcSessionDeSb> Session_;
      DeSbNode(BufferNodeSize blockSize, StyleFlag style, RcSessionDeSb& ses)
         : base(blockSize, style)
         , Session_{&ses} {
      }
      void OnBufferConsumed() override {
         this->Session_->IsDetecting_ = false;
      }
      void OnBufferConsumedErr(const ErrC&) override {
      }
   public:
      static DeSbNode* Alloc(RcSessionDeSb& ses) {
         return base::Alloc<DeSbNode>(0, StyleFlag::AllowCrossing, ses);
      }
   };
   static void EmitDeSbTimer(TimerEntry* timer, TimeStamp now) {
      (void)now;
      RcSessionDeSb& rthis = ContainerOf(*static_cast<decltype(RcSessionDeSb::DeSbTimer_)*>(timer), &RcSessionDeSb::DeSbTimer_);
      io::Device*    dev = rthis.GetDevice();
      // 在此先做個簡單的檢查, 可以排除大部分已斷線的情況.
      if (dev->OpImpl_GetState() != io::State::LinkReady)
         return;
      // 如果在 dev OpThread 要 this->DeSbTimer_.xxxAndWait();
      // 而此時要呼叫 if (dev->IsSendBufferEmpty())
      // 則會死結! 所以不判斷 dev->IsSendBufferEmpty(); 直接使用 DeSbNode 處理.
      if (!rthis.IsDetecting_) {
         rthis.IsDetecting_ = true;
         dev->SendASAP(BufferList{DeSbNode::Alloc(rthis)});
      }
      else { // 超過時間還沒有將 DeSbNode 消化掉 => 送出阻塞!
         dev->AsyncClose(RevPrintTo<std::string>(
            "Over DeSb interval:", rthis.DeSbInterval_,
            "|authcId=", rthis.GetUserId()));
         return;
      }
      timer->RunAfter(rthis.DeSbInterval_);
   }
   DataMemberEmitOnTimer<&RcSessionDeSb::EmitDeSbTimer> DeSbTimer_;
   bool IsDetecting_{};
   char Padding____[7];

   void SetApReady(StrView info) override {
      base::SetApReady(info);
      this->DeSbTimer_.RunAfter(this->DeSbInterval_);
   }
   void OnDevice_StateChanged(io::Device& dev, const io::StateChangedArgs& e) override {
      base::OnDevice_StateChanged(dev, e);
      if (e.After_.State_ >= io::State::Disposing)
         this->DeSbTimer_.DisposeAndWait();
   }

public:
   const TimeInterval   DeSbInterval_;

   RcSessionDeSb(RcFunctionMgrSP mgr, RcSessionRole role, TimeInterval deSbInterval)
      : base{std::move(mgr), role}
      , DeSbInterval_{deSbInterval} {
   }
   ~RcSessionDeSb() {
      // 如果在 EmitDeSbTimer() 正要執行 dev->SendBuffered(BufferList{DeSbNode::Alloc(rthis)});
      // 則在此 this->DeSbTimer_.DisposeAndWait(); 會造成 this 穢土重生!
      // 此時在 ~DeSbNode(); this 會再死一次, 造成 crash!
      // 所以 this->DeSbTimer_.DisposeAndWait(); 必須放在 OnDevice_StateChanged() 裡面.
   }
};

struct RcSessionServer_Factory : public SessionFactory, public RcFunctionMgr {
   fon9_NON_COPY_NON_MOVE(RcSessionServer_Factory);
   const CharVector        NameVer_;
   const CharVector        Description_;
   // 傳送阻塞偵測間隔(秒).
   const TimeInterval      DeSbInterval_;

   RcSessionServer_Factory(std::string name, auth::AuthMgrSP authMgr, const CharVector& nameVer, const CharVector& desp, TimeInterval deSbInterval,
                           auth::PoDupLogonAgentSP onlineMgr)
      : SessionFactory(std::move(name))
      , NameVer_{nameVer}
      , Description_{desp}
      , DeSbInterval_{deSbInterval} {
      this->Add(RcFunctionAgentSP{new RcFuncConnServer("f9rcServer.0", "fon9 RcServer", authMgr)});
      this->Add(RcFunctionAgentSP{new RcFuncSaslServer(std::move(authMgr), std::move(onlineMgr))});
   }
   unsigned RcFunctionMgrAddRef() override {
      return intrusive_ptr_add_ref(static_cast<SessionFactory*>(this));
   }
   unsigned RcFunctionMgrRelease() override {
      return intrusive_ptr_release(static_cast<SessionFactory*>(this));
   }

   io::SessionSP CreateRcSession() {
      if (this->DeSbInterval_.IsNullOrZero())
         return new RcSession(this, RcSessionRole::Host);
      return new RcSessionDeSb(this, RcSessionRole::Host, this->DeSbInterval_);
   }
   io::SessionSP CreateSession(IoManager& iomgr, const IoConfigItem& cfg, std::string& errReason) override {
      (void)iomgr; (void)cfg; (void)errReason;
      return this->CreateRcSession();
   }
   io::SessionServerSP CreateSessionServer(IoManager& iomgr, const IoConfigItem& cfg, std::string& errReason) override {
      (void)iomgr; (void)cfg; (void)errReason;
      return new RcSessionServer(this);
   }

   struct RcSessionServer : public io::SessionServer {
      fon9_NON_COPY_NON_MOVE(RcSessionServer);
      intrusive_ptr<RcSessionServer_Factory> Owner_;
      RcSessionServer(RcSessionServer_Factory* owner)
         : Owner_(owner) {
      }
      io::SessionSP OnDevice_Accepted(io::DeviceServer&) {
         return this->Owner_->CreateRcSession();
      }
   };
};
//--------------------------------------------------------------------------//
fon9_API RcFunctionMgr* FindRcFunctionMgr(seed::PluginsHolder& holder, const StrView cDestPath) {
   StrView  destPath = cDestPath;
   StrView  sesFacName = StrFetchTrim(destPath, '/');
   auto     sesFacPark = holder.Root_->Get<SessionFactoryPark>(sesFacName);
   if (!sesFacPark) {
      if (auto iomgr = holder.Root_->GetSapling<IoManager>(sesFacName))
         sesFacPark = iomgr->SessionFactoryPark_;
      if (!sesFacPark) {
         holder.SetPluginsSt(LogLevel::Error, "Not found SessionFactoryPark=", cDestPath);
         return nullptr;
      }
   }
   RcSessionServer_Factory* rcFuncMgr = dynamic_cast<RcSessionServer_Factory*>(sesFacPark->Get(destPath).get());
   if (!rcFuncMgr) {
      holder.SetPluginsSt(LogLevel::Error, "Not found RcFunctionMgr=", cDestPath);
      return nullptr;
   }
   return rcFuncMgr;
}

} } // namespace

static bool RcSessionServer_Start(fon9::seed::PluginsHolder& holder, fon9::StrView args) {
   class ArgsParser : public fon9::SessionFactoryConfigWithAuthMgr {
      fon9_NON_COPY_NON_MOVE(ArgsParser);
      using base = fon9::SessionFactoryConfigWithAuthMgr;
      fon9::CharVector     NameVer_{fon9::StrView{"f9rcServer.0"}};
      fon9::CharVector     Description_{fon9::StrView{"fon9 RcServer"}};
      fon9::TimeInterval   DeSbInterval_;
   public:
      using base::base;
      fon9::SessionFactorySP CreateSessionFactory() override {
         if (auto authMgr = this->GetAuthMgr())
            return new fon9::rc::RcSessionServer_Factory(
               this->Name_, authMgr, this->NameVer_, this->Description_,
               this->DeSbInterval_,
               authMgr->Agents_->Get<fon9::auth::PoDupLogonAgent>(fon9_kCSTR_PoDupLogonAgent_Name)
               );
         return nullptr;
      }
      bool OnUnknownTag(fon9::seed::PluginsHolder& holder, fon9::StrView tag, fon9::StrView value) override {
         if (tag == "Ver")
            this->NameVer_.assign(value);
         else if (tag == "Desp")
            this->Description_.assign(value);
         else if (tag == "DeSb")
            this->DeSbInterval_ = fon9::StrTo(value, fon9::TimeInterval{});
         else
            return base::OnUnknownTag(holder, tag, value);
         return true;
      }
   };
   return ArgsParser{holder, "RcSessionServer"}.Parse(holder, args);
}

extern "C" fon9_API fon9::seed::PluginsDesc f9p_RcSessionServer;
static fon9::seed::PluginsPark f9pAutoPluginsReg{"RcSessionServer", &f9p_RcSessionServer};
fon9::seed::PluginsDesc f9p_RcSessionServer{"", &RcSessionServer_Start, nullptr, nullptr, };
