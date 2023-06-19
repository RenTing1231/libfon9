// \file fon9/rc/RcClientApi.h
// \author fonwinz@gmail.com
#ifndef __fon9_rc_RcClientApi_h__
#define __fon9_rc_RcClientApi_h__
#include "fon9/io/IoState.h"
#include "fon9/rc/Rc.h"
#include <stddef.h> // offsetof();
#include <string.h> // memset();

#ifdef __cplusplus
extern "C" {
#endif//__cplusplus

/// 啟動 fon9 C API 函式庫.
/// - 請使用時注意: 不考慮 multi thread 同時呼叫 fon9_Initialize()/fon9_Finalize();
/// - 可重覆呼叫 fon9_Initialize(), 但須有對應的 fon9_Finalize();
///   最後一個 fon9_Finalize() 被呼叫時, 結束函式庫.
/// - 啟動 Device factory.
/// - 啟動 LogFile: 
///   - if (logFileFmt == NULL) 表示不改變 log 的輸出位置, 預設 log 輸出在 console.
///   - if (*logFileFmt == '\0') 則 logFileFmt = "./logs/{0:f+'L'}/fon9cli.log";
///     {0:f+'L'} 表示使用 local time 的 YYYYMMDD.
/// - 若返回非 0:
///   - ENOSYS: 已經呼叫 fon9_Finalize() 系統結束.
///   - 其他為 log 檔開啟失敗的原因(errno).
fon9_CAPI_FN(int) fon9_Initialize(const char* logFileFmt);
/// 同 fon9_Initialize(); 但可在首次初始化時設定 rc session 的 iosv 參數.
/// 若 iosvCfg == NULL; 則使用預設: "ThreadCount=1|Wait=Block";
/// iosvCfg 未指定的參數預設值: ThreadCount=2|Wait=Block 不綁核, 
fon9_CAPI_FN(int) f9rc_Initialize(const char* logFileFmt, const char* iosvCfg);
/// 在首次 fon9_Initialize() or f9rc_Initialize() 之前, 可設定 DefaultThreadPool 的參數.
/// - 在 fon9_Initialize() or f9rc_Initialize() 之後, 呼叫此處則沒有任何效果.
/// - 若 usWakeupInterval == 0, 則表示需喚醒 DefaultThreadPool 時, 使用 notify_one();
/// - 若 usWakeupInterval > 0,  則表示定時喚醒 DefaultThreadPool 的時間間隔(microsecond);
fon9_CAPI_FN(void) fon9_PresetDefaultThreadPoolValues(uint8_t threadCount, uint64_t usWakeupInterval);
/// 透過字串參數設定 DefaultThreadPool 參數: N/Interval
/// - N: thread count;
/// - Interval: 0(預設)=採用notify_one(), >0:喚醒間隔(例: 1ms);
///   - 字串格式預設為秒, 例: 1.5 表示 1.5秒; 
///   - 可加 ms 後綴, 表示毫秒(1/1000秒), 例: 500ms;
fon9_CAPI_FN(void) fon9_PresetDefaultThreadPoolStrArg(const char* args);
/// 透過執行時參數設定 DefaultThreadPool 參數: --DefaultThreadPool=N/Interval
fon9_CAPI_FN(void) fon9_PresetDefaultThreadPoolCmdArg(int argc, const char* argv[]);

/// 結束 fon9 函式庫.
/// - 結束前必須先將所有建立的物件刪除:
///   - 例如: f9rc_CreateClientSession() => f9rc_DestroyClientSession();
/// - 結束系統後, 無法透過 fon9_Initialize() 重啟, 若要重啟, 必須結束程式重新執行.
/// - 返回 0 表示已結束, 無法再呼叫 fon9_Initialize() 重新啟動.
/// - 返回 >0 表示還有多少次, 已呼叫 fon9_Initialize(), 但尚未呼叫 fon9_Finalize();
fon9_CAPI_FN(uint32_t) fon9_Finalize(void);

// typedef void (fon9_CAPI_CALL *fon9_FnFinalizeHandler)(void* userData);
// /// 註冊在最後一個 fon9_Finalize() 被呼叫時, 要處理的事項.
// /// - 正在結束時的註冊, 不會被呼叫.
// /// - 此函式 thread safe.
// ///
// /// \retval 0 註冊失敗: 重複註冊(fnFinalizeHandler 及 userData 皆相同).
// /// \retval 1 註冊成功.
// fon9_CAPI_FN(int) fon9_AddFinalizeHandler(fon9_FnFinalizeHandler fnFinalizeHandler, void* userData);

//--------------------------------------------------------------------------//

typedef struct fon9_IoManager    fon9_IoManager;

/// 必須在 fon9_Initialize() 之後呼叫.
/// 系統結束前必須使用 fon9_DestroyIoManager(retval); 刪除已建立的 IoManager.
/// \param name 在 fon9 LogFile 辨識.
/// \param iosvCfg "ThreadCount=n|Wait=Policy|Cpus=List|Capacity=0";
///                - 若為 NULL 或 "", 預設: "ThreadCount=2|Wait=Block";
///                - List: 要綁定的 CPU(Core) 列表, 使用「,」分隔, 例如: 1,2
///                - Capacity: 可支援的連線(fd)數量, 預設為 0 = 自動擴增;
/// \retval NULL 建立失敗, 尚未呼叫 fon9_Initialize(); 或已呼叫 fon9_Finalize();
fon9_CAPI_FN(fon9_IoManager*) fon9_CreateIoManager(const char* name, const char* iosvCfg);

/// 系統結束前必須刪除已建立的 IoManager.
fon9_CAPI_FN(void) fon9_DestroyIoManager(fon9_IoManager* ioMgr, int isWait);
static inline void fon9_DestroyIoManager_Wait(fon9_IoManager* ioMgr)   { fon9_DestroyIoManager(ioMgr, 1); }
static inline void fon9_DestroyIoManager_NoWait(fon9_IoManager* ioMgr) { fon9_DestroyIoManager(ioMgr, 0); }

/// 取得 rc client session 的 IoManager.
/// 必須先呼叫過 fon9_Initialize(); 或 f9rc_Initialize();
/// retval 不可透過 fon9_DestroyIoManager() 刪除.
fon9_CAPI_FN(fon9_IoManager*) f9rc_GetRcClientIoManager(void);

typedef struct {
   const char* SesName_;
   const char* SesParams_;
   const char* DevName_;
   const char* DevParams_;
} fon9_IoSessionParams;

//--------------------------------------------------------------------------//

/// Rc protocol 可以支援多種功能呼叫(例: OmsRc, SeedVisitor...)
/// 每種功能呼叫, 可能需要 API 的使用者提供: 必要資料, 及相關的 Handler.
/// \code
///   typedef struct {
///      f9rc_FunctionNoteParams BaseParam_;
///      ... 其他參數 ...
///   } f9sv_ClientSessionParams;
/// \endcode
typedef struct {
   /// 例如: sizeof(f9sv_ClientSessionParams);
   uint32_t          ParamSize_;
   f9rc_FunctionCode FunctionCode_;
   char              Padding__[3];
} f9rc_FunctionNoteParams;

/// 設定需要記錄那些 log.
/// - 0x00000007 for RcSession    : f9rc_ClientLogFlag_*
/// - 0x00000ff8 for SeedVisitor  : f9sv_ClientLogFlag_*  定義在 fon9/rc/RcSeedVisitor.h
/// - 0x000f0000 for OmsRc        : f9oms_ClientLogFlag_* 定義在 f9omstw/f9omsrc/OmsRc.h
fon9_ENUM(f9rc_ClientLogFlag, uint32_t) {
   f9rc_ClientLogFlag_None = 0,
   f9rc_ClientLogFlag_All  = 0x7fffffff,
   f9sv_ClientLogFlag_All  = 0x00000ff8,
   f9oms_ClientLogFlag_All = 0x000f0000,

   /// 連線(斷線)相關事件.
   f9rc_ClientLogFlag_Link = 0x00000001,
};

typedef struct {
   void*                UserData_;
   f9rc_ClientLogFlag   LogFlags_;
   char                 Reserved___[4];
} f9rc_ClientSession;

typedef void (fon9_CAPI_CALL *f9rc_FnOnLinkEv)(f9rc_ClientSession*, f9io_State st, fon9_CStrView info);

/// 建立 RcClientSession 連線時所需要的參數.
typedef struct {
   const char*          UserId_;
   const char*          Password_;
   const char*          DevName_;
   const char*          DevParams_;
   void*                UserData_;
   f9rc_RcFlag          RcFlags_;
   char                 Reserved___[2];
   f9rc_ClientLogFlag   LogFlags_;

   /// 連線有關的事件: 連線失敗, 連線後斷線...
   f9rc_FnOnLinkEv      FnOnLinkEv_;

   /// 各個 FunctionCode 所需要的參數.
   f9rc_FunctionNoteParams*   FunctionNoteParams_[f9rc_FunctionCode_Count];

} f9rc_ClientSessionParams;

#define f9rc_InitClientSessionParams(f9rcClientSessionParams, f9rcFunctionNoteParams, funcCode) {  \
   memset(f9rcFunctionNoteParams, 0, sizeof(*f9rcFunctionNoteParams));                             \
   f9rcFunctionNoteParams->BaseParams_.FunctionCode_ = funcCode;                                   \
   f9rcFunctionNoteParams->BaseParams_.ParamSize_ = sizeof(*f9rcFunctionNoteParams);               \
   f9rcClientSessionParams->FunctionNoteParams_[funcCode] = &f9rcFunctionNoteParams->BaseParams_;  \
}

//--------------------------------------------------------------------------//

/// 建立一個 rc 連線物件, 當您不再使用時, 應呼叫 f9rc_DestroyClientSession(*result) 銷毀.
/// - 除了 args->UserData_ 可以為任意值(包含NULL), 其他指標若為 NULL, 則直接 crash!
/// - args->DevName_: 通訊設備名稱, 例如: "TcpClient";
/// - args->DevParams_: 通訊設備參數, 例如: "192.168.1.1:6601"; "dn=localhost:6601"
/// - 有可能在返回前就觸發事件, 但此時 *result 必定已經填妥已建立的 Session.
/// - retval 1 成功: *result 儲存 Session;
/// - retval 0 失敗: devName 不正確, 或尚未呼叫 fon9_Initialize();
fon9_CAPI_FN(int)
f9rc_CreateClientSession(f9rc_ClientSession** result,
                         const f9rc_ClientSessionParams* args);

/// 銷毀一個 rc 連線物件.
/// 在返回前, 仍然可能在其他 thread 收到事件.
/// isWait = 1 表示要等確定銷毀後才返回.
/// isWait = 0 表示在返回後仍可能收到事件, 如果您是在事件通知時呼叫, 則不能等候銷毀(會造成死結).
fon9_CAPI_FN(void)
f9rc_DestroyClientSession(f9rc_ClientSession* ses, int isWait);

static inline void f9rc_DestroyClientSession_Wait(f9rc_ClientSession* ses) {
   f9rc_DestroyClientSession(ses, 1);
}
static inline void f9rc_DestroyClientSession_NoWait(f9rc_ClientSession* ses) {
   f9rc_DestroyClientSession(ses, 0);
}

#ifdef __cplusplus
}//extern "C"
#endif//__cplusplus
#endif//__fon9_rc_RcClientApi_h__
