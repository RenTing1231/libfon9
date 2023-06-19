// \file fon9/MessageQueue.hpp
// \author fonwinz@gmail.com
#ifndef __fon9_MessageQueue_hpp__
#define __fon9_MessageQueue_hpp__
#include "fon9/ThreadController.hpp"
#include "fon9/ThreadTools.hpp"
#include "fon9/Log.hpp"
fon9_BEFORE_INCLUDE_STD;
#include <deque>
#include <vector>
fon9_AFTER_INCLUDE_STD;

namespace fon9 {

/// \ingroup Thrs
/// - 每個 thread 會建立一個 MessageHandlerT 用來處理訊息.
/// - MessageHandlerT 必須提供:
///   - `typename MessageHandlerT::MessageType;`
///   - `MessageHandlerT::MessageHandlerT(MessageQueue&);`
///   - 處理訊息, 底下函式三選一, 只能提供其中一種:
///      - 一次一筆
///         `void MessageHandlerT::OnMessage(MessageType&);`
///      - 一次一批, 用完後會自動清除 container.
///         `void MessageHandlerT::OnMessage(MessageContainerT& container);`
///      - 一次一批, MessageHandlerT 自行取出訊息.
///         \code
///            void MessageHandlerT::OnMessage(typename MessageQueue::Locker& queue) {
///               assert(this->ConsumingMessage_.empty());
///               this->ConsumingMessage_.swap(*queue);
///               queue.unlock(); // 這裡必須自行呼叫 unlock(); 然後再處理 this->ConsumingMessage_;
///               ... 在這裡處理 this->ConsumingMessage_
///            }
///         \endcode
///   - 若想在喚醒後, 先做一些事情:
///     - 可提供 `void MessageHandlerT::OnAfterWakeup(typename MessageQueue::Locker& queue);` 來處理.
///     - 此時 queue 仍在 lock() 狀態, 若過程中有解鎖, 則在返回前必須再次鎖定.
///   - `void MessageHandlerT::OnThreadEnd(const std::string& thrName);`
template <
   class MessageHandlerT,
   class MessageT = typename MessageHandlerT::MessageType,
   class MessageContainerT = std::deque<MessageT>,
   class WaitPolicyT = WaitPolicy_CV
>
class MessageQueue {
   fon9_NON_COPY_NON_MOVE(MessageQueue);
   using QueueController = ThreadController<MessageContainerT, WaitPolicyT>;
   using LockerT = typename QueueController::Locker;
   using ThreadPool = std::vector<std::thread>;
   QueueController   QueueController_;
   ThreadPool        ThreadPool_;

   template <class MessageHandler>
   static auto OnMessage(MessageHandler& messageHandler, LockerT& queue) -> decltype(messageHandler.OnMessage(queue)) {
      return messageHandler.OnMessage(queue);
   }

   template <class MessageHandler>
   static auto OnMessage(MessageHandler& messageHandler, LockerT& queue) -> decltype(messageHandler.OnMessage(*queue)) {
      MessageContainerT msgQu{std::move(*queue)};
      queue.unlock();
      return messageHandler.OnMessage(msgQu);
   }

   template <class MessageHandler>
   static auto OnBeforeMessage(MessageHandler& messageHandler, MessageT& msg, LockerT* queue) -> decltype(messageHandler.OnBeforeMessage(msg, *queue)) {
      messageHandler.OnBeforeMessage(msg, *queue);
      assert(queue.owns_lock());
   }
   template <class MessageHandler>
   static void OnBeforeMessage(MessageHandler&, ...) {
   }
   template <class MessageHandler>
   static auto OnMessage(MessageHandler& messageHandler, LockerT& queue) -> decltype(messageHandler.OnMessage(queue->front())) {
      MessageT msg(std::move(queue->front()));
      queue->pop_front();
      OnBeforeMessage(messageHandler, &queue);
      queue.unlock();
      return messageHandler.OnMessage(msg);
   }

   template <class MessageHandler>
   static auto OnAfterWakeup(MessageHandler& messageHandler, LockerT* queue) -> decltype(messageHandler.OnAfterWakeup(*queue)) {
      messageHandler.OnAfterWakeup(*queue);
      assert(queue->owns_lock());
   }
   template <class MessageHandler>
   static void OnAfterWakeup(MessageHandler&, ...) {
   }

   bool DoMessages(MessageHandlerT& messageHandler, LockerT& queue) {
      for (;;) {
         this->OnAfterWakeup(messageHandler, &queue);
         switch (ThreadState threadState = this->QueueController_.GetState(queue)) {
         default:
         case ThreadState::Idle:
         case ThreadState::Terminated:
            assert(!"Invalid ThreadState.");
         case ThreadState::EndNow:
            return false;

         case ThreadState::ExecutingOrWaiting:
            if (queue->empty())
               return true;
            break;
         case ThreadState::EndAfterWorkDone:
            if (queue->empty())
               return false;
            break;
         }
         this->OnMessage(messageHandler, queue);
         queue.lock();
      }
   }

   static void ThrRun(std::string thrName, MessageQueue* pthis) {
      SetCurrentThreadName(thrName.c_str());
      fon9_LOG_ThrRun("MessageQueue.ThrRun|name=", thrName);
      MessageHandlerT   messageHandler(*pthis);
      {
         Locker  queue{pthis->QueueController_};
         while (pthis->DoMessages(messageHandler, queue))
            pthis->QueueController_.Wait(queue);
         pthis->QueueController_.OnBeforeThreadEnd(queue);
      }
      messageHandler.OnThreadEnd(thrName);
      fon9_LOG_ThrRun("MessageQueue.ThrRun.End|name=", thrName);
   }

   void WaitThreadJoin() {
      JoinThreads(this->ThreadPool_);
   }

protected:
   QueueController& GetQueueController() {
      return this->QueueController_;
   }

public:
   using MessageHandler = MessageHandlerT;
   using MessageType = MessageT;
   using MessageContainer = MessageContainerT;
   using Locker = LockerT;
   using WaitPolicyType = WaitPolicyT;

   MessageQueue() {
   }
   /// 若有剩餘未執行的訊息，將會被拋棄。
   ~MessageQueue() {
      this->WaitForEndNow();
   }

   void StartThread(uint32_t threadCount, StrView thrName) {
      this->ThreadPool_.reserve(threadCount);
      this->QueueController_.OnBeforeThreadStart(threadCount);
      RevBufferFixedSize<1024> rbuf;
      for (unsigned id = 0; id < threadCount;) {
         rbuf.Rewind();
         RevPrint(rbuf, thrName, "|indexInPool=", ++id);
         this->ThreadPool_.emplace_back(&ThrRun, rbuf.ToStrT<std::string>(), this);
      }
      this->ThreadPool_.shrink_to_fit();
   }
   size_t GetThreadCount() const {
      return this->ThreadPool_.size();
   }

   /// 通知結束，若有剩餘未執行的訊息，可透過 WaitForEndNow(remainMessageHandler) 處理。
   void NotifyForEndNow() {
      this->QueueController_.NotifyForEndNow();
   }
   /// 通知結束, 並在 thread 結束後, 透過 remainMessageHandler 處理剩餘訊息.
   void WaitForEndNow(MessageHandlerT& remainMessageHandler) {
      this->WaitForEndNow();
      Locker  queue{this->QueueController_};
      while (!queue->empty()) {
         this->OnMessage(remainMessageHandler, queue);
         queue.lock();
      }
   }
   /// 通知結束, 並在 thread 結束後返回, 但不處理剩餘訊息.
   /// 可再呼叫 `WaitForEndNow(MessageHandlerT& remainMessageHandler);` 處理剩餘訊息.
   void WaitForEndNow() {
      this->QueueController_.WaitForEndNow();
      this->WaitThreadJoin();
   }

   /// 通知訊息處理完畢後結束 thread.
   void NotifyForEndAfterWorkDone() {
      this->QueueController_.NotifyForEndAfterWorkDone();
   }
   /// 等候 thread 處理完訊息後, 結束 thread.
   void WaitForEndAfterWorkDone() {
      this->QueueController_.WaitForEndAfterWorkDone();
      this->WaitThreadJoin();
   }

   /// 傳回 <= ThreadState::ExecutingOrWaiting 表示有加入 MessageQueue.
   template <class... ArgsT>
   ThreadState EmplaceMessage(ArgsT&&... args) {
      Locker            queue{this->QueueController_};
      const ThreadState st = this->CheckNotify(queue, queue->empty());
      if (st <= ThreadState::ExecutingOrWaiting)
         queue->emplace_back(std::forward<ArgsT>(args)...);
      return st;
   }

   Locker Lock() {
      return Locker{this->QueueController_};
   }
   ThreadState CheckNotify(Locker& locker, bool isQueueEmpty) {
      const ThreadState st = this->QueueController_.GetState(locker);
      if (st <= ThreadState::ExecutingOrWaiting) {
         if (this->QueueController_.GetRunningCount(locker) > 1 || isQueueEmpty)
            this->QueueController_.NotifyOne(locker);
      }
      return st;
   }

   ThreadState GetThreadState() {
      return this->QueueController_.GetState(this->Lock());
   }
};

} // namespace
#endif//__fon9_MessageQueue_hpp__
