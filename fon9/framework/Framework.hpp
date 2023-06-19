/// \file fon9/framework/Framework.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_framework_Framework_hpp__
#define __fon9_framework_Framework_hpp__
#include "fon9/seed/PluginsMgr.hpp"
#include "fon9/auth/AuthMgr.hpp"

namespace fon9 {

namespace seed { class fon9_API SysEnv; }

/// \ingroup Misc
struct fon9_API Framework {
   std::string          ConfigPath_;
   std::string          SyncerPath_;
   seed::MaTreeSP       Root_;
   InnSyncerSP          Syncer_;
   auth::AuthMgrSP      MaAuth_;
   seed::PluginsMgrSP   MaPlugins_;

   virtual ~Framework();

   /// create system default object.
   /// - 設定工作目錄:  `-w dir` or `--workdir dir`
   /// - ConfigPath_ = `-c cfgpath` or `--cfg cfgpath` or `getenv("fon9cfg");` or default="fon9cfg"
   /// - 然後載入設定: fon9local.cfg, fon9common.cfg; 設定內容包含:
   ///   - LogFileFmt  如果沒設定, log 就輸出在 console.
   ///     - $LogFileFmt=./logs/{0:f+'L'}/fon9sys-{1:04}.log  # 換日時換檔 {0:f+'L'}=YYYYMMDD(localtime), {1:04}=檔案序號.
   ///     - $LogFileSizeMB=n                                 # 超過 n MB 依序換檔: {1:04}
   ///   - $HostId     沒有預設值, 如果沒設定, 就不會設定 LocalHostId_
   ///   - $SyncerPath 指定 InnSyncerFile 的路徑, 預設 = "fon9syn"
   ///   - $MaAuthName 預設 "MaAuth": 並建立(開啟) this->ConfigPath_ + $MaAuthName + ".f9dbf" 儲存 this->MaAuth_ 之下的資料表.
   ///   - $MemLock    預設 "N"
   ///   - $MaPlugins  預設 "MaPlugins.f9gv": 儲存 「plugins設定」的檔案, 實際儲存位置為: this->ConfigPath_ + $MaPlugins
   int Initialize(int argc, const char* argv[]);
   /// 在 Initialize() 建立 sysEnv->Initialize(argc, argv); 之後呼叫; 預設 do nothing;
   /// \retval !=0 則中斷 this->Initialize(); 後續應該結束系統.
   virtual int OnAfterSysEnvInitialized(seed::SysEnv&);
   /// 在 Initialize() 建立 this->MaAuth_ 之後呼叫; 預設 do nothing;
   virtual void OnAfterMaAuth();
   /// 在 Initialize() 建立 this->MaPlugins_ 之後呼叫; 預設 do nothing;
   virtual void OnAfterMaPlugins();

   /// dbf.LoadAll(), syncer.StartSync(), ...
   void Start();

   /// syncer.StopSync(), dbf.Close(), root.OnParentSeedClear(), ...
   void Dispose();

   /// Dispose() 並且等候相關 thread 執行完畢.
   void DisposeForAppQuit();
};

/// 必須將 fon9/framework/Fon9CoRun.cpp 加入您的執行專案裡面, 在 int main() 呼叫 Fon9CoRun();
/// - 在 framework.Start() 之前會呼叫 fnBeforeStart(framework), 若傳回非0, 則立即結束 Fon9CoRun().
/// - 您可以在 fnBeforeStart() 啟動您自訂的物件, 例如:
///   - `f9omstw::OmsPoIvListAgent::Plant(*fon9sys.MaAuth_);`
int Fon9CoRun(int argc, const char* argv[], int (*fnBeforeStart)(Framework&));
/// - 必須已經呼叫過 fon9sys.Initialize(argc, argv);
/// - 執行 fnBeforeStart() 及 fon9sys.Start();
/// - 結束前呼叫 fon9sys.DisposeForAppQuit();
int Fon9SysCoStart(Framework& fon9sys, int argc, const char* argv[], int (*fnBeforeStart)(Framework&));
/// 預設加入:
///   fon9::auth::PlantScramSha256(*fon9sys.MaAuth_);
///   fon9::auth::PolicyAclAgent::Plant(*fon9sys.MaAuth_);
void PlantMaAuth_UserAgent(Framework& fon9sys);

} // namespaces
#endif//__fon9_framework_Framework_hpp__
