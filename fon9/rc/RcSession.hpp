// \file fon9/rc/RcSession.hpp
// \author fonwinz@gmail.com
#ifndef __fon9_rc_RcSession_hpp__
#define __fon9_rc_RcSession_hpp__
#include "fon9/rc/Rc.hpp"
#include "fon9/io/Session.hpp"
#include "fon9/auth/AuthBase.hpp"

namespace fon9 { namespace rc {

enum class RcSessionRole : int8_t {
   /// 負責在 OnDevice_LinkReady() 先送出 ProtocolVersion: "f9rc.0".
   /// 負責在一段時間沒送資料時, 送出 Hb.
   ProtocolInitiator = 0x01,
   /// 負責在收到 ProtocolVersion 之後, 回應 ProtocolVersion.
   ProtocolAcceptor = 0x02,

   /// 一般使用者端: 負責主動送出 ProtocolVersion, 及登入要求.
   /// 需 RcFuncConnClient, RcFuncSaslClient; 配合運作.
   User = ProtocolInitiator,
   /// 一般主機端: 負責回應 ProtocolVersion, 及驗證登入要求.
   /// 須 RcFuncConnServer, RcFuncSaslServer; 配合運作.
   Host = ProtocolAcceptor,
};
fon9_ENABLE_ENUM_BITWISE_OP(RcSessionRole);

/// \ingroup rc
/// 使用 f9rc 協定的處理程序.
class fon9_API RcSession : public io::Session {
   fon9_NON_COPY_NON_MOVE(RcSession);
   using base = io::Session;
public:
   using ChecksumT = uint16_t;

   const RcFunctionMgrSP   FunctionMgr_;
   const RcSessionRole     Role_;

   /// userId 格式 = "authc?authz"; 例: "fon"; "fon.t01?fon";
   /// userId 解析後, 會填入: UserId_ = authc; AuthzId_ = authz;
   RcSession(RcFunctionMgrSP mgr, RcSessionRole role, f9rc_RcFlag flags = f9rc_RcFlag{}, StrView userId = StrView{});
   ~RcSession();

   /// rbuf 必須已儲存了符合規則的 Function param.
   /// 在透過 Device 送出前, 這裡會加上:
   /// - checksum(如果需要) + fnCode + ByteArraySizeToBitvT(rbuf的資料量)
   /// - 若 if (!this->LocalParam_.IsNoChecksum()) 則加上 checksum.
   void Send(f9rc_FunctionCode fnCode, RevBufferList&& rbuf);

   /// 發生嚴重錯誤, 強制結束 Session.
   void ForceLogout(std::string reason);
   /// 收到對方的 Logout 訊息: 呼叫 Device 關閉連線.
   void OnLogoutReceived(std::string reason);

   /// 在「協定選擇」溝通完畢後, 送出的第一個封包.
   /// - rbuf 必須包含:
   ///   - Client端, Connection REQ: "ClientApVer" + "Description".
   ///   - Server端, Connection ACK: "ServerApVer" + "Description" + "SASL mech list: 使用 ' '(space) 分隔"
   void SendConnecting(RevBufferList&& rbuf) {
      assert(this->SessionSt_ < RcSessionSt::Connecting);
      this->SetSessionSt(RcSessionSt::Connecting);
      this->Send(f9rc_FunctionCode_Connection, std::move(rbuf));
   }

   void SendSasl(RevBufferList&& rbuf) {
      if (RcSessionSt::Connecting <= this->SessionSt_ && this->SessionSt_ <= RcSessionSt::Logoning) {
         this->SetSessionSt(RcSessionSt::Logoning);
         this->Send(f9rc_FunctionCode_SASL, std::move(rbuf));
      }
   }
   void OnSaslDone_AtServer(auth::AuthR rcode, StrView userId, StrView authzId) {
      assert(this->UserId_.empty() && this->AuthzId_.empty());
      this->UserId_.assign(userId);
      this->AuthzId_.assign(authzId);
      this->OnSaslDone(rcode);
   }
   void OnSaslDone_AtClient(auth::AuthR rcode) {
      assert(!this->UserId_.empty());
      this->OnSaslDone(rcode);
   }

   void ResetNote(f9rc_FunctionCode fnCode, RcFunctionNoteSP&& note) {
      if (static_cast<size_t>(fnCode) < this->Notes_.size())
         this->Notes_[static_cast<size_t>(fnCode)] = std::move(note);
   }
   template <typename T>
   T* GetNote(f9rc_FunctionCode fnCode) const {
      if (static_cast<size_t>(fnCode) < this->Notes_.size())
         return dynamic_cast<T*>(this->Notes_[static_cast<size_t>(fnCode)].get());
      return nullptr;
   }
   RcFunctionNote* GetNote(f9rc_FunctionCode fnCode) const {
      if (static_cast<size_t>(fnCode) < this->Notes_.size())
         return this->Notes_[static_cast<size_t>(fnCode)].get();
      return nullptr;
   }

   struct ProtocolParam {
      f9rc_RcFlag Flags_{};
      char        Padding__[2];
      int         RcVer_{-1};
      bool IsNoChecksum() const {
         return IsEnumContains(this->Flags_, f9rc_RcFlag_NoChecksum);
      }
      // local 端的 ApVer_ 放在 RcFuncConnection::ApVersion_;
   };
   const ProtocolParam& GetLocalParam() const {
      return this->LocalParam_;
   }

   struct ProtocolParamRemote : public ProtocolParam {
      CharVector  ApVer_;
   };
   const ProtocolParamRemote& GetRemoteParam() const {
      return this->RemoteParam_;
   }
   void SetRemoteApVer(CharVector&& ver) {
      this->RemoteParam_.ApVer_ = std::move(ver);
   }

   const CharVector& GetUserId() const {
      return this->UserId_;
   }
   const CharVector& GetAuthzId() const {
      return this->AuthzId_;
   }
   StrView GetUserIdForAuthz() const {
      return ToStrView(this->AuthzId_.empty() ? this->UserId_ : this->AuthzId_);
   }
   /// 通常只有在 Server 端才會在斷線後清除 UserId 的訊息;
   /// 通常會配合 OnSaslDone_AtServer(auth::AuthR rcode, StrView userId, StrView authzId); 設定 UserId;
   void ClearUserId() {
      this->UserId_.clear();
      this->AuthzId_.clear();
   }

   const CharVector& GetRemoteIp() const {
      return this->RemoteIp_;
   }
   TimeStamp GetLastRecvTime() const {
      return this->LastRecvTime_;
   }
   TimeStamp GetLastSentTime() const {
      return this->LastSentTime_;
   }
   f9rc_FunctionCode GetRxFunctionCode() const {
      return this->RxFunctionCode_;
   }
   RcSessionSt GetSessionSt() const {
      return this->SessionSt_;
   }

   /// 傳回密碼字串, 預設傳回 StrView{}, 由 Client Session 自行實作.
   virtual StrView GetAuthPassword() const;

   io::Device* GetDevice() const {
      return this->Dev_;
   }

protected:
   void OnDevice_Initialized(io::Device& dev) override;
   void OnDevice_Destructing(io::Device& dev) override;
   void OnDevice_StateChanged(io::Device& dev, const io::StateChangedArgs& e) override;
   io::RecvBufferSize OnDevice_LinkReady(io::Device& dev) override;
   io::RecvBufferSize OnDevice_Recv(io::Device& dev, DcQueue& rxbuf) override;
   std::string SessionCommand(io::Device& dev, StrView cmdln) override;
   void OnDevice_CommonTimer(io::Device& dev, TimeStamp now) override;

   void SendProtocolVersion();
   void SetSessionSt(RcSessionSt st) {
      this->SessionSt_ = st;
   }
   virtual void SetApReady(StrView info);

private:
   void ResetSessionSt(RcSessionSt st);
   void OnProtocolReady();
   io::RecvBufferSize OnDevice_Recv_CheckProtocolVersion(io::Device& dev, DcQueue& rxbuf);
   io::RecvBufferSize OnDevice_Recv_Parser(DcQueue& rxbuf);
   void OnSaslDone(auth::AuthR rcode);

   // Acceptor 可選擇那些參數使用對方的設定.
   // f9rc_RcFlag    FollowRemote_;

   RcSessionSt       SessionSt_{};
   bool              IsRxFunctionCodeReady_;
   f9rc_FunctionCode RxFunctionCode_;
   ChecksumT         RxChecksum_;
   char              pad_____________[2];
   TimeStamp         LastRecvTime_;
   TimeStamp         LastSentTime_;
   CharVector        UserId_;
   CharVector        AuthzId_;
   CharVector        RemoteIp_;
   io::Device*       Dev_{};
   ProtocolParamRemote  RemoteParam_;
   ProtocolParam        LocalParam_;

   struct RcFuncNoteSP {
      fon9_NON_COPY_NON_MOVE(RcFuncNoteSP);
      RcFunctionNote* Ptr_;
      RcFuncNoteSP(RcFunctionNoteSP&& px) : Ptr_{px.release()} {}
      RcFuncNoteSP() : Ptr_{nullptr} {}
      ~RcFuncNoteSP() { this->FreePtr(); }
      void FreePtr() {
         if (this->Ptr_) {
            this->Ptr_->RcFunctionNote_FreeThis();
            this->Ptr_ = nullptr;
         }
      }
      void operator=(RcFunctionNoteSP&& px) {
         this->FreePtr();
         this->Ptr_ = px.release();
      }
      RcFunctionNote* get() const {
         return this->Ptr_;
      }
   };
   using Notes = std::array<RcFuncNoteSP, f9rc_FunctionCode_Count>;
   Notes Notes_;
};

} } // namespaces
#endif//__fon9_rc_RcSession_hpp__
