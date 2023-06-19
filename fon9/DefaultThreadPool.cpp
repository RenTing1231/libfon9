// \file fon9/DefaultThreadPool.cpp
// \author fonwinz@gmail.com
#include "fon9/DefaultThreadPool.hpp"
#include "fon9/sys/OnWindowsMainExit.hpp"
#include "fon9/CyclicBarrier.hpp"
#include "fon9/CmdArgs.hpp"

namespace fon9 {

static uint8_t       gDefaultThreadPool_Preset_ThreadCount;
static TimeInterval  gDefaultThreadPool_Preset_WakeupInterval;
//--------------------------------------------------------------------------//
fon9_API void PresetDefaultThreadPoolValues(uint8_t threadCount, TimeInterval wakeupInterval) {
   gDefaultThreadPool_Preset_ThreadCount = threadCount;
   gDefaultThreadPool_Preset_WakeupInterval = wakeupInterval;
}
fon9_API void PresetDefaultThreadPoolStrArg(StrView args) {
   if (args.empty())
      return;
   gDefaultThreadPool_Preset_ThreadCount = StrTo(&args, gDefaultThreadPool_Preset_ThreadCount);
   StrTrimHead(&args);
   if (!fon9::isalnum(args.Get1st()))
      args.SetBegin(args.begin() + 1);
   if (!args.empty())
      gDefaultThreadPool_Preset_WakeupInterval = StrTo(args, gDefaultThreadPool_Preset_WakeupInterval);
}
fon9_API void PresetDefaultThreadPoolCmdArg(int argc, const char* argv[]) {
   PresetDefaultThreadPoolStrArg(GetCmdArg(argc, argv, nullptr, "DefaultThreadPool"));
}
//--------------------------------------------------------------------------//
void DefaultThreadPoolWaitPolicy::SetWakeupInterval(const Locker& locker, TimeInterval v) {
   fon9_LOG_INFO("fon9.DefaultThreadPool|WakeupInterval=", v);
   this->WakeupInterval_ = v;
   this->NotifyAll(locker);
}
fon9_API DefaultThreadPool& GetDefaultThreadPool() {
   struct DefaultThreadPoolImpl : public DefaultThreadPool, sys::OnWindowsMainExitHandle {
      fon9_NON_COPY_NON_MOVE(DefaultThreadPoolImpl);
      DefaultThreadPoolImpl() {
         this->SetWakeupInterval(gDefaultThreadPool_Preset_WakeupInterval);
         this->StartThread((gDefaultThreadPool_Preset_ThreadCount ? gDefaultThreadPool_Preset_ThreadCount : 4u),
                           "fon9.DefaultThreadPool");
      }
      void OnWindowsMainExit_Notify() {
         this->NotifyForEndNow();
      }
      void OnWindowsMainExit_ThreadJoin() {
         this->WaitForEndNow();
      }
   };
   static DefaultThreadPoolImpl ThreadPool_;
   return ThreadPool_;
}
fon9_API void DefaultThreadPool_AddTask(DefaultThreadTask&& task) {
   GetDefaultThreadPool().EmplaceMessage(std::move(task));
}
//--------------------------------------------------------------------------//
struct DefaultThreadTaskHandler {
   using MessageType = DefaultThreadTask;
   template <class DefaultThreadPoolBase>
   DefaultThreadTaskHandler(DefaultThreadPoolBase&) {
      if (gWaitLogSystemReady)
         gWaitLogSystemReady();
   }
   void OnMessage(DefaultThreadTask& task) {
      task();
   }
   void OnThreadEnd(const std::string& threadName) {
      (void)threadName;
   }
};
fon9_API void WaitDefaultThreadPoolQuit(DefaultThreadPool& thrPool, unsigned waitTimes, TimeInterval tiSleep) {
   thrPool.SetWakeupInterval(TimeInterval{});
   if (thrPool.GetThreadState() < ThreadState::EndAfterWorkDone) {
      for (; waitTimes > 0; --waitTimes) {
         unsigned       L = static_cast<unsigned>(thrPool.GetThreadCount());
         CyclicBarrier  waiter{L + 1};
         for (; L > 0; --L) {
            thrPool.EmplaceMessage([&waiter]() {
               waiter.Wait();
            });
         }
         waiter.Wait();
         std::this_thread::sleep_for(tiSleep.ToDuration());
      }
   }
   thrPool.WaitForEndAfterWorkDone();

   // 把剩餘工作做完, 避免 memory leak.
   DefaultThreadTaskHandler handler(thrPool);
   thrPool.WaitForEndNow(handler);
}

} // namespaces
