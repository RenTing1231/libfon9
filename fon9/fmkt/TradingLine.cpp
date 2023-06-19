/// \file fon9/fmkt/TradingLine.cpp
/// \author fonwinz@gmail.com
#include "fon9/fmkt/TradingLine.hpp"
#include "fon9/TimedFileName.hpp"
#include "fon9/FilePath.hpp"

namespace fon9 { namespace fmkt {

/// 預設為 No; 當設為 EnabledYN::Yes, 則盡量使用同一條線路.
fon9_API EnabledYN   gTradingLineSelect_TryLastLine_YN{};

//--------------------------------------------------------------------------//
TradingLine::~TradingLine() {
}
bool TradingLine::IsOrigSender(const TradingRequest& req) const {
   (void)req;
   return false;
}
//--------------------------------------------------------------------------//
TradingLineHelper::~TradingLineHelper() {
}
SendRequestResult TradingLineHelper::OnAskFor_BeforeQueue(TradingRequest& req, const TLineLocker& tsvrAsker) {
   (void)req; (void)tsvrAsker;
   return SendRequestResult::Queuing;
}
SendRequestResult TradingLineHelper::OnAskFor_NoReadyLine(TradingRequest& req, const TLineLocker& tsvrAsker) {
   (void)req; (void)tsvrAsker;
   return SendRequestResult::NoReadyLine;
}
//--------------------------------------------------------------------------//
TradingLineManagerEv::~TradingLineManagerEv() {
}
void TradingLineManagerEv::OnNewTradingLineReady(TradingLine* src, const TLineLocker& tsvrFrom) {
   (void)src; (void)tsvrFrom;
}
void TradingLineManagerEv::OnHelperReady(const TLineLocker& tsvrFrom) {
   (void)tsvrFrom;
}
void TradingLineManagerEv::OnTradingLineBroken(TradingLine& src, const TLineLocker& tsvrFrom) {
   (void)src; (void)tsvrFrom;
}
void TradingLineManagerEv::OnHelperBroken(const TLineLocker& tsvrFrom) {
   (void)tsvrFrom;
}
void TradingLineManagerEv::OnSendReqQueueEmpty(const TLineLocker& tsvrFrom) {
   (void)tsvrFrom;
}
void TradingLineManagerEv::OnTradingLineManagerDetach(const TLineLocker& tsvrFrom) {
   (void)tsvrFrom;
}
//--------------------------------------------------------------------------//
void TradingLineManager::TradingSvrImpl::GetStInfo(RevBuffer& rbuf) const {
   if (!this->Helper_)
      RevPrint(rbuf, "|NoHelper");
   else {
      this->Helper_->GetStInfo(rbuf);
      RevPrint(rbuf, "|Helper=");
   }
   RevPrint(rbuf, "ReadyLines=", this->CountReadyLines());
}
void TradingLineManager::TradingSvrImpl::SendReqQueue(void* tsvr) {
   if (this->ReqQueue_.empty())
      return;
   TradingLineManager& owner = this->GetOwner();
   SendRequestResult   res;
   do {
      res = owner.SendRequest_ByLinesOrHelper(*this->ReqQueue_.front(), *static_cast<Locker*>(tsvr));
      switch (res) {
      case SendRequestResult::Queuing:
         return;
      default:
      case SendRequestResult::RejectRequest:
      case SendRequestResult::Sent:
      case SendRequestResult::NoReadyLine:
      case SendRequestResult::RequestEnd:
         this->ReqQueue_.pop_front();
         break;
      }
   } while (!this->ReqQueue_.empty());
   if (res != SendRequestResult::NoReadyLine) {
      if (this->EvHandler_)
         this->EvHandler_->OnSendReqQueueEmpty(*static_cast<Locker*>(tsvr));
   }
}
void TradingLineManager::TradingSvrImpl::SendReqQueueByOffer(void* tsvrSelf, FnHelpOffer&& fnOffer) {
   if (this->ReqQueue_.empty())
      return;
   do {
      switch (fnOffer(*this->ReqQueue_.front(), tsvrSelf)) {
      case SendRequestResult::Queuing: // offer => Busy; 可能原因: offer 流量管制, offer 繞進滿載.
      case SendRequestResult::NoReadyLine:
         return;
      default:
      case SendRequestResult::RejectRequest:
      case SendRequestResult::Sent:
      case SendRequestResult::RequestEnd:
         this->ReqQueue_.pop_front();
         break;
      }
   } while (!this->ReqQueue_.empty());
   if (this->EvHandler_)
      this->EvHandler_->OnSendReqQueueEmpty(*static_cast<Locker*>(tsvrSelf));
}
//--------------------------------------------------------------------------//
TradingLineManager::~TradingLineManager() {
   this->OnBeforeDestroy();
}
void TradingLineManager::OnBeforeDestroy() {
   this->FlowControlTimer_.DisposeAndWait();
   TradingSvr::Locker  tsvr{this->TradingSvr_};
   if (tsvr->EvHandler_)
      tsvr->EvHandler_->OnTradingLineManagerDetach(tsvr);
   this->ClearReqQueue(std::move(tsvr), "TradingLineManager.dtor");
}
void TradingLineManager::OnTradingLineBroken(TradingLine& src) {
   TradingSvr::Locker tsvr{this->TradingSvr_};
   auto ibeg = tsvr->Lines_.begin();
   auto ifind = std::find(tsvr->Lines_.begin(), tsvr->Lines_.end(), &src);
   if (ifind == tsvr->Lines_.end())
      return;
   if (tsvr->LastIndex_ > static_cast<unsigned>(ifind - ibeg))
      --tsvr->LastIndex_;
   tsvr->Lines_.erase(ifind);
   this->OnTradingLineBroken(src, std::move(tsvr));
}
void TradingLineManager::OnTradingLineBroken(TradingLine& src, Locker&& tsvr) {
   assert(&tsvr->GetOwner() == this);
   if (tsvr->EvHandler_)
      tsvr->EvHandler_->OnTradingLineBroken(src, tsvr);
   if (!tsvr->IsSendable())
      this->ClearReqQueue(std::move(tsvr), "No ready line.");
}
void TradingLineManager::OnHelperBroken(Locker&& tsvr) {
   assert(&tsvr->GetOwner() == this);
   if (tsvr->EvHandler_)
      tsvr->EvHandler_->OnHelperBroken(tsvr);
   if (!tsvr->IsSendable())
      this->ClearReqQueue(std::move(tsvr), "No helper.");
}
void TradingLineManager::ClearReqQueue(Locker&& tsvr, StrView cause) {
   assert(&tsvr->GetOwner() == this);
   Reqs reqs = std::move(tsvr->ReqQueue_);
   tsvr.unlock();
   for (const TradingRequestSP& r : reqs)
      this->NoReadyLineReject(*r, cause);
}
SendRequestResult TradingLineManager::RequestPushToQueue(TradingRequest& req, const Locker& tsvr) {
   assert(&tsvr->GetOwner() == this);
   tsvr->ReqQueue_.push_back(&req);
   return SendRequestResult::Queuing;
}
//--------------------------------------------------------------------------//
void TradingLineManager::OnTradingLineReady(TradingLine& src) {
   TradingSvr::Locker tsvr{this->TradingSvr_};
   auto ifind = std::find(tsvr->Lines_.begin(), tsvr->Lines_.end(), &src);
   if (ifind == tsvr->Lines_.end()) {
      tsvr->LastIndex_ = static_cast<unsigned>(tsvr->Lines_.size());
      tsvr->Lines_.push_back(&src);
   }
   else {
      tsvr->LastIndex_ = static_cast<unsigned>(ifind - tsvr->Lines_.begin());
   }
   this->OnNewTradingLineReady(&src, std::move(tsvr));
}
void TradingLineManager::FlowControlTimer::EmitOnTimer(TimeStamp now) {
   (void)now;
   TradingLineManager&  rmgr = ContainerOf(*this, &TradingLineManager::FlowControlTimer_);
   TradingSvr::Locker   tsvr{rmgr.TradingSvr_};
   if (!tsvr->IsLinesEmpty())
      rmgr.OnNewTradingLineReady(nullptr, std::move(tsvr));
}
void TradingLineManager::SendReqQueue(Locker&& tsvr) {
   assert(&tsvr->GetOwner() == this);
   tsvr->SendReqQueue(&tsvr);
}
void TradingLineManager::OnNewTradingLineReady(TradingLine* src, Locker&& tsvr) {
   assert(&tsvr->GetOwner() == this);
   this->SendReqQueue(std::move(tsvr));
   tsvr.Relock(this->TradingSvr_);
   if (tsvr->EvHandler_)
      tsvr->EvHandler_->OnNewTradingLineReady(src, tsvr);
}
void TradingLineManager::OnHelperReady(Locker&& tsvr) {
   assert(&tsvr->GetOwner() == this);
   this->SendReqQueue(std::move(tsvr));
   tsvr.Relock(this->TradingSvr_);
   if (tsvr->EvHandler_)
      tsvr->EvHandler_->OnHelperReady(tsvr);
}
void TradingLineManager::UpdateHelper(TradingLineHelperSP helper) {
   Locker tsvr{this->TradingSvr_};
   tsvr->Helper_ = std::move(helper);
   if (tsvr->Helper_ && tsvr->Helper_->IsHelperReady())
      this->OnHelperReady(std::move(tsvr));
   else
      this->OnHelperBroken(std::move(tsvr));
}
void TradingLineManager::SetEvHandler(EventSP evHandler) {
   Locker tsvr{this->TradingSvr_};
   if (tsvr->EvHandler_ == evHandler)
      return;
   if (tsvr->EvHandler_)
      tsvr->EvHandler_->OnTradingLineManagerDetach(tsvr);
   tsvr->EvHandler_ = std::move(evHandler);
}   
//--------------------------------------------------------------------------//
SendRequestResult TradingLineManager::CheckOpQueuingRequest(Reqs& reqQueue, TradingRequest& req) {
   // 因為 req 已經準備要進排隊了, 所以這裡沒有速度的要求, 就整個 queue 都掃一遍吧!
   if (!req.PreOpQueuingRequest(*this))
      return SendRequestResult::Queuing;
   const auto iend = reqQueue.end();
   auto i = reqQueue.begin();
   while (i != iend) {
      switch (req.OpQueuingRequest(*this, **i)) {
      default:
      case TradingRequest::Op_NotSupported:                       return SendRequestResult::Queuing;
      case TradingRequest::Op_ThisDone:                           return SendRequestResult::RequestEnd;
      case TradingRequest::Op_AllDone:       reqQueue.erase(i);   return SendRequestResult::RequestEnd;
      case TradingRequest::Op_TargetDone:    reqQueue.erase(i);   return SendRequestResult::Queuing;
      case TradingRequest::Op_NotTarget:                          break; // check next queue item;
      }
      ++i;
   }
   return SendRequestResult::Queuing;
}
void TradingLineManager::SelectPreferNextLine(const TradingRequest& req, const Locker& tsvr) {
   assert(&tsvr->GetOwner() == this);
   if (const size_t lineCount = tsvr->Lines_.size()) {
      for (size_t L = 0; L < lineCount; ++L) {
         unsigned nextIndex = static_cast<unsigned>((tsvr->LastIndex_ + 1) % lineCount);
         if (req.IsPreferTradingLine(*tsvr->Lines_[nextIndex]))
            break;
         tsvr->LastIndex_ = nextIndex;
      }
   }
}
SendRequestResult TradingLineManager::OnAskFor_SendRequest_FromLocal(TradingRequest& req, const Locker& tsvrSelf, const Locker& tsvrAsker) {
   (void)tsvrAsker;
   assert(&tsvrSelf->GetOwner() == this);
   return this->SendRequest_ByLines_NoQueue(req, tsvrSelf);
}
SendRequestResult TradingLineManager::SendRequest_ByLinesOnly(TradingRequest& req, TradingLines& tsvr) {
   size_t lineCount = tsvr.Lines_.size();
   if (lineCount == 0)
      return SendRequestResult::NoReadyLine;
   using LineSendResult = TradingLine::SendResult;
   LineSendResult resFlowControl{LineSendResult::Busy};
   bool           hasBusyLine = false;

   // 若有多條線路, 目前使用輪流(均分)的方式送出要求.
   // - 但是此法是最快的嗎? => 不一定.
   // - 現代 CPU 有大量的 cache, 盡量固定使用某一條線路, 可讓 CPU cache 發揮最大功效.
   // - 但是某條線路塞到滿之後再換下一條, 也有TCP流量問題要考慮.
   // - 多線路時怎樣使用才是最好? 還要進一步研究.
   // => 經過 f9twsqd 在客戶端的實際驗證, 瞬間多筆時, 總是使用同一條線路, 延遲最低。
   //    所以系統增加一個選項, 由使用者決定使用哪種方法.
   // => 但須要考慮 TCP 的流量機制(需考慮對方 TCP 的 Window size, 中繼設備(Router/Switch...)則可能會被改變此值).
   //    所以使用 [TradingLine.強制換線筆數] 機制;
   TimeStamp utcNow;
   if (gTradingLineSelect_TryLastLine_YN == EnabledYN::Yes) {
      utcNow = UtcNow();
      if (tsvr.LastIndex_ < lineCount) {
         const auto* lastLine = tsvr.Lines_[tsvr.LastIndex_];
         if ((utcNow - tsvr.LastLineSentBeginTime_) > lastLine->LineFlowInterval() || lastLine->LineFlowInterval().IsZero()) {
            // 超過[開始計數時間]: 清除計數器.
            tsvr.LastLineSentCount_ = 0;
         }
         else if (tsvr.LastLineSentCount_ >= lastLine->LineFlowCount()) {
            // 超過[強制換線筆數]: 換條線路;
            tsvr.LastLineSentCount_ = 0;
            ++tsvr.LastIndex_;
         }
      }
   }
   else {
      ++tsvr.LastIndex_;
   }

   for (size_t L = 0; L < lineCount; ++L) {
   __RETRY_SAME_INDEX:
      if (tsvr.LastIndex_ >= lineCount)
         tsvr.LastIndex_ = 0;
      LineSendResult resSend = tsvr.Lines_[tsvr.LastIndex_]->SendRequest(req);
      if (fon9_LIKELY(resSend == LineSendResult::Sent)) {
         if (gTradingLineSelect_TryLastLine_YN == EnabledYN::Yes) {
            if (fon9_UNLIKELY(++tsvr.LastLineSentCount_ == 1))
               tsvr.LastLineSentBeginTime_ = utcNow;
         }
         return SendRequestResult::Sent;
      }

      if (fon9_LIKELY(resSend >= LineSendResult::FlowControl)) {
         if (resFlowControl < LineSendResult::FlowControl || resSend < resFlowControl)
            resFlowControl = resSend;
      }
      else if (fon9_LIKELY(resSend == LineSendResult::RejectRequest))
         return SendRequestResult::RejectRequest;
      else if (fon9_LIKELY(resSend == LineSendResult::NotSupport)) {
         // 等迴圈結束, 再來判斷是否需要 Queue.
      }
      else if (fon9_LIKELY(resSend == LineSendResult::Busy))
         hasBusyLine = true;
      else {
         assert(resSend == LineSendResult::Broken);
         tsvr.Lines_.erase(tsvr.Lines_.begin() + tsvr.LastIndex_);
         if (L < --lineCount)
            goto __RETRY_SAME_INDEX;
         if (lineCount == 0)
            return SendRequestResult::NoReadyLine;
         break;
      }
      // FlowControl or Busy: continue check the next line.
      ++tsvr.LastIndex_;
   } // for(L < lineCount)
   if (resFlowControl >= LineSendResult::FlowControl)
      this->FlowControlTimer_.RunAfter(ToFlowControlInterval(resFlowControl));
   else if (!hasBusyLine) { // All not support.
      return SendRequestResult::NoReadyLine;
   }
   return SendRequestResult::Queuing;
}
SendRequestResult TradingLineManager::SendRequest_ByLinesOrHelper(TradingRequest& req, const Locker& tsvr) {
   assert(&tsvr->GetOwner() == this);
   switch (auto retval = this->SendRequest_ByLinesOnly(req, *tsvr)) {
   default:
   case SendRequestResult::RejectRequest:
   case SendRequestResult::Sent:
   case SendRequestResult::RequestEnd:
      return retval;
   case SendRequestResult::Queuing:
      if (tsvr->Helper_) {
         retval = tsvr->Helper_->OnAskFor_BeforeQueue(req, tsvr);
         if (retval == SendRequestResult::NoReadyLine)
            return SendRequestResult::Queuing;
      }
      // 這裡不可將 req 放入 tsvr->ReqQueue_; 也不可呼叫 this->RequestPushToQueue();
      // 因為可能是從 this->SendReqQueue() 來到此處: 嘗試傳送 tsvr->ReqQueue_.front();
      return retval;
   case SendRequestResult::NoReadyLine:
      if (tsvr->Helper_) {
         retval = tsvr->Helper_->OnAskFor_NoReadyLine(req, tsvr);
         if (fon9_LIKELY(retval != SendRequestResult::NoReadyLine))
            return retval;
      }
      return this->NoReadyLineReject(req, "No ready line.");
   }
}
SendRequestResult TradingLineManager::NoReadyLineReject(TradingRequest& req, StrView cause) {
   (void)req; (void)cause;
   return SendRequestResult::NoReadyLine;
}
//--------------------------------------------------------------------------//
TradingLineLogPathMaker::~TradingLineLogPathMaker() {
}
TimeStamp TradingLineLogPathMaker::GetTDay() {
   return UtcNow() + GetLocalTimeZoneOffset();
}
std::string TradingLineLogPathMaker::MakeLogPath(std::string& res, TimeStamp* outTDay) {
   TimeStamp tday = this->GetTDay();
   if (outTDay)
      *outTDay = tday;
   if (tday.IsNullOrZero())
      return "|err=Unknown TDay.";
   TimedFileName  logPath(this->LogPathFmt_, TimedFileName::TimeScale::Day);
   // log 檔名與 TDay 相關, 與 TimeZone 無關, 所以要排除 TimeZone;
   logPath.RebuildFileNameExcludeTimeZone(tday);
   res = FilePath::AppendPathTail(&logPath.GetFileName());
   return std::string{};
}

} } // namespaces
