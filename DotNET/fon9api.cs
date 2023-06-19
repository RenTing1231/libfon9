using System;
using System.Text;
using System.Runtime.InteropServices;

namespace fon9
{
   using uint32_t = System.UInt32;

   public static class Api
   {
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
      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "fon9_Initialize", CharSet = CharSet.Ansi)]
      public static extern int Initialize([MarshalAs(UnmanagedType.LPStr)] string logFileFmt);

      /// 同 Initialize(); 但可在首次初始化時設定 rc session 的 iosv 參數.
      // fon9_CAPI_FN(int) f9rc_Initialize(const char* logFileFmt, const char* iosvCfg);
      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "f9rc_Initialize", CharSet = CharSet.Ansi)]
      public static extern int RcInitialize([MarshalAs(UnmanagedType.LPStr)] string logFileFmt, [MarshalAs(UnmanagedType.LPStr)] string iosvCfg);

      /// 在首次 Initialize() or RcInitialize() 之前, 可設定 DefaultThreadPool 的參數.
      /// - 在 Initialize() or RcInitialize() 之後, 呼叫此處則沒有任何效果.
      /// - 若 usWakeupInterval == 0, 則表示需喚醒 DefaultThreadPool 時, 使用 notify_one();
      /// - 若 usWakeupInterval > 0,  則表示定時喚醒 DefaultThreadPool 的時間間隔(microsecond);
      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "fon9_PresetDefaultThreadPoolValues", CharSet = CharSet.Ansi)]
      public static extern void PresetDefaultThreadPoolValues(Byte defaultThreadPool_ThreadCount, UInt64 usWakeupInterval);

      /// 透過字串參數設定 DefaultThreadPool 參數: N/Interval
      /// - N: thread count;
      /// - Interval: 0(預設)=採用notify_one(), >0:喚醒間隔(例: 1ms);
      ///   - 字串格式預設為秒, 例: 1.5 表示 1.5秒; 
      ///   - 可加 ms 後綴, 表示毫秒(1/1000秒), 例: 500ms;
      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "fon9_PresetDefaultThreadPoolStrArg", CharSet = CharSet.Ansi)]
      public static extern void PresetDefaultThreadPoolStrArg([MarshalAs(UnmanagedType.LPStr)] string args);

      /// 結束 fon9 函式庫.
      /// - 結束前必須先將所有建立的物件刪除:
      ///   - 例如: f9rc_CreateClientSession() => f9rc_DestroyClientSession();
      /// - 結束系統後, 無法透過 fon9_Initialize() 重啟, 若要重啟, 必須結束程式重新執行.
      /// - 返回 0 表示已結束, 無法再呼叫 fon9_Initialize() 重新啟動.
      /// - 返回 >0 表示還有多少次, 已呼叫 fon9_Initialize(), 但尚未呼叫 fon9_Finalize();
      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "fon9_Finalize")]
      public static extern uint32_t Finalize();

      /// 要求使用者從 console 輸入密碼.
      /// buflen 包含 EOS'\0', 一般用法:
      /// 如果 promptFD == NULL: 則不輸出 '*';
      /// 如果 promptStr != NULL: 則: 若 promptFD==NULL 則使用 stderr 輸出;
      /// \return 不含EOS的長度.
      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "fon9_getpass", CharSet = CharSet.Ansi)]
      static extern UInt64 getpass(IntPtr promptFD, string prompt, ref byte pwbuf, UInt64 bufsz);
      public static string getpass(string prompt)
      {
         byte[] pwbuf = new byte[1024];
         UInt64 sz = getpass(IntPtr.Zero, prompt, ref pwbuf[0], (uint)pwbuf.Length);
         return Encoding.UTF8.GetString(pwbuf, 0, (int)sz);
      }

      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "fon9_SetLogLevel", CharSet = CharSet.Ansi)]
      public static extern void SetLogLevel(LogLevel lv);

      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "fon9_GetLogLevel", CharSet = CharSet.Ansi)]
      public static extern LogLevel GetLogLevel();
   }

   public enum LogLevel : byte
   {
      /// 追蹤程式運行用的訊息.
      Trace,
      /// 抓蟲用的訊息.
      Debug,
      /// 一般資訊.
      Info,
      /// 重要訊息, 例如: thread start.
      Important,
      /// 警告訊息.
      Warn,
      /// 錯誤訊息.
      Error,
      /// 嚴重錯誤.
      Fatal,
   }

   [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
   public struct CStrView
   {
      internal IntPtr Begin_;
      internal IntPtr End_;
      /// <summary>
      /// 在 CStrView 的使用期間, 參照的 byte[] refBytes; 必須保持有效.
      /// </summary>
      public CStrView(byte[] refBytes)
      {
         this.Begin_ = IntPtr.Zero;
         this.End_ = IntPtr.Zero;
         this.RefBytes(refBytes);
      }
      public void RefBytes(byte[] refBytes)
      {
         unsafe
         {
            fixed (byte* p = refBytes)
            {
               this.Begin_ = new IntPtr(p);
               this.End_ = new IntPtr(p + refBytes.Length);
            }
         }
      }

      public int Length { get { return (int)(this.End_.ToInt64() - this.Begin_.ToInt64()); } }

      public override string ToString()
      {
         if (this.Begin_ == IntPtr.Zero)
            return string.Empty;
         // 適用於 .NET Core 3.1 3.0 2.2 2.1 2.0 1.1; .NET Standard 2.1;
         // .NET Framework 4.6 未支援
         // return Marshal.PtrToStringUTF8(this.Begin_, this.Length);
         unsafe
         {  // .NET Framework 4.6(含以上) 才有支援 Encoding.UTF8.GetString(byte*, int);
            return Encoding.UTF8.GetString((byte*)this.Begin_.ToPointer(), this.Length);
         }
      }
   }

   public static class Tools
   {
      public const int STD_INPUT_HANDLE = -10;
      [DllImport("kernel32.dll", SetLastError = true)]
      public static extern IntPtr GetStdHandle(int nStdHandle);

      [DllImport("kernel32.dll", SetLastError = true)]
      public static extern bool CancelIoEx(IntPtr handle, IntPtr lpOverlapped);

      [DllImport("kernel32.dll")]
      public static extern void ExitThread(int dwExitCode);

      [DllImport("Kernel32")]
      public static extern bool SetConsoleCtrlHandler(HandlerRoutine handle, bool add);
      public static HandlerRoutine SetConsoleCtrlHandler()
      {
         HandlerRoutine hrCtrl = new HandlerRoutine(CtrlHandler);
         SetConsoleCtrlHandler(hrCtrl, true);
         return hrCtrl;
      }

      public delegate Boolean HandlerRoutine(CtrlTypes CtrlType);

      public enum CtrlTypes
      {
         CTRL_C_EVENT = 0,
         CTRL_BREAK_EVENT,
         CTRL_CLOSE_EVENT,
         CTRL_LOGOFF_EVENT = 5,
         CTRL_SHUTDOWN_EVENT
      }

      public static string CtrlMessage_;
      public static bool CtrlHandler(CtrlTypes ctrlType)
      {
         CtrlMessage_ = ctrlType.ToString();
         CancelIoEx(GetStdHandle(STD_INPUT_HANDLE), IntPtr.Zero);
         ExitThread(0);
         return true;
      }
   }

   /// 在 IoManager 裡面, 建立連線時所需要的參數.
   [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
   public class IoSessionParams
   {
      [MarshalAs(UnmanagedType.LPStr)]
      public string SesName_;
      [MarshalAs(UnmanagedType.LPStr)]
      public string SesParams_;
      [MarshalAs(UnmanagedType.LPStr)]
      public string DevName_;
      [MarshalAs(UnmanagedType.LPStr)]
      public string DevParams_;
   }
   public class IoManager : IDisposable
   {
      IntPtr IoMgr_ = IntPtr.Zero;

      public IoManager()
      {
      }
      public IoManager(string name, string iosvCfg)
      {
         this.Create(name, iosvCfg);
      }

      /// 使用 DestroyWait(); 關閉全部連線.
      public void Dispose()
      {
         this.DestroyWait();
      }
      public void DestroyWait() => Destroy(true);
      public void DestroyNoWait() => Destroy(false);
      /// 銷毀 IoManager, 並在刪除前關閉全部連線.
      /// 在返回前, 仍然可能在其他 thread 收到事件.
      /// isWait = true 表示要等確定銷毀後才返回.
      /// isWait = false 表示在返回後仍可能收到事件, 如果您是在事件通知時呼叫, 則不能等候銷毀(會造成死結).
      public void Destroy(bool isWait)
      {
         IntPtr ioMgr = this.IoMgr_;
         if (ioMgr == IntPtr.Zero)
            return;
         this.IoMgr_ = IntPtr.Zero;
         DestroyIoManager(ioMgr, isWait ? 1 : 0);
      }

      public IntPtr Handle { get { return this.IoMgr_; } }

      /// 建立 IoManager 連線管理物件.
      /// - 必須在 fon9.Api.Initialize() 之後呼叫.
      /// - 當您不再使用時, 應呼叫 Dispose(); 或 DestroyWait(); 或 DestroyNoWait(); 關閉.
      /// - 建立之前, 會先用 this.DestroyWait(); 銷毀之前的連線.
      /// - name 在 fon9 LogFile 辨識, 是哪個 IoManager 紀錄的 log.
      /// - iosvCfg 範例 "ThreadCount=n|Wait=Policy|Cpus=List|Capacity=0";
      ///   - 若 iosvCfg.IsNullOrEmpty(), 預設: "ThreadCount=2|Wait=Block";
      ///   - List: 要綁定的 CPU(Core) 列表, 使用「,」分隔, 例如: 1,2
      ///   - Capacity: 可支援的連線(fd)數量, 預設為 0 = 自動擴增;
      /// - retval true 成功.
      /// - retval false 失敗: 尚未呼叫 fon9.Api.Initialize(); 或已呼叫 fon9.Api.Finalize();
      public bool Create(string name, string iosvCfg)
      {
         this.DestroyWait();
         this.IoMgr_ = CreateIoManager(name, iosvCfg);
         return this.IoMgr_ == IntPtr.Zero ? false : true;
      }

      // fon9_CAPI_FN(fon9_IoManager*) fon9_CreateIoManager(const char* name, const char* iosvCfg);
      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "fon9_CreateIoManager", CharSet = CharSet.Ansi)]
      static extern IntPtr CreateIoManager([MarshalAs(UnmanagedType.LPStr)] string name, [MarshalAs(UnmanagedType.LPStr)] string iosvCfg);

      /// 系統結束前必須刪除已建立的 IoManager.
      // fon9_CAPI_FN(void) fon9_DestroyIoManager(fon9_IoManager* ioMgr, int isWait);
      [DllImport(fon9.DotNetApi.kDllName, EntryPoint = "fon9_DestroyIoManager")]
      static extern void DestroyIoManager(IntPtr ioMgr, int isWait);
   }
}
