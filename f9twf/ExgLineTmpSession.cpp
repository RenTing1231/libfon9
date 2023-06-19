// \file f9twf/ExgLineTmpSession.cpp
// \author fonwinz@gmail.com
#include "f9twf/ExgLineTmpSession.hpp"
#include "f9twf/ExgTmpLinkSys.hpp"
#include "f9twf/ExgTradingLineMgr.hpp"
#include "fon9/io/Device.hpp"
#include "fon9/io/SocketClientConfig.hpp"

namespace f9twf {

#define kLinkSysTimeoutInterval  fon9::TimeInterval_Second(10)

//--------------------------------------------------------------------------//

ExgLineTmpSession::~ExgLineTmpSession() {
   this->Log_.UpdateLogHeader();
}
void ExgLineTmpSession::CheckApBroken(TmpSt st) {
   if (this->TmpSt_ == TmpSt::ApReady) {
      this->TmpSt_ = st;
      this->Log_.UpdateLogHeader();
      this->OnExgTmp_ApBroken();
   }
}
void ExgLineTmpSession::AsyncClose(std::string cause) {
   this->CheckApBroken(TmpSt::DevClose);
   this->Dev_->AsyncLingerClose(std::move(cause));
}

//--------------------------------------------------------------------------//

void ExgLineTmpSession::WriteLogLinkSt(const fon9::io::StateUpdatedArgs& e) {
   fon9::RevBufferList rbuf{128};
   fon9::RevPrint(rbuf, "|LinkSt|", fon9::io::GetStateStr(e.State_), '|', e.Info_);
   rbuf.AllocPacket<TmpLogPacketHeader>()
      ->Initialize(TmpLogPacketType::Info, fon9::CalcDataSize(rbuf.cfront()), fon9::UtcNow());
   this->Log_.Append(rbuf.MoveOut());
}
void ExgLineTmpSession::OnDevice_Initialized(fon9::io::Device& dev) {
   this->Dev_ = &dev;
}
bool ExgLineTmpSession::OnDevice_BeforeOpen(fon9::io::Device& dev, std::string& cfgstr) {
   if (cfgstr.empty()) {
      // 在 Initialized 狀態下, 手動(或定時) reopen, 應使用上次的參數, 然後再 AppendDn();
      // 因為有可能在 P06,P07 重載後, 手動(或定時) reopen;
      cfgstr = this->DevOpenArgs_;
   }
   else { // 保留參數, 提供 reopen 使用.
      this->DevOpenArgs_ = cfgstr;
   }
   this->HasTaiFexDN_ = this->LineMgr_.AppendDn(this->LineArgs_, cfgstr);
   if (this->HasTaiFexDN_) {
      this->LineMgr_.OnSession_StateUpdated(dev, &cfgstr, fon9::LogLevel::Info);
      return true;
   }
   // 改成在 ExgLineTmpSession::OnDevice_StateChanged():
   // 當有 ConfigError 時, 關閉 dev, 透過 ClosedReopenInterval_ 重新開啟;
   return true;
   // fon9::StrView                 cfgs{&cfgstr};
   // fon9::io::SocketClientConfig  clicfg;
   // clicfg.SetDefaults();
   // struct Parser : public fon9::io::SocketClientConfig::Parser {
   //    fon9_NON_COPY_NON_MOVE(Parser);
   //    using base = fon9::io::SocketClientConfig::Parser;
   //    using base::base;
   //    bool OnErrorBreak(ErrorEventArgs& e) {
   //       // 不認識的 Tag: 可能是 Device.Options, 所以視為成功.
   //       return e.Result_ != Result::EUnknownTag;
   //    }
   // };
   // Parser{clicfg}.Parse(cfgs);
   // if (!clicfg.AddrRemote_.IsUnspec() || !clicfg.DomainNames_.empty())
   //    return true;
   // this->LineMgr_.OnSession_StateUpdated(dev, "dn not found(P06 or P07 error)", fon9::LogLevel::Error);
   // if (dev.OpImpl_GetState() == fon9::io::State::Initialized) {
   //    // 可能是 this->IoManager_.ExgMapMgr_ 的 P06、P07 尚未載入.
   //    // 所以稍等一會兒之後再試一次.
   //    dev.CommonTimerRunAfter(fon9::TimeInterval_Second(1));
   // }
   // return false;
}
void ExgLineTmpSession::OnDevice_StateChanged(fon9::io::Device& dev, const fon9::io::StateChangedArgs& e) {
   (void)dev;
   this->WriteLogLinkSt(e.After_);
   if (e.BeforeState_ == fon9::io::State::LinkReady)
      this->CheckApBroken(TmpSt::ApBroken);
   else if (e.After_.State_ == fon9::io::State::ConfigError)
      this->AsyncClose("Error: config or P06 or P07"); // 關閉後重新設定 & 開啟.
}
void ExgLineTmpSession::OnDevice_StateUpdated(fon9::io::Device& dev, const fon9::io::StateUpdatedArgs& e) {
   (void)dev;
   this->WriteLogLinkSt(e);
}
fon9::io::RecvBufferSize ExgLineTmpSession::OnDevice_LinkReady(fon9::io::Device& dev) {
   this->LastRxTime_.Assign0();

   ExgLineTmpRevBuffer buf;
   buf.Alloc<TmpL10>().Initialize();
   this->SendTmpNoSeqNum(fon9::UtcNow(), std::move(buf));

   dev.CommonTimerRunAfter(kLinkSysTimeoutInterval);
   return fon9::io::RecvBufferSize::Default;
}

//--------------------------------------------------------------------------//

fon9::io::RecvBufferSize ExgLineTmpSession::OnDevice_Recv(fon9::io::Device& dev, fon9::DcQueue& rxbuf) {
   (void)dev;
   char  pkbuf[64 * 1024 + 64];
   static_assert(sizeof(TmpMsgLength) == 2, "sizeof TmpPacket over 64KB?");
   while (auto* pktmp = reinterpret_cast<const TmpHeaderSt*>(rxbuf.Peek(pkbuf, sizeof(TmpMsgLength)))) {
      const auto pksz = pktmp->GetPacketSize();
      if ((pktmp = reinterpret_cast<const TmpHeaderSt*>(rxbuf.Peek(pkbuf, pksz))) == nullptr)
         return fon9::io::RecvBufferSize::Default;

      if (*reinterpret_cast<const TmpCheckSum*>(reinterpret_cast<const char*>(pktmp) + pksz - sizeof(TmpCheckSum))
          != TmpCalcCheckSum(*pktmp, pksz)) {
         this->AsyncClose("CheckSum error");
         return fon9::io::RecvBufferSize::CloseRecv;
      }

      this->LastRxTime_ = fon9::UtcNow();

      fon9::RevBufferList rbuf{0};
      char* lhdr = rbuf.AllocBuffer(pksz + sizeof(TmpLogPacketHeader));
      reinterpret_cast<TmpLogPacketHeader*>(lhdr)
         ->Initialize(TmpLogPacketType::Recv, pksz, this->LastRxTime_);
      memcpy(lhdr + sizeof(TmpLogPacketHeader), pktmp, pksz);
      this->Log_.Append(rbuf.MoveOut());

      if (fon9_LIKELY(this->TmpSt_ == TmpSt::ApReady)) {
         if (fon9_UNLIKELY(pktmp->MessageType_ == TmpMessageType_R(04))) {
            // 收到 R04, 回覆 R05.
            ExgLineTmpRevBuffer buf;
            buf.Alloc<TmpR05>().Initialize(TmpMessageType_R(05));
            this->SendTmpNoSeqNum(this->LastRxTime_, std::move(buf));
         }
         else if (fon9_LIKELY(pktmp->MessageType_ >= TmpMessageType::ApSys)) {
            if (fon9_UNLIKELY(!this->OnRecvTmpApPacket(*pktmp))) {
               rxbuf.PopConsumed(pksz);
               return fon9::io::RecvBufferSize::CloseRecv;
            }
         }
         else {
            this->CheckApBroken(TmpSt::TmpRelink);
            this->OnRecvTmpLinkSys(*pktmp);
         }
      }
      else {
         this->OnRecvTmpLinkSys(*pktmp);
      }
      rxbuf.PopConsumed(pksz);
   }
   return fon9::io::RecvBufferSize::Default;
}
bool ExgLineTmpSession::OnRecvTmpApPacket(const TmpHeader& pktmp) {
   if (auto seqn = TmpGetValueU(pktmp.MsgSeqNum_)) {
      if (fon9_UNLIKELY(!this->Log_.CheckSetRxSeqNum(seqn))) {
         // 序號不連續: 重新登入.
         this->AsyncClose("RxMsgSeqNum has gap");
         return false;
      }
   }
   this->OnExgTmp_ApPacket(pktmp);
   return true;
}
void ExgLineTmpSession::OnRecvTmpLinkSys(const TmpHeaderSt& pktmp) {
   if (pktmp.StatusCode_ != 0) {
      // LinkSystem 不正確, 先關閉, 等一會兒再連線.
      // device 的設定必須增加: "|ClosedReopen=5" 表示關閉後 5 秒重連.
      this->AsyncClose(fon9::RevPrintTo<std::string>("Rx.LinkSystem.Err"
                                                     "|msg=", pktmp.MessageType_,
                                                     "|st=", pktmp.StatusCode_));
      return;
   }
   fon9_WARN_DISABLE_SWITCH;
   fon9_MSC_WARN_DISABLE_NO_PUSH(4063); // case '10' is not a valid value for switch of enum TmpMessageType
   ExgLineTmpRevBuffer buf;
   switch (pktmp.MessageType_) {
   case TmpMessageType_L(10):
      buf.Alloc<TmpL20>().Initialize(TmpMessageType_L(20));
      break;
   case TmpMessageType_L(30):
   {
      TmpL40&  L40 = buf.Alloc<TmpL40>();
      this->Log_.CheckL30_EndOutBoundNum(TmpGetValueU(static_cast<const TmpL30*>(&pktmp)->EndOutBoundNum_));
      L40.Initialize(*static_cast<const TmpL30*>(&pktmp), this->Log_.LastRxMsgSeqNum());
      // 計算 KEY-VALUE = (APPEND-NO * PASSWORD)取千與百二位數字。
      L40.KeyValue_ = static_cast<uint8_t>(((TmpGetValueU(L40.AppendNo_)
                                             * static_cast<unsigned>(this->LineArgs_.GetPassNum()))
                                            / 100) % 100);
      L40.ApCode_ = this->LineArgs_.ApCode_;
      this->SystemType_ = static_cast<const TmpL30*>(&pktmp)->SystemType_;
      break;
   }
   case TmpMessageType_L(41):
   {  // 從 L41.Data_[] 取回 ApPacket(); 但是此時尚未 ApReady !!!!
      const uint8_t* rpk = static_cast<const TmpL41*>(&pktmp)->Data_;
      size_t         rsz = TmpGetValueU(static_cast<const TmpL41*>(&pktmp)->MsgLength_);
      rsz -= sizeof(TmpL41) - sizeof(TmpMsgLength) - sizeof(static_cast<const TmpL41*>(&pktmp)->Data_);
      for (;;) {
         auto pksz = reinterpret_cast<const TmpHeader*>(rpk)->GetPacketSize();
         if (pksz > rsz)
            break;
         this->OnRecvTmpApPacket(*reinterpret_cast<const TmpHeader*>(rpk));
         if ((rsz -= pksz) <= 0)
            break;
         rpk += pksz;
      }
      buf.Alloc<TmpL42>().Initialize(TmpMessageType_L(42));
      break;
   }
   case TmpMessageType_L(50):
      this->ApHbInterval_ = fon9::TimeInterval_Second(static_cast<const TmpL50*>(&pktmp)->HeartBtInt_);
      this->MaxFlowCtrlCnt_ = TmpGetValueU(static_cast<const TmpL50*>(&pktmp)->MaxFlowCtrlCnt_);
      buf.Alloc<TmpL60>().Initialize(TmpMessageType_L(60));
      break;
   default:
      return;
   }
   fon9_WARN_POP;

   this->SendTmpNoSeqNum(this->LastRxTime_, std::move(buf));

   if (pktmp.MessageType_ != TmpMessageType_L(50)) {
      if (pktmp.MessageType_ == TmpMessageType_L(41)) {
         // L41.Data_[] 處理完畢後, 記錄最後序號狀態.
         this->Log_.UpdateLogHeader();
      }
      return;
   }

   // 必須先送出回覆, 然後才能觸發 ApReady 事件, 避免在事件處理過程送出其他 TMP 訊息.
   this->TmpSt_ = TmpSt::ApReady;
   char msgApReady[] = "|-:ApReady|";
   msgApReady[1] = TmpApCodeToAlpha(this->LineArgs_.ApCode_);

   fon9::RevBufferList rbuf{128};
   fon9::RevPrint(rbuf, fon9::StrView{msgApReady, sizeof(msgApReady) - 1});
   rbuf.AllocPacket<TmpLogPacketHeader>()
      ->Initialize(TmpLogPacketType::Info, fon9::CalcDataSize(rbuf.cfront()), this->LastRxTime_);
   this->Log_.Append(rbuf.MoveOut());

   this->GetApReadyInfo(rbuf);
   fon9::RevPrint(rbuf, fon9::StrView{msgApReady + 1, sizeof(msgApReady) - 3}); // +1, -3: 移除頭尾的 '|' 及 EOS;
   this->LineMgr_.OnSession_StateUpdated(
      *this->Dev_,
      fon9::ToStrView(fon9::BufferTo<std::string>(rbuf.MoveOut())),
      fon9::LogLevel::Info);
   this->OnExgTmp_ApReady();
}
void ExgLineTmpSession::GetApReadyInfo(fon9::RevBufferList& rbuf) {
   (void)rbuf;
}

void ExgLineTmpSession::OnDevice_CommonTimer(fon9::io::Device& dev, fon9::TimeStamp now) {
   if (dev.OpImpl_GetState() == fon9::io::State::Initialized) {
      // 先前因 P06,P07 尚未備妥, 無法建立 open 參數,
      // 在 OnDevice_BeforeOpen() 有啟動定時 reopen,
      // => 所以在此透過 dev.AsyncOpen() 再次檢查 P06,P07;
      dev.AsyncOpen(std::string());
      return;
   }
   fon9::TimeInterval tmdiff = (now - this->LastRxTime_);
   if (fon9_LIKELY(this->TmpSt_ == TmpSt::ApReady)) {
      if (tmdiff >= this->ApHbInterval_ * 2) {
         this->AsyncClose("ApTimeout.");
         return;
      }
      tmdiff = this->ApHbInterval_ - (now - this->LastTxTime_);
      if (tmdiff <= fon9::TimeInterval_Second(1)) {
         ExgLineTmpRevBuffer buf;
         buf.Alloc<TmpR04>().Initialize(TmpMessageType_R(04));
         this->SendTmpNoSeqNum(now, std::move(buf));
         tmdiff = this->ApHbInterval_;
      }
   }
   else {
      if (tmdiff > kLinkSysTimeoutInterval) {
         this->AsyncClose("LinkSysTimeout.");
         return;
      }
      tmdiff = kLinkSysTimeoutInterval;
   }
   dev.CommonTimerRunAfter(tmdiff);
   this->Log_.UpdateLogHeader();
}

//--------------------------------------------------------------------------//

void ExgLineTmpSession::SendTmpNoSeqNum(fon9::TimeStamp now, ExgLineTmpRevBuffer&& buf) {
   this->LastTxTime_ = now;

   char* pkptr = const_cast<char*>(buf.RBuf_.GetCurrent());
   auto  pksz = fon9::CalcDataSize(buf.RBuf_.cfront());
   assert(buf.RBuf_.cfront()->GetNext() == nullptr); // RBuf 僅允許使用一個 Node;
   reinterpret_cast<TmpHeader*>(pkptr)->MsgTime_.AssignFrom(now);
   reinterpret_cast<TmpHeader*>(pkptr)->SessionFcmId_ = this->LineArgs_.SessionFcmId_;
   reinterpret_cast<TmpHeader*>(pkptr)->SessionId_ = this->LineArgs_.SessionId_;
   *reinterpret_cast<TmpCheckSum*>(pkptr + pksz - sizeof(TmpCheckSum))
      = TmpCalcCheckSum(*reinterpret_cast<TmpHeader*>(pkptr), pksz);
   this->Dev_->Send(pkptr, pksz);

   buf.RBuf_.AllocPacket<TmpLogPacketHeader>()
      ->Initialize(TmpLogPacketType::Send, pksz, now);
   this->Log_.Append(buf.RBuf_.MoveOut());
}

} // namespaces
