// \file fon9/rc/RcSession.cpp
// \author fonwinz@gmail.com
#include "fon9/rc/RcSession.hpp"
#include "fon9/io/Device.hpp"
#include "fon9/Log.hpp"
#include "fon9/BitvArchive.hpp"

namespace fon9 { namespace rc {

#define kRcSession_RecvBufferSize      static_cast<io::RecvBufferSize>(1024 * 4)
#ifdef _DEBUG
#define kRcSession_Logon_Timer         TimeInterval_Second(295)
#define kRcSession_HbInterval          TimeInterval_Second(300)
#else
#define kRcSession_Logon_Timer         TimeInterval_Second(5)
#define kRcSession_HbInterval          TimeInterval_Second(30)
#endif

// 每次 FunctionParam 大小限制, 避免封包錯誤(或免惡意連線), 造成記憶體用盡.
#define kRcSession_FunctionParam_MaxSize  (1024 * 1024 * 64)

#define cstrProtocolHead               "f9rc."
#define cstrProtocolHeadVer            "0"
#define cstrProtocolParam_NoChecksum   "Checksum=N"
#define cstrProtocolHeadAllParams      cstrProtocolParam_NoChecksum // "," cstrProtocolParam_xxx "," cstrProtocolParam_yyy
#define cstrProtocolHeadMax            cstrProtocolHead cstrProtocolHeadVer ":" cstrProtocolHeadAllParams "\n"
#define cstrProtocolHeadErr            cstrProtocolHead cstrProtocolHeadVer ":err="
//--------------------------------------------------------------------------//
using ChecksumT = RcSession::ChecksumT;
static inline ChecksumT FeedCalcCheckSum(ChecksumT cksum, const byte* ptr, size_t sz) {
   do {
      cksum = static_cast<ChecksumT>((((cksum & 1) << 15) | (cksum >> 1)) + *ptr++);
   } while (--sz > 0);
   return cksum;
}
static ChecksumT BufferList_CalcRcChecksum(const BufferNode* node) {
   ChecksumT cksum = 0;
   while (node) {
      if (auto sz = node->GetDataSize())
         cksum = FeedCalcCheckSum(cksum, node->GetDataBegin(), sz);
      node = node->GetNext();
   }
   return cksum;
}
static ChecksumT FuncParam_CalcRcChecksum(DcQueue& buf, f9rc_FunctionCode funcCode, size_t paramSize) {
   ChecksumT   cksum = static_cast<ChecksumT>(funcCode);
   if (fon9_UNLIKELY(paramSize <= 0))
      return cksum;
   if (size_t blksz = buf.GetCurrBlockSize()) {
      if (blksz > paramSize)
         blksz = paramSize;
      cksum = FeedCalcCheckSum(cksum, buf.Peek1(), blksz);
      if ((paramSize -= blksz) <= 0)
         return cksum;
   }

   DcQueue::DataBlock blk;
   const void* peekHandler = buf.PeekNextBlock(NULL, blk);
   while (peekHandler) {
      if (blk.first && blk.second > 0) {
         if (blk.second > paramSize)
            blk.second = paramSize;
         cksum = FeedCalcCheckSum(cksum, blk.first, blk.second);
         if ((paramSize -= blk.second) <= 0)
            return cksum;
      }
      peekHandler = buf.PeekNextBlock(peekHandler, blk);
   }
   return cksum;
}
//--------------------------------------------------------------------------//
RcSession::RcSession(RcFunctionMgrSP mgr, RcSessionRole role, f9rc_RcFlag flags, StrView userId)
   : FunctionMgr_(std::move(mgr))
   , Role_(role) {
   this->LocalParam_.Flags_ = flags;
   this->UserId_.assign(StrFetchNoTrim(userId, '?')); // StrFetchNoTrim() 之後剩下的 userId 則為 authz;
   this->AuthzId_.assign(userId);
}
RcSession::~RcSession() {
}
StrView RcSession::GetAuthPassword() const {
   return StrView{};
}
std::string RcSession::SessionCommand(io::Device& dev, StrView cmdln) {
   return base::SessionCommand(dev, cmdln);
}
void RcSession::OnDevice_Initialized(io::Device& dev) {
   this->Dev_ = &dev;
}
void RcSession::OnDevice_Destructing(io::Device&) {
   this->Dev_ = nullptr;
}
void RcSession::ResetSessionSt(RcSessionSt st) {
   this->SetSessionSt(st);
   this->RemoteParam_.RcVer_ = -1;
   this->RemoteParam_.Flags_ = f9rc_RcFlag{};
   this->IsRxFunctionCodeReady_ = false;
   this->LastRecvTime_ = this->LastSentTime_ = TimeStamp{};
   this->RemoteIp_.clear();
}
void RcSession::OnDevice_StateChanged(io::Device& dev, const io::StateChangedArgs& e) {
   if (e.BeforeState_ == io::State::LinkReady) {
      this->ResetSessionSt(RcSessionSt::Closing);
      this->FunctionMgr_->OnSessionLinkBroken(*this);
   }
   else if (e.After_.State_ == io::State::LinkReady) {
      this->ResetSessionSt(RcSessionSt::DeviceLinkReady);
      // 連線了 => 取得 RemoteIp, 不包含「:port」
      StrView remote = StrFindValue(&e.After_.DeviceId_, "R");
      this->RemoteIp_.assign(StrFetchNoTrim(remote, ':'));
      // 後續的連線成功事項, 交給 OnDevice_LinkReady() 處理.
   }
   base::OnDevice_StateChanged(dev, e);
}
io::RecvBufferSize RcSession::OnDevice_LinkReady(io::Device& dev) {
   // this->RemoteIp_ 已在 OnDevice_StateChanged() 裡面 State::LinkReady 時處理.
   // 啟動登入計時, 逾時未登入成功, 則斷線.
   dev.CommonTimerRunAfter(kRcSession_Logon_Timer);
   if (IsEnumContains(this->Role_, RcSessionRole::ProtocolInitiator))
      // 啟動端送出 ("f9rc.0:Checksum=N\n" 或 "f9rc.0\n") + Function.Connection
      this->SendProtocolVersion();
   this->FunctionMgr_->OnSessionLinkReady(*this);
   return kRcSession_RecvBufferSize;
}
void RcSession::Send(f9rc_FunctionCode fnCode, RevBufferList&& rbuf) {
   ByteArraySizeToBitvT(rbuf, CalcDataSize(rbuf.cfront()));
   char* pout = rbuf.AllocPrefix(sizeof(fnCode));
   PutBigEndian(pout -= sizeof(fnCode), fnCode);
   rbuf.SetPrefixUsed(pout);
   if (!this->LocalParam_.IsNoChecksum()) {
      pout = rbuf.AllocPrefix(sizeof(ChecksumT));
      PutBigEndian(pout -= sizeof(ChecksumT), BufferList_CalcRcChecksum(rbuf.cfront()));
      rbuf.SetPrefixUsed(pout);
   }
   this->Dev_->Send(rbuf.MoveOut());
   this->LastSentTime_ = UtcNow();
}
void RcSession::ForceLogout(std::string reason) {
   if (this->SessionSt_ < RcSessionSt::ProtocolReady) {
      this->Dev_->AsyncClose(std::move(reason));
   } else {
      RevBufferList rbuf{64};
      ToBitv(rbuf, reason);
      this->Send(f9rc_FunctionCode_Logout, std::move(rbuf));
      this->Dev_->AsyncLingerClose(std::move(reason));
   }
}
void RcSession::OnLogoutReceived(std::string reason) {
   this->SetSessionSt(RcSessionSt::Closing);
   this->Dev_->AsyncLingerClose(std::move(reason));
}
void RcSession::SetApReady(StrView info) {
   this->SetSessionSt(RcSessionSt::ApReady);
   if (!this->Dev_->Manager_) // 無 manager(client 端), 直接記錄登入結果.
      fon9_LOG_INFO("RcSession.ApReady|dev=", ToPtr(this->Dev_),
                    "|RemoteAp=", this->RemoteParam_.ApVer_,
                    "|user=", this->UserId_,
                    "|info=", info);
   else { // 有 manager(server 端), 記錄登入結果 & 其他資訊.
      std::string exinfo;
      exinfo.reserve(256);
      exinfo.append("|RemoteAp=");
      this->RemoteParam_.ApVer_.AppendTo(exinfo);
      exinfo.append("|from=");
      this->RemoteIp_.AppendTo(exinfo);
      exinfo.append("|user=");
      this->UserId_.AppendTo(exinfo);
      if (!this->AuthzId_.empty()) {
         exinfo.append("|authz=");
         this->AuthzId_.AppendTo(exinfo);
      }
      exinfo.push_back('|');
      info.AppendTo(exinfo);
      this->Dev_->Manager_->OnSession_StateUpdated(*this->Dev_, ToStrView(exinfo), LogLevel::Info);
   }
   this->FunctionMgr_->OnSessionApReady(*this);
}
void RcSession::OnSaslDone(auth::AuthR rcode) {
   if (rcode.RCode_ == fon9_Auth_Success || rcode.RCode_ == fon9_Auth_PassChanged)
      this->SetApReady(ToStrView(rcode.Info_));
   else {
      // fon9_LOG_WARN():
      RevBufferList rbuf{kLogBlockNodeSize};
      RevPrint(rbuf, "|rcode=", rcode.RCode_, "|err=", rcode.Info_, '\n');
      if (!this->AuthzId_.empty())
         RevPrint(rbuf, "|authz=", this->AuthzId_);
      RevPrint(rbuf, "RcSession.OnSaslDone|dev=", ToPtr(this->Dev_), "|user=", this->UserId_);
      LogWrite(LogLevel::Warn, std::move(rbuf));
      this->ForceLogout(std::move(rcode.Info_));
   }
}

io::RecvBufferSize RcSession::OnDevice_Recv(io::Device& dev, DcQueue& rxbuf) {
   if (fon9_UNLIKELY(this->RemoteParam_.RcVer_ < 0)) { // 連線後, 尚未溝通「協定版本」.
      auto rbsz = this->OnDevice_Recv_CheckProtocolVersion(dev, rxbuf);
      if (rbsz != io::RecvBufferSize::Default)
         return rbsz;
      // 「協定版本」溝通完畢, 繼續檢查收到的資料.
   }
   try {
      return this->OnDevice_Recv_Parser(rxbuf);
   }
   catch (BitvDecodeError& e) {
      this->ForceLogout(std::string("Rc.BitvDecodeError:") + e.what());
   }
   catch (std::exception& e) {
      this->ForceLogout(std::string("Rc.Exception:") + e.what());
   }
   return io::RecvBufferSize::NoRecvEvent;
}
io::RecvBufferSize RcSession::OnDevice_Recv_Parser(DcQueue& rxbuf) {
   if (!this->IsRxFunctionCodeReady_) { // 尚未收到 FunctionCode.
   __CONTINUE_PARSE_BUFFER:
      const byte* phead;
      if (this->RemoteParam_.IsNoChecksum()) {
         static_assert(sizeof(f9rc_FunctionCode) == 1, "sizeof(f9rc_FunctionCode)必須為1, 才能使用 rxbuf.Peek1()");
         if ((phead = rxbuf.Peek1()) == nullptr)
            return kRcSession_RecvBufferSize;
         this->RxFunctionCode_ = static_cast<f9rc_FunctionCode>(*phead);
         rxbuf.PopConsumed(1u);
      }
      else {
         char  headbuf[sizeof(ChecksumT) + sizeof(f9rc_FunctionCode)];
         if ((phead = static_cast<const byte*>(rxbuf.Peek(headbuf, sizeof(headbuf)))) == nullptr)
            return kRcSession_RecvBufferSize;
         this->RxChecksum_ = GetBigEndian<ChecksumT>(phead);
         this->RxFunctionCode_ = static_cast<f9rc_FunctionCode>(phead[sizeof(ChecksumT)]);
         rxbuf.PopConsumed(sizeof(headbuf));
      }
      this->IsRxFunctionCodeReady_ = true;
   }
   RcFunctionParam fnParam{rxbuf};
   auto            hdrSize = PeekBitvByteArraySize(rxbuf, fnParam.RemainParamSize_);
   if (fon9_UNLIKELY(hdrSize < PeekBitvByteArraySizeR::DataReady)) {
      if (fon9_UNLIKELY(fnParam.RemainParamSize_ > kRcSession_FunctionParam_MaxSize)) {
         this->ForceLogout("Function param size over limit");
         return io::RecvBufferSize::NoRecvEvent;
      }
      return kRcSession_RecvBufferSize;
   }
   this->IsRxFunctionCodeReady_ = false;
   if (!this->RemoteParam_.IsNoChecksum()) {
      // 檢查 Checksum: 需包含已經取出的 this->FunctionCode_
      ChecksumT cksum = FuncParam_CalcRcChecksum(rxbuf, this->RxFunctionCode_, static_cast<size_t>(hdrSize) + fnParam.RemainParamSize_);
      if (cksum != this->RxChecksum_) {
         this->ForceLogout("Checksum error");
         return io::RecvBufferSize::NoRecvEvent;
      }
   }
   rxbuf.PopConsumed(static_cast<size_t>(hdrSize));
   this->LastRecvTime_ = fnParam.RecvTime_;
   if (static_cast<size_t>(this->RxFunctionCode_) < this->Notes_.size()) {
      if (RcFunctionNote* fnNote = this->Notes_[static_cast<size_t>(this->RxFunctionCode_)].get()) {
         fnNote->OnRecvFunctionCall(*this, fnParam);
         goto __CONTINUE_PARSE_BUFFER;
      }
      if (RcFunctionAgent* fnAgent = this->FunctionMgr_->Get(this->RxFunctionCode_)) {
         if (fon9_UNLIKELY(this->SessionSt_ < fnAgent->SessionStRequired_)) {
            this->ForceLogout("SessionSt not ready");
            return io::RecvBufferSize::NoRecvEvent;
         }
         fnAgent->OnRecvFunctionCall(*this, fnParam);
         goto __CONTINUE_PARSE_BUFFER;
      }
   }
   this->ForceLogout("Unknown function code");
   return io::RecvBufferSize::NoRecvEvent;
}
io::RecvBufferSize RcSession::OnDevice_Recv_CheckProtocolVersion(io::Device& dev, DcQueue& rxbuf) {
   // 等候 "f9rc.0:Checksum=N\n" 版本資訊.
   auto        blk{rxbuf.PeekCurrBlock()};
   const char* pend = static_cast<const char*>(memchr(blk.first, '\n', blk.second));
   if (pend == nullptr) {
      if (blk.second < (sizeof(cstrProtocolHeadMax) - 1))
         return kRcSession_RecvBufferSize;
   __UNKNOWN_PROTOCOL:;
      fon9_LOG_WARN("RcSession.CheckProtocolVersion|dev=", ToPtr(&dev), "|err=Unknown protocol.");
      dev.AsyncLingerClose("Unknown protocol.");
      return io::RecvBufferSize::NoRecvEvent;
   }
   if (memcmp(blk.first, cstrProtocolHead, sizeof(cstrProtocolHead) - 1) != 0)
      goto __UNKNOWN_PROTOCOL;
   const byte* phead = blk.first + sizeof(cstrProtocolHead) - 1;
   std::string errmsg;
   if (fon9_UNLIKELY((this->RemoteParam_.RcVer_ = (*phead++ - '0')) != 0)) {
      errmsg.assign(cstrProtocolHeadErr "Unknown protocol version.\n");
   __ERROR_CLOSE:;
      this->RemoteParam_.RcVer_ = -1;
      this->SetSessionSt(RcSessionSt::Closing);
      dev.StrSend(errmsg);
      dev.AsyncLingerClose(std::move(errmsg));
      return io::RecvBufferSize::NoRecvEvent;
   }
   switch (*phead++) {
   default:
      errmsg.assign(cstrProtocolHeadErr "Unknown protocol format.\n");
      goto __ERROR_CLOSE;
   case '\n':
      break;
   case ':':
      StrView  params{reinterpret_cast<const char*>(phead), pend};
      while(!params.IsNullOrEmpty()) {
         StrView  spr = StrFetchNoTrim(params, ',');
         if (spr == cstrProtocolParam_NoChecksum)
            this->RemoteParam_.Flags_ |= f9rc_RcFlag_NoChecksum;
         else {
            errmsg.assign(cstrProtocolHeadErr "Unknown protocol param:");
            spr.AppendTo(errmsg);
            errmsg.push_back('\n');
            goto __ERROR_CLOSE;
         }
      }
      break;
   }
   rxbuf.PopConsumed(static_cast<size_t>(pend - reinterpret_cast<const char*>(blk.first) + 1));
   this->OnProtocolReady();
   return io::RecvBufferSize::Default;
}
void RcSession::OnProtocolReady() {
   if (this->SessionSt_ < RcSessionSt::ProtocolReady)
      this->SetSessionSt(RcSessionSt::ProtocolReady);
   if (IsEnumContains(this->Role_, RcSessionRole::ProtocolAcceptor)) {
      // TODO: 預設參數要 follow remote? 或是由 remote 直接指定?
      this->LocalParam_.Flags_ = this->RemoteParam_.Flags_;
      this->SendProtocolVersion();
   }
   this->FunctionMgr_->OnSessionProtocolReady(*this);
}
void RcSession::SendProtocolVersion() {
   RevBufferList rbuf{64};
   RevPutChar(rbuf, '\n');
   struct PushParamAux {
      unsigned ParamCount_{};
      void CheckPushParam(RevBufferList& rbuf, bool hasParam, StrView strParam) {
         if (!hasParam)
            return;
         if (++this->ParamCount_ > 1)
            RevPutChar(rbuf, ',');
         RevPrint(rbuf, strParam);
      }
   };
   PushParamAux   aux;
   aux.CheckPushParam(rbuf, this->LocalParam_.IsNoChecksum(), cstrProtocolParam_NoChecksum);
   if (aux.ParamCount_ > 0)
      RevPutChar(rbuf, ':');
   RevPrint(rbuf, cstrProtocolHead cstrProtocolHeadVer);
   this->Dev_->SendBuffered(rbuf.MoveOut());
}
//--------------------------------------------------------------------------//
void RcSession::OnDevice_CommonTimer(io::Device& dev, TimeStamp now) {
   switch (this->SessionSt_) {
   case RcSessionSt::Closing:
      break;
   case RcSessionSt::DeviceLinkReady:
   case RcSessionSt::ProtocolReady:
   case RcSessionSt::Connecting:
   case RcSessionSt::Logoning:
      this->ForceLogout("Logon timeout.");
      break;
   case RcSessionSt::ApReady:
      const TimeInterval tiRecv = (kRcSession_HbInterval * 2) - (now - this->LastRecvTime_);
      if (tiRecv < TimeInterval_Second(1))
         this->ForceLogout("Heartbeat timeout.");
      else if (IsEnumContains(this->Role_, RcSessionRole::ProtocolAcceptor))
         dev.CommonTimerRunAfter(tiRecv);
      else { // RcSessionRole::ProtocolInitiator
         // - 因為有可能會有: 送出後,對方不需要回應的訊息.
         //   所以, 不能只用 [上次送訊息的時間] 來決定是否需要送 Hb.
         // - 太久沒送出 or 太久沒收到: 都需要送出 Hb:
         //   所以 (now - 最後收到的時間) or (now - 最後送出的時間) >= kRcSession_HbInterval 就需要送出 Hb;
         const TimeInterval hbAfter = (std::min(this->LastSentTime_, this->LastRecvTime_) + kRcSession_HbInterval) - now;
         if (hbAfter > TimeInterval_Second(1))
            dev.CommonTimerRunAfter(hbAfter);
         else {
            RevBufferList  rbuf{64};
            ToBitv(rbuf, now);
            this->Send(f9rc_FunctionCode_Heartbeat, std::move(rbuf));
            dev.CommonTimerRunAfter(kRcSession_HbInterval);
         }
      }
      break;
   }
}

} } // namespace
