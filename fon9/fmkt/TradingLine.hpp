/// \file fon9/fmkt/TradingLine.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_fmkt_TradingLine_hpp__
#define __fon9_fmkt_TradingLine_hpp__
#include "fon9/fmkt/TradingRequest.hpp"
#include "fon9/Timer.hpp"
#include "fon9/ConfigUtils.hpp"
#include <deque>

namespace fon9 { namespace fmkt {

/// 預設為 No; 當設為 EnabledYN::Yes, 則盡量使用同一條線路.
extern fon9_API EnabledYN   gTradingLineSelect_TryLastLine_YN;

/// \ingroup fmkt
/// 交易連線基底.
class fon9_API TradingLine {
   fon9_NON_COPY_NON_MOVE(TradingLine);

protected:
   /// 若有設定 gTradingLineSelect_TryLastLine_YN, 則在此線路連續傳送後, 若超過此時間間隔, 則重設傳送計數器.
   /// 用於避免 TCP 流量(SND buffer 滿載) 造成的延遲;
   /// 若為 LineFlowInterval_.IsZero(); 則不考慮此流量參數;
   TimeInterval   LineFlowInterval_;
   /// 若有設定 gTradingLineSelect_TryLastLine_YN, 則在此線路連續傳送 N 筆後, 強制切換線路.
   /// 若為 0 or 1, 則只送 1 筆, 就切換線路.
   unsigned       LineFlowCount_{0};
   char           Padding____[4];

public:
   /// 瞬間傳送超過 lineFlowCount 筆, 則應切換線路;
   /// [瞬間]的計算規則: 送出第1筆的時間點, 到現在的時間差. 不會針對[每一筆]考慮時間.
   TradingLine(unsigned lineFlowCount, TimeInterval lineFlowInterval)
      : LineFlowInterval_{lineFlowInterval}
      , LineFlowCount_{lineFlowCount} {
   }
   template <class FlowCounterArgs>
   TradingLine(const FlowCounterArgs& fc) : TradingLine(fc.FcCount_, TimeInterval_Millisecond(fc.FcTimeMS_)) {
   }

   virtual ~TradingLine();

   unsigned LineFlowCount() const {
      return this->LineFlowCount_;
   }
   TimeInterval LineFlowInterval() const {
      return this->LineFlowInterval_;
   }

   enum class SendResult {
      Sent = 0,
      /// >= SendRequestResult::FlowControl 表示流量管制.
      /// 可透過 ToFlowControlInterval(res); 取得: 還要等候多久才解除管制.
      FlowControl = 1,

      /// 線路忙碌中, 例如: 每次只能送1筆, 必須等收到結果後才能送下一筆.
      Busy = -1,
      /// 線路已斷線無法再送單.
      Broken = -2,
      /// 拒絕送出 Request: 通常是 Request 的內容有誤.
      RejectRequest = -3,
      /// 例如: 零股線路, 不支援定價交易.
      NotSupport = -4,
   };
   /// 設計衍生者請注意:
   /// 透過 TradingLineManager 來的下單要求, 必定已經鎖住「可用線路表」,
   /// 因此不可再呼叫 TradingLineManager 的相關函式, 會造成死結!
   virtual SendResult SendRequest(TradingRequest& req) = 0;

   /// 判斷 this 是否為 req 的原始傳送者.
   /// 預設傳回 false;
   virtual bool IsOrigSender(const TradingRequest& req) const;
};
inline TimeInterval ToFlowControlInterval(TradingLine::SendResult r) {
   assert(r >= TradingLine::SendResult::FlowControl);
   return TimeInterval::Make<6>(static_cast<TimeInterval::OrigType>(r));
}
inline TradingLine::SendResult ToFlowControlResult(TimeInterval ti) {
   assert(static_cast<TradingLine::SendResult>(ti.GetOrigValue()) >= TradingLine::SendResult::FlowControl);
   return static_cast<TradingLine::SendResult>(ti.GetOrigValue());
}

/// \ingroup fmkt
/// 下單要求傳送的結果.
enum class SendRequestResult : int {
   /// 返回此訊息前, 必定已經呼叫過 TradingLineManager::NoReadyLineReject();
   NoReadyLine = -2,
   /// 拒絕送出 Request: 通常是 Request 的內容有誤.
   RejectRequest = -3,

   Sent = 0,
   Queuing = 1,
   /// 下單要求完成, 既沒送出, 也沒排隊, 例: 內部刪單.
   RequestEnd = 2,
};

class fon9_API TradingLineHelper;
using TradingLineHelperSP = intrusive_ptr<TradingLineHelper>;

class fon9_API TradingLineManagerEv;
using TradingLineManagerEvSP = intrusive_ptr<TradingLineManagerEv>;

/// \ingroup fmkt
/// 交易連線管理員基底.
/// - 負責尋找可下單的線路送出下單要求.
/// - 負責處理流量管制:
///   - 有線路但暫時無法送單(例: FIX流量管制, TMP送單中), 將下單要求放到 Queue.
///   - 等候流量管制時間, 時間到解除管制時, 透過 OnNewTradingLineReady() 通知.
class fon9_API TradingLineManager {
   fon9_NON_COPY_NON_MOVE(TradingLineManager);

public:
   using Reqs = std::deque<TradingRequestSP>;
   using HelperSP = TradingLineHelperSP;
   using EventSP = TradingLineManagerEvSP;
   using FnHelpOffer = std::function<SendRequestResult(TradingRequest& req, void* tsvrAsker)>;
   /// 可送出下單要求的連線. 流量管制時不移除.
   class TradingLines {
      fon9_NON_COPY_NON_MOVE(TradingLines);
      TradingLines() = default;
      friend class TradingLineManager;
      using Lines = std::vector<TradingLine*>;
      Lines       Lines_;
      unsigned    LastIndex_{0};
      unsigned    LastLineSentCount_{0};
      TimeStamp   LastLineSentBeginTime_;
   public:
      bool IsLinesEmpty() const {
         return this->Lines_.empty();
      }
      size_t CountReadyLines() const {
         return this->Lines_.size();
      }
   };
   class TradingSender : public TradingLines {
      fon9_NON_COPY_NON_MOVE(TradingSender);
      TradingSender() = default;
      friend class TradingLineManager;
      HelperSP Helper_;
   };
   class fon9_API TradingSvrImpl : public TradingSender {
      fon9_NON_COPY_NON_MOVE(TradingSvrImpl);
      friend class TradingLineManager;
      Reqs     ReqQueue_;
      EventSP  EvHandler_;
   public:
      TradingSvrImpl() = default;
      TradingLineManager& GetOwner() {
         return ContainerOf(TradingSvr::StaticCast(*this), &TradingLineManager::TradingSvr_);
      }
      const TradingRequest* ReqQueueFront() {
         return this->ReqQueue_.empty() ? nullptr : this->ReqQueue_.front().get();
      }
      bool IsReqQueueEmpty() const {
         return this->ReqQueue_.empty();
      }
      void ReqQueueMoveTo(Reqs& out) {
         out = std::move(this->ReqQueue_);
      }
      bool IsHelperReady() const;
      bool IsSendable() const;
      void GetStInfo(RevBuffer& rbuf) const;

      /// MustLock<TradingSvrImpl>::Locker* tsvr;
      /// 若有需要會觸發 this->EvHandler_->OnSendReqQueueEmpty() 事件.
      void SendReqQueue(void* tsvr);
      /// tsvrSelf = this->Owner().Lock() = Asker;
      /// 若有需要會觸發 this->EvHandler_->OnSendReqQueueEmpty() 事件.
      void SendReqQueueByOffer(void* tsvrSelf, FnHelpOffer&& fnOffer);
   };
   using TradingSvr = MustLock<TradingSvrImpl>;

protected:
   TradingSvr  TradingSvr_;

public:
   using Locker = TradingSvr::Locker;
   using QueueReqs = Reqs;

   TradingLineManager() = default;
   virtual ~TradingLineManager();

   Locker Lock() {
      return this->TradingSvr_.Lock();
   }

   bool IsNoReadyLine() const {
      return this->TradingSvr_.ConstLock()->IsLinesEmpty();
   }
   bool IsSendable() const {
      return this->TradingSvr_.ConstLock()->IsSendable();
   }

   /// 當 src 進入可下單狀態時的通知:
   /// - 連線成功後, 進入可下單狀態.
   /// - 從忙碌狀態, 進入可下單狀態.
   void OnTradingLineReady(TradingLine& src);

   /// 當 src 斷線時的通知.
   /// 不包含: 流量管制, 線路忙碌.
   void OnTradingLineBroken(TradingLine& src);

   SendRequestResult SendRequest(TradingRequest& req) {
      return this->SendRequest(req, Locker{this->TradingSvr_});
   }
   SendRequestResult SendRequest(TradingRequest& req, const Locker& tsvr) {
      SendRequestResult res;
      if (fon9_LIKELY(tsvr->ReqQueue_.empty())) {
         res = this->SendRequest_ByLinesOrHelper(req, tsvr);
      }
      else {
         res = this->CheckOpQueuingRequest(tsvr->ReqQueue_, req);
      }
      if (fon9_LIKELY(res != SendRequestResult::Queuing))
         return res;
      return this->RequestPushToQueue(req, tsvr);
   }

   /// 透過 req.IsPreferTradingLine(); 來選擇下一個建議的交易線路.
   /// 但不一定會用該線路, 例: 如果該線路忙碌(流量管制), 則會使用其他線路.
   void SelectPreferNextLine(const TradingRequest& req, const Locker& tsvr);

   /// 嘗試送出 Queue 裡面的下單要求.
   /// 預設: tsvr->SendReqQueue(&tsvr);
   virtual void SendReqQueue(Locker&& tsvr);

   /// 協助轉送下單要求.
   /// tsvrSelf = this->Lock();
   /// 預設: this->SendRequest_ByLines_NoQueue();
   virtual SendRequestResult OnAskFor_SendRequest_FromLocal(TradingRequest& req, const Locker& tsvrSelf, const Locker& tsvrAsker);

   /// - 若 tsvr 已有排隊, 則不會送出, 也不會加入 tsvr->ReqQueue_, 會直接返回 SendRequestResult::Queuing;
   /// - 若可以使用 tsvr->Lines_ 立即送出(有線路,且流量允許), 則會將 req 送出.
   /// - 若沒送出, 則 req 不會有任何變動, 也不會呼叫 Helper.
   SendRequestResult SendRequest_ByLines_NoQueue(TradingRequest& req, const Locker& tsvr) {
      if (fon9_LIKELY(tsvr->ReqQueue_.empty()))
         return this->SendRequest_ByLinesOnly(req, *tsvr);
      return SendRequestResult::Queuing;
   }

   /// 設定 ev handler, 設定時不會觸發事件.
   void SetEvHandler(EventSP evHandler);
   /// 設定 helper, 或 helper 狀態改變時呼叫;
   /// - 若 helper!=nullptr 且 helper->IsHelperReady():
   ///   - 觸發 OnHelperReady() 事件:
   ///     若現在有排隊中的委託, 則會立即呼叫 helper->OnAskFor_BeforeQueue(); 請求協助.
   /// - 否則(helper==nullptr 或 !helper->IsHelperReady()):
   ///   - 觸發 OnHelperBroken() 事件:
   ///     若現在沒有可用線路, 則會拒絕正在排隊中的委託.
   void UpdateHelper(TradingLineHelperSP helper);
   void UpdateHelperReady();
   void UpdateHelperBroken();

protected:
   /// 衍生者在解構時, 應先呼叫此處,
   /// 如此在 Queue 之中的 req 才會通過 this->NoReadyLineReject(req) 通知衍生者.
   virtual void OnBeforeDestroy();

   /// 清除全部的「排隊中下單要求」.
   virtual void ClearReqQueue(Locker&& tsvr, StrView cause);

   /// - 無可用線路時的拒絕.
   /// - TradingLineManager 解構時, 拒絕還在 Queue 裡面的要求.
   /// - 如果是從 ClearReqQueue() 來到此處, 則會先解鎖.
   /// - 如果是從 SendRequest_ByLinesOrHelper() 來到此處, 則仍在鎖定狀態.
   /// - 預設 do nothing, 直接返回 SendRequestResult::NoReadyLine;
   virtual SendRequestResult NoReadyLineReject(TradingRequest& req, StrView cause);

   /// 當有新的可交易線路時的通知.
   /// - 預設:
   ///   1. this->SendReqQueue(tsvr); 送出 queue 的 req.
   ///   2. 觸發事件 tsvr->EvHandler_->OnNewTradingLineReady();
   /// - src == nullptr 表示: 從計時器來的: 流量管制解除.
   /// - 如果是從 OnTradingLineReady() 來的, 則 tsvr->Lines_[tsvr->LastIndex_] == src.
   virtual void OnNewTradingLineReady(TradingLine* src, Locker&& tsvr);
   virtual void OnHelperReady(Locker&& tsvr);
   /// 當 src 斷線時的通知.
   /// 預設:
   /// 1. 觸發事件 tsvr->EvHandler_->OnTradingLineBroken();
   /// 2. 若 if (!tsvr->IsSendable()) 則: 呼叫 this->ClearReqQueue();
   virtual void OnTradingLineBroken(TradingLine& src, Locker&& tsvr);
   /// tsvr->Helper_ 無法提供協助時通知, 但必須由主動呼叫 this->UpdateHelper() 時才會觸發此事件.
   /// 如果 !helper.IsHelperReady(); 但沒呼叫 this->UpdateHelper(); 則不會觸發此事件.
   /// 預設:
   /// 1. 觸發事件 tsvr->EvHandler_->OnHelperBroken();
   /// 2. 若 if (!tsvr->IsSendable()) 則: 呼叫 this->ClearReqQueue();
   virtual void OnHelperBroken(Locker&& tsvr);

   /// 預設: tsvr->ReqQueue_.push_back(&req); 返回 SendRequestResult::Queuing;
   virtual SendRequestResult RequestPushToQueue(TradingRequest& req, const Locker& tsvr);

   /// 檢查 req 要操作的標的是否在 reqQueue 裡面?
   /// 如果是, 也許可以內部[刪改查], 不用送出.
   /// 當 reqQueue.empty() 時, 不會來到此處.
   /// 預設直接返回 SendRequestResult::Queuing:
   /// 表示 req 為一般要求, 呼叫端必定會將 req 放入 reqQueue.
   SendRequestResult CheckOpQueuingRequest(Reqs& reqQueue, TradingRequest& req);

   /// 使用交易線路送出 req, 或是, 當 [無線路 or 流量管制] 時, 不做任何事情.
   /// - 若需流量管制, 則會啟動計時器, 不論: req 接下來是否透過 Helper 處理, 或是否有放到 Queue.
   /// - 可用 retval 判斷是否有送出, 或未送出原因.
   /// - 這裡不會將 req 放到 tsvr->ReqQueue_;
   /// - 這裡不會呼叫 this->NoReadyLineReject();
   SendRequestResult SendRequest_ByLinesOnly(TradingRequest& req, TradingLines& tsvr);

private:
   /// 用 Lines 或 Helper 送出 req, 若無法送出, 則透過 retval 告知原因, 不會放入 Queue;
   /// - 透過 this->SendRequest_ByLinesOnly() 送出 req;
   /// - 若有可用線路, 但暫時無法送出(需要 Queuing) 則:
   ///   - 若有 Helper 則直接返回 Helper->OnAskFor_BeforeQueue();
   ///   - 若無 Helper 則返回 SendRequestResult::Queuing;
   ///     不會放入 tsvr->ReqQueue_; 也不會呼叫 this->RequestPushToQueue();
   /// - 若無可用線路(NoReadyLine), 無法送出:
   ///   - 若有 Helper 則透過 Helper->OnAskFor_NoReadyLine() 處理.
   ///     - 即使 Helper->OnAskFor_NoReadyLine() 返回 Queuing, 也不會再呼叫 Helper->OnAskFor_BeforeQueue();
   ///   - 或 this->NoReadyLineReject();
   SendRequestResult SendRequest_ByLinesOrHelper(TradingRequest& req, const Locker& tsvr);

   struct FlowControlTimer : public DataMemberTimer {
      fon9_NON_COPY_NON_MOVE(FlowControlTimer);
      FlowControlTimer() = default;
      virtual void EmitOnTimer(TimeStamp now) override;
   };
   FlowControlTimer FlowControlTimer_;
};

class fon9_API TradingLineLogPathMaker {
   fon9_NON_COPY_NON_MOVE(TradingLineLogPathMaker);

public:
   const std::string LogPathFmt_;

   TradingLineLogPathMaker(std::string logPathFmt)
      : LogPathFmt_{std::move(logPathFmt)} {
   }
   virtual ~TradingLineLogPathMaker();

   /// 預設 = 本地時間 = UtcNow() + GetLocalTimeZoneOffset();
   virtual TimeStamp GetTDay();

   /// - *outTDay = this->GetTDay();
   /// - 若 this->GetTDay() 無效時, 傳回失敗訊息(例如: "|err=Unknown TDay.");
   /// - retval.empty() 表示結果存放在 res; 返回時 res 尾端會加上 '/';
   std::string MakeLogPath(std::string& res, TimeStamp* outTDay = nullptr);
};

/// \ingroup fmkt
/// 當 TradingLineManager 無法送出要求時的協助者.
/// 當 [沒有交易連線(全斷)] or [流量管制] 時, 透過 TradingLineHelper 取得協助.
class fon9_API TradingLineHelper : public intrusive_ref_counter<TradingLineHelper> {
   fon9_NON_COPY_NON_MOVE(TradingLineHelper);
   char  Padding____[4];
public:
   TradingLineHelper() = default;
   virtual ~TradingLineHelper();

   using TLineLocker = TradingLineManager::Locker;
   /// 在 from.SendRequest_ByLinesOrHelper() 有可用線路, 但因流量無法立即送出時, 請求 Helper 協助傳送.
   /// - 返回 SendRequestResult::Queuing(預設) 或 SendRequestResult::NoReadyLine; 表示:
   ///   接下來呼叫端(from)會放入 tsvr->ReqQueue_; (或繼續放在 tsvr->ReqQueue_); 等候傳送.
   /// - 由於返回後可能會需要將 req 放入 queue, 所以返回前不允許 tsvr.unlock();
   virtual SendRequestResult OnAskFor_BeforeQueue(TradingRequest& req, const TLineLocker& tsvrAsker);
   /// 在 from.SendRequest_ByLinesOrHelper() 發現無可用線路, 請求 Helper 協助傳送.
   /// - 預設返回 SendRequestResult::NoReadyLine; 表示:
   ///   呼叫端(from)執行: from.NoReadyLineReject(req, "No ready line.");
   /// - 返回 SendRequestResult::Queuing 表示:
   ///   接下來呼叫端(from)會放入 tsvr->ReqQueue_; (或繼續放在 tsvr->ReqQueue_); 等候傳送.
   /// - 由於返回後可能會需要將 req 放入 queue, 所以返回前不允許 tsvr.unlock();
   virtual SendRequestResult OnAskFor_NoReadyLine(TradingRequest& req, const TLineLocker& tsvrAsker);
   /// 是否可以提供協助傳送.
   /// - 若因流量管制暫時不能送, 仍應視為 [可協助];
   virtual bool IsHelperReady() const = 0;
   /// 取得狀態說明文字.
   virtual void GetStInfo(RevBuffer& rbuf) const = 0;
};
inline bool TradingLineManager::TradingSvrImpl::IsHelperReady() const {
   return this->Helper_ ? this->Helper_->IsHelperReady() : false;
}
inline bool TradingLineManager::TradingSvrImpl::IsSendable() const {
   return !this->IsLinesEmpty() || this->IsHelperReady();
}

/// \ingroup fmkt
/// 當 TradingLineManager 的事件通知.
/// 當 [沒有交易連線(全斷)] or [流量管制] 時, 透過 TradingLineHelper 取得協助.
class fon9_API TradingLineManagerEv : public intrusive_ref_counter<TradingLineManagerEv> {
   fon9_NON_COPY_NON_MOVE(TradingLineManagerEv);
   char  Padding____[4];
public:
   TradingLineManagerEv() = default;
   virtual ~TradingLineManagerEv();

   using TLineLocker = TradingLineManager::Locker;
   /// 當 src 線路可用時通知.
   /// - 若 src==nullptr 表示: 解除流量管制時的通知.
   /// - 在 from.OnNewTradingLineReady() 之中通知.
   /// - tsvr.ReqQueue_ 不一定為空.
   /// - 每條線路上線後都會有通知.
   virtual void OnNewTradingLineReady(TradingLine* src, const TLineLocker& tsvrFrom);
   virtual void OnHelperReady(const TLineLocker& tsvrFrom);
   /// 當 src 斷線時的通知.
   /// - 每條可用線路斷線後都會有通知.
   /// - 通知時 src 已從 tsvr->Lines_ 移除;
   virtual void OnTradingLineBroken(TradingLine& src, const TLineLocker& tsvrFrom);
   /// 當 tsvr->Helper_ 無法提供協助時通知.
   virtual void OnHelperBroken(const TLineLocker& tsvrFrom);
   /// 傳送隊列送完後通知.
   /// 在 from.SendReqQueue();
   /// 當 !tsvr->ReqQueue_.empty() 有排隊中的要求,
   /// 經由 from.SendRequest_ByLinesOrHelper(); 處理後,
   /// 變成 tsvr->ReqQueue_.empty(); 如果不是因為 NoReadyLine,
   /// 則會觸發此事件.
   /// 若原本就沒有 req 在排隊, 則不會觸發此事件.
   virtual void OnSendReqQueueEmpty(const TLineLocker& tsvrFrom);
   /// TradingLineManager 解構, 或設定新的 EvHandler 時通知.
   virtual void OnTradingLineManagerDetach(const TLineLocker& tsvrFrom);
};
inline void TradingLineManager::UpdateHelperReady() {
   Locker tsvr{this->TradingSvr_};
   assert(tsvr->Helper_ && tsvr->Helper_->IsHelperReady());
   this->OnHelperReady(std::move(tsvr));
}
inline void TradingLineManager::UpdateHelperBroken() {
   Locker tsvr{this->TradingSvr_};
   assert(!tsvr->Helper_ || !tsvr->Helper_->IsHelperReady());
   this->OnHelperBroken(std::move(tsvr));
}

} } // namespaces
#endif//__fon9_fmkt_TradingLine_hpp__
