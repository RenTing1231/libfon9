/// \file fon9/DefaultThreadPool.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_DefaultThreadPool_hpp__
#define __fon9_DefaultThreadPool_hpp__
#include "fon9/MessageQueue.hpp"
fon9_BEFORE_INCLUDE_STD;
#include <functional>
fon9_AFTER_INCLUDE_STD;

namespace fon9 {

class DefaultThreadPoolWaitPolicy : public WaitPolicy_CV {
   fon9_NON_COPY_NON_MOVE(DefaultThreadPoolWaitPolicy);
   using base = WaitPolicy_CV;
   TimeInterval   WakeupInterval_;
public:
   DefaultThreadPoolWaitPolicy() = default;
   void NotifyOne(const Locker& locker) {
      if (this->WakeupInterval_.GetOrigValue() <= 0)
         base::NotifyOne(locker);
   }
   void Wait(Locker& locker) {
      if (this->WakeupInterval_.GetOrigValue() <= 0)
         base::Wait(locker);
      else
         base::WaitFor(locker, this->WakeupInterval_.ToDuration());
   }
   void SetWakeupInterval(const Locker& locker, TimeInterval v);
};

/// \ingroup Thrs
/// 在 GetDefaultThreadPool() 裡面執行的單一作業。
using DefaultThreadTask = std::function<void()>;
struct DefaultThreadTaskHandler;
class DefaultThreadPool : public MessageQueue< DefaultThreadTaskHandler, DefaultThreadTask,
                                               std::deque<DefaultThreadTask>,
                                               DefaultThreadPoolWaitPolicy> {
   fon9_NON_COPY_NON_MOVE(DefaultThreadPool);
public:
   DefaultThreadPool() = default;
   void SetWakeupInterval(TimeInterval v) {
      this->GetQueueController().GetWaitPolicy().SetWakeupInterval(this->Lock(), v);
   }
};

/// \ingroup Thrs
/// 在程式啟動首次呼叫 GetDefaultThreadPool() 之前, 可以先呼叫此處設定 DefaultThreadPool 的參數:
/// 若已經呼叫過 GetDefaultThreadPool(), 則不會有任何變動;
/// - 目前 DefaultThreadPool 有用到的地方:
///   - log file(FileAppender)
///   - DN resolve(io/SocketAddressDN.cpp)
///   - Device 執行 OpQueue_
///   - InnDbf
///   - ...
/// - threadCount 應該考慮不同工作種類的數量, 與 CPU 核心數無關.
///   因為以上的工作都是: 低 CPU 用量, 且 IO blocking.
/// - 若 usWakeupInterval.GetOrigValue() <= 0, 則表示需喚醒 DefaultThreadPool 時, 使用 notify_one();
/// - 若 usWakeupInterval.GetOrigValue() >  0, 則表示定時喚醒 DefaultThreadPool 的時間間隔;
fon9_API void PresetDefaultThreadPoolValues(uint8_t threadCount, TimeInterval wakeupInterval);
/// \ingroup Thrs
/// 透過執行時參數設定 DefaultThreadPool 參數: --DefaultThreadPool=N/Interval
/// 若已經呼叫過 GetDefaultThreadPool(), 則不會有任何變動;
/// - N: thread count;
/// - Interval: 0(預設)=採用notify_one(), >0:喚醒間隔(例: 1ms);
fon9_API void PresetDefaultThreadPoolStrArg(StrView args);
/// \ingroup Thrs
/// 透過執行時參數設定 DefaultThreadPool 參數: --DefaultThreadPool=N/Interval
/// \ref PresetDefaultThreadPoolStrArg();
fon9_API void PresetDefaultThreadPoolCmdArg(int argc, const char* argv[]);

/// \ingroup Thrs
/// 取得 fon9 提供的一個 thread pool.
/// * 一般用於不急迫, 但比較花時間的簡單工作, 例如: 寫檔、domain name 查找...
/// * 程式結束時, 剩餘的工作會被拋棄!
fon9_API DefaultThreadPool& GetDefaultThreadPool();

/// \ingroup Thrs
/// 如果 link 不到 DefaultThreadPool::EmplaceMessage(); 則可用此函式;
fon9_API void DefaultThreadPool_AddTask(DefaultThreadTask&& task);

/// \ingroup Thrs
/// 等候 thread pool 將所有的工作完成之後結束 thread pool.
/// 結束 thread pool 之後, 不會再處理後續加入的工作!!
/// 為了讓等候中的工作有機會全部做完, 所以:
/// - 總共等候 waitTimes 次循環(每個 thread 都完成工作).
/// - 每次循環之後睡一會兒(tiSleep) 等看看有沒有新工作.
fon9_API void WaitDefaultThreadPoolQuit(DefaultThreadPool& thrPool,
                                        unsigned waitTimes = 3,
                                        TimeInterval tiSleep = TimeInterval_Millisecond(10));

} // namespaces
#endif//__fon9_DefaultThreadPool_hpp__
