// \file fon9/rc/RcClientApi.cpp
// \author fonwinz@gmail.com
#include "fon9/rc/RcClientApi.h"
#include "fon9/rc/RcClientSession.hpp"
#include "fon9/DefaultThreadPool.hpp"
#include "fon9/LogFile.hpp"
#include "fon9/sys/OnWindowsMainExit.hpp"

namespace fon9 { namespace rc {

enum f9rc_St {
   f9rc_St_Pending,
   f9rc_St_Initializing,
   f9rc_St_Initialized,
   f9rc_St_Finalizing,
   f9rc_St_Finalized,
};
static volatile f9rc_St       f9rc_St_{f9rc_St_Pending};
static std::atomic<uint32_t>  f9rc_InitCount_{0};
intrusive_ptr<RcClientMgr>    RcClientMgr_;
//--------------------------------------------------------------------------//
// struct FinalizeHandlerRec {
//    fon9_FnFinalizeHandler  FnFinalizeHandler_;
//    void*                   UserData_;
//    bool operator==(const FinalizeHandlerRec& rhs) const {
//       return this->FnFinalizeHandler_ == rhs.FnFinalizeHandler_ && this->UserData_ == rhs.UserData_;
//    }
// };
// using FinalizeHandlersImpl = std::vector<FinalizeHandlerRec>;
// static MustLock<FinalizeHandlersImpl>  FinalizeHandlers_;

} } // namespaces
using namespace fon9;
using namespace fon9::rc;

//--------------------------------------------------------------------------//

extern "C" {

// fon9_CAPI_FN(int)
// fon9_AddFinalizeHandler(fon9_FnFinalizeHandler fnFinalizeHandler, void* userData) {
//    FinalizeHandlerRec rec{fnFinalizeHandler, userData};
//    auto recs = FinalizeHandlers_.Lock();
//    if (std::find(recs->begin(), recs->end(), rec) != recs->end())
//       return 0;
//    recs->push_back(rec);
//    return 1;
// }

fon9_CAPI_FN(int)
f9rc_Initialize(const char* logFileFmt, const char* iosvCfg) {
   if (f9rc_St_ >= f9rc_St_Finalizing)
      return ENOSYS;
   ++f9rc_InitCount_;
   if (logFileFmt) {
      if (*logFileFmt == '\0')
         logFileFmt = "./logs/{0:f+'L'}/fon9cli.log";
      auto res = fon9::InitLogWriteToFile(logFileFmt, fon9::FileRotate::TimeScale::Day, 0, 0);
      if (res.IsError())
         return res.GetError().value();
   }
   // 不考慮 multi thread 同時呼叫 fon9_Initialize()/fon9_Finalize() 的情況.
   if (f9rc_St_ < f9rc_St_Initializing) {
      f9rc_St_ = f9rc_St_Initializing;

      assert(RcClientMgr_.get() == nullptr);
      fon9::IoManagerArgs ioMgrArgs;
      ioMgrArgs.Name_ = "RcClientMgr";
      ioMgrArgs.IoServiceCfgstr_ = (iosvCfg ? iosvCfg : "ThreadCount=1|Wait=Block");
      RcClientMgr_.reset(new RcClientMgr{ioMgrArgs});

      fon9::GetDefaultThreadPool();
      fon9::GetDefaultTimerThread();
      f9rc_St_ = f9rc_St_Initialized;
   }
   else while (f9rc_St_ == f9rc_St_Initializing) {
      std::this_thread::yield();
   }
   return 0;
}

fon9_CAPI_FN(int)
fon9_Initialize(const char* logFileFmt) {
   return f9rc_Initialize(logFileFmt, nullptr);
}

fon9_CAPI_FN(void)
fon9_PresetDefaultThreadPoolValues(uint8_t defaultThreadPool_ThreadCount, uint64_t usWakeupInterval) {
   fon9::PresetDefaultThreadPoolValues(defaultThreadPool_ThreadCount, fon9::TimeInterval_Microsecond(fon9::signed_cast(usWakeupInterval)));
}

fon9_CAPI_FN(void)
fon9_PresetDefaultThreadPoolCmdArg(int argc, const char* argv[]) {
   fon9::PresetDefaultThreadPoolCmdArg(argc, argv);
}
fon9_CAPI_FN(void)
fon9_PresetDefaultThreadPoolStrArg(const char* args) {
   fon9::PresetDefaultThreadPoolStrArg(fon9::StrView_cstr(args));
}


fon9_CAPI_FN(fon9_IoManager*)
f9rc_GetRcClientIoManager() {
   return RcClientMgr_.get();
}
   
fon9_CAPI_FN(uint32_t)
fon9_Finalize() {
   assert(f9rc_InitCount_ > 0);
   if (f9rc_InitCount_ == 0)
      return 0;
   if (--f9rc_InitCount_ > 0)
      return f9rc_InitCount_;
   f9rc_St_ = f9rc_St_Finalizing;

   // FinalizeHandlersImpl finalizeHandlers{std::move(*FinalizeHandlers_.Lock())};
   // for (auto rec : finalizeHandlers)
   //    rec.FnFinalizeHandler_(rec.UserData_);

   if (RcClientMgr_) {
      RcClientMgr_->DisposeDevices("fon9_Finalize");
      while (RcClientMgr_->use_count() > 1)
         std::this_thread::yield();
      RcClientMgr_.reset();
   }

   fon9::WaitDefaultThreadPoolQuit(fon9::GetDefaultThreadPool());
   #ifdef fon9_HAVE_OnWindowsMainExitHandle
      // 讓 sys::OnWindowsMainExitHandle 的衍生者正常結束.
      // 例如 fon9::GetDefaultTimerThread();
      fon9::sys::OnWindowsMainExit_When_No_atexit();
   #endif
   f9rc_St_ = f9rc_St_Finalized;
   return 0;
}

fon9_CAPI_FN(int)
f9rc_CreateClientSession(f9rc_ClientSession** result,
                         const f9rc_ClientSessionParams* params) {
   if (RcClientMgr_.get() == nullptr)
      return false;
   RcClientMgr::RcClientConfigItem iocfg;
   iocfg.Params_ = params;
   return RcClientMgr_->CreateRcClientSession(result, iocfg);
}

fon9_CAPI_FN(void)
f9rc_DestroyClientSession(f9rc_ClientSession* ses, int isWait) {
   if (ses && RcClientMgr_)
      RcClientMgr_->DestroyRcClientSession(static_cast<RcClientSession*>(ses), isWait);
}
//--------------------------------------------------------------------------//
fon9_CAPI_FN(fon9_IoManager*) fon9_CreateIoManager(const char* name, const char* iosvCfg) {
   if (f9rc_InitCount_ <= 0)
      return nullptr;
   ++f9rc_InitCount_;
   fon9::IoManagerArgs ioMgrArgs;
   ioMgrArgs.Name_.assign(name);
   if (iosvCfg)
      ioMgrArgs.IoServiceCfgstr_.assign(iosvCfg);
   fon9_IoManager* ioMgr = new fon9_IoManager{ioMgrArgs};
   intrusive_ptr_add_ref(ioMgr);
   return ioMgr;
}
fon9_CAPI_FN(void) fon9_DestroyIoManager(fon9_IoManager* ioMgr, int isWait) {
   if (ioMgr == nullptr)
      return;
   ioMgr->DisposeDevices("DestroyIoManager");
   if (isWait) {
      while (ioMgr->use_count() > 1)
         std::this_thread::yield();
   }
   intrusive_ptr_release(ioMgr);
   fon9_Finalize(); // 在 fon9_CreateIoManager(); 有 ++f9rc_InitCount_; 所以在此需要 fon9_Finalize();
}

} // extern "C"
