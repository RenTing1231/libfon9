/// \file fon9/rc/Rc_UT.cpp
/// \author fonwinz@gmail.com
#include "fon9/TestTools.hpp"
#include "fon9/io/SimpleManager.hpp"
#include "fon9/rc/RcSession.hpp"
#include "fon9/rc/RcFuncConn.hpp"

#include "fon9/framework/Framework.hpp"
#include "fon9/framework/IoManagerTree.hpp"
#include "fon9/auth/SaslScramSha256Server.hpp"
#include "fon9/DefaultThreadPool.hpp"

#define kUSERID      "user"
#define kPASSWORD    "pencil"
static const char kRcUT_Dbf_FileName[] = "RcUT.f9dbf";
//--------------------------------------------------------------------------//
class RcFuncHeartbeatRTT : public fon9::rc::RcFunctionAgent {
   fon9_NON_COPY_NON_MOVE(RcFuncHeartbeatRTT);
   using base = RcFunctionAgent;
public:
   RcFuncHeartbeatRTT() : base{f9rc_FunctionCode_Heartbeat} {
   }
   void OnRecvFunctionCall(fon9::rc::RcSession&, fon9::rc::RcFunctionParam& param) override {
      // 計算 RTT.
      fon9::TimeStamp tmRemoteAck;
      fon9::BitvTo(param.RecvBuffer_, tmRemoteAck);
      fon9::TimeStamp tmLocalSend;
      fon9::BitvTo(param.RecvBuffer_, tmLocalSend);
      fon9_LOG_INFO("RcFuncHeartbeat|rtt=", param.RecvTime_ - tmLocalSend,
                    "|RemoteTime=", tmRemoteAck);
   }
};
//--------------------------------------------------------------------------//
const f9rc_FunctionCode   RcFuncCode_Test = static_cast<f9rc_FunctionCode>(f9rc_FunctionCode_Count - 1);

class RcFuncTest : public fon9::rc::RcFunctionAgent {
   fon9_NON_COPY_NON_MOVE(RcFuncTest);
   using base = RcFunctionAgent;
public:
   RcFuncTest() : base{RcFuncCode_Test} {
   }
   void OnRecvFunctionCall(fon9::rc::RcSession&, fon9::rc::RcFunctionParam& param) override {
      std::string msg;
      fon9::BitvTo(param.RecvBuffer_, msg);
      fon9_LOG_INFO("RcFuncTest:Recv|msg=", msg);
   }
};
//--------------------------------------------------------------------------//
extern "C" fon9_API fon9::seed::PluginsDesc f9p_NamedIoManager;
extern "C" fon9_API fon9::seed::PluginsDesc f9p_TcpServer;
extern "C" fon9_API fon9::seed::PluginsDesc f9p_TcpClient;
extern "C" fon9_API fon9::seed::PluginsDesc f9p_RcSessionServer;
void* ForceLinkSomething() {
   static const void* forceLinkList[]{
      &f9p_NamedIoManager, &f9p_TcpServer, &f9p_TcpClient, &f9p_RcSessionServer
   };
   return forceLinkList;
}

struct RcTester : private fon9::Framework {
   RcTester() {
      ForceLinkSomething();
      this->Initialize();
   }
   ~RcTester() {
      this->DisposeForAppQuit();
      this->RemoveTestFiles();
   }
   void RemoveTestFiles() {
      ::remove(kRcUT_Dbf_FileName);
   }
private:
   void Initialize() {
      this->Root_.reset(new fon9::seed::MaTree{"RcUT"});
      // 開檔前先移除上次的測試檔, 避免遺留的資料影響測試結果.
      this->RemoveTestFiles();
      // AuthMgr 必須有一個「有效的 InnDbf」儲存資料, 否則無法運作.
      fon9::InnDbfSP authStorage{new fon9::InnDbf("userdbf", nullptr)};
      authStorage->Open(kRcUT_Dbf_FileName);
      // f9rc server 需要 AuthMgr 提供使用者驗證.
      this->MaAuth_ = fon9::auth::AuthMgr::Plant(this->Root_, authStorage, "AuthMgr");
      auto  userMgr = fon9::auth::PlantScramSha256(*this->MaAuth_);

      // 建立一個可登入的 kUSERID + kPASSWORD
      userMgr->GetSapling()->OnTreeOp([](const fon9::seed::TreeOpResult&, fon9::seed::TreeOp* opTree) {
         opTree->Add(kUSERID, [](const fon9::seed::PodOpResult& res, fon9::seed::PodOp* opPod) {
            auto tab = res.Sender_->LayoutSP_->GetTab(0);
            opPod->BeginWrite(*tab, [tab](const fon9::seed::SeedOpResult&, const fon9::seed::RawWr* wr) {
               tab->Fields_.Get("Flags")->PutNumber(*wr, 0, 0);
            });
            opPod->OnSeedCommand(tab, "repw " kPASSWORD, [](const fon9::seed::SeedOpResult&, fon9::StrView) {});
         });
      });
      // {  // 測試 user 是否有建立成功.
      //    fon9::auth::AuthResult authr{this->MaAuth_};
      //    authr.AuthcId_.assign("user");
      //    auto  lkuser = userMgr->GetLockedUser(authr);
      //    fon9_LOG_INFO("user=", lkuser.second ? "OK" : "ERR");
      //    // lkuser 物件自動死亡, 避免持續 lock.
      // }
      this->MaPlugins_.reset(new fon9::seed::PluginsMgr(this->Root_, "Plugins"));
      this->Root_->Add(this->MaPlugins_, "Plant.Plugins");
      #define _ "\x01"
      this->MaPlugins_->LoadConfigStr(
         "Id"      _ "Enabled" _ "FileName" _ "EntryName"       _ "Args\n"
         "DevTcpc" _ "Y"       _ ""         _ "TcpClient"       _ "Name=TcpClient|AddTo=FpDevice\n"
         "DevTcps" _ "Y"       _ ""         _ "TcpServer"       _ "Name=TcpServer|AddTo=FpDevice\n"
         "MaIo"    _ "Y"       _ ""         _ "NamedIoManager"  _ "Name=MaIo|DevFp=FpDevice|SesFp=FpSession|SvcCfg='ThreadCount=1|Capacity=100'\n"
         "RcSv"    _ "Y"       _ ""         _ "RcSessionServer" _ "Name=RcSv|AuthMgr=AuthMgr|AddTo=FpSession\n"
      );
      auto iomgr = this->Root_->GetSapling<fon9::IoManagerTree>("MaIo");
      auto sesfac = fon9::dynamic_pointer_cast<fon9::rc::RcFunctionMgr>(iomgr->SessionFactoryPark_->Get("RcSv"));
      sesfac->Reset(fon9::rc::RcFunctionAgentSP{new RcFuncTest});
      iomgr->LoadConfigStr(
         "Id"   _ "Enabled" _ "Sch" _ "Session" _ "SessionArgs" _ "Device"    _ "DeviceArgs\n"
         "RcSv" _ "Y"       _ ""    _ "RcSv"    _ ""            _ "TcpServer" _ "19999\n"
         , fon9::TimeInterval_Second(1)
      );
      #undef _
   }
};
//--------------------------------------------------------------------------//
#ifdef fon9_WINDOWS
#include "fon9/io/win/IocpTcpClient.hpp"
using IoService = fon9::io::IocpService;
using IoServiceSP = fon9::io::IocpServiceSP;
using TcpClient = fon9::io::IocpTcpClient;

#else
#include "fon9/io/FdrTcpClient.hpp"
#include "fon9/io/FdrServiceEpoll.hpp"
using IoService = fon9::io::FdrServiceEpoll;
using IoServiceSP = fon9::io::FdrServiceSP;
using TcpClient = fon9::io::FdrTcpClient;

#endif

struct ClientTester {
   struct RcSessionClient : public fon9::rc::RcSession {
      fon9_NON_COPY_NON_MOVE(RcSessionClient);
      using base = fon9::rc::RcSession;
      const std::string Password_;

      RcSessionClient(fon9::rc::RcFunctionMgrSP mgr, fon9::StrView userId, fon9::StrView password, f9rc_RcFlag rcflags)
         : base(std::move(mgr), fon9::rc::RcSessionRole::User, rcflags, userId)
         , Password_{password.ToString()} {
      }
      fon9::StrView GetAuthPassword() const override {
         return fon9::StrView{&this->Password_};
      }
      void OnDevice_StateChanged(fon9::io::Device& dev, const fon9::io::StateChangedArgs& e) override {
         fon9_LOG_INFO("RcSessionClient.OnDevice_StateChanged|devid=", e.After_.DeviceId_,
                       "|st=", fon9::io::GetStateStr(e.After_.State_), "|info=", e.After_.Info_);
         base::OnDevice_StateChanged(dev, e);
      }
   };

   struct RcFuncClientMgr : public fon9::rc::RcFunctionMgrRefCounter {
      fon9_NON_COPY_NON_MOVE(RcFuncClientMgr);
      RcFuncClientMgr() {
         this->Add(fon9::rc::RcFunctionAgentSP{new fon9::rc::RcFuncConnClient("f9rcCli.0", "fon9 RcClient Tester")});
         this->Add(fon9::rc::RcFunctionAgentSP{new fon9::rc::RcFuncSaslClient{}});
         this->Reset(fon9::rc::RcFunctionAgentSP{new RcFuncHeartbeatRTT{}});
      }
   };

   /// 如果有多個 RcCli_, 可以共用 IoSv_ 、 FuncClientMgr_;
   IoServiceSP                IoSv_;
   fon9::rc::RcFunctionMgrSP  FuncClientMgr_;
   RcSessionClient*           RcCli_;
   fon9::io::DeviceSP         Dev_;
   ClientTester(f9rc_RcFlag rcflag) : FuncClientMgr_{new RcFuncClientMgr} {
      fon9::io::IoServiceArgs argIoSv{};
      IoService::MakeResult   errIoSv;
      argIoSv.ThreadCount_ = 1;
      // 其他參數,例如: argIoSv.Capacity_ = 1;
      this->IoSv_ = IoService::MakeService(argIoSv, "RcUT_Cli", errIoSv);
      this->RcCli_ = new RcSessionClient(this->FuncClientMgr_, kUSERID, kPASSWORD, rcflag);
      this->Dev_ = new TcpClient(this->IoSv_, this->RcCli_, nullptr);
      this->Dev_->Initialize();
      this->Dev_->AsyncOpen("127.0.0.1:19999");
   }
   ~ClientTester() {
      this->Dev_->AsyncDispose("~ClientTester");
      this->IoSv_.reset();
   }
};
//--------------------------------------------------------------------------//
int main(int argc, const char** argv) {
   // 測試方法:
   // - 建立 RcServer: TcpServer + RcServerFactory
   // - 建立 RcClient: TcpClient + RcSession
   // - 測試項目: 連線、登入、Heartbeat
   // - 顯示 Heartbeat RTT
   #if defined(_MSC_VER) && defined(_DEBUG)
      _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
   #endif

   f9rc_RcFlag  rcflag{};
   for (int L = 1; L < argc; ++L) {
      if (strcmp(argv[L], "NoChecksum") == 0)
         rcflag |= f9rc_RcFlag_NoChecksum;
   }

   fon9::AutoPrintTestInfo utinfo("Rc_UT");
   fon9::GetDefaultThreadPool();
   fon9::GetDefaultTimerThread();

   RcTester       rcTester;
   ClientTester   cliTester{rcflag};

   char                 cmdln[1024*8];
   fon9::RevBufferList  rbuf{64};
   while (fgets(cmdln, sizeof(cmdln), stdin) != NULL) {
      fon9::StrView ln{fon9::StrView_cstr(cmdln)};
      if (fon9::StrTrim(&ln).empty()) {
         puts("\n" "quit, h:Send heartbeat, t:Test heartbeat timeout, o:Reopen, else:send test string.\n");
         continue;
      }
      if (ln == "quit")
         break;
      if (ln.size() == 1) {
         switch (ln.Get1st()) {
         case 't':
            puts("Waiting heartbeat timeout...");
            cliTester.RcCli_->GetDevice()->CommonTimerRunAfter(fon9::TimeInterval_Minute(2));
            continue;
         case 'o':
            cliTester.RcCli_->GetDevice()->AsyncOpen(std::string{});
            continue;
         case 'h':
            fon9::ToBitv(rbuf, fon9::UtcNow());
            cliTester.RcCli_->Send(f9rc_FunctionCode_Heartbeat, std::move(rbuf));
            continue;
         }
      }
      size_t count = 1;
      if (ln.Get1st() == '*') {
         ln.SetBegin(ln.begin() + 1);
         if ((count = fon9::StrTo(&ln, 0u)) <= 0)
            count = 1;
      }
      const size_t totsz = ln.size() * count;
      do {
         fon9::RevPrint(rbuf, ln);
      } while (--count > 0);
      fon9::ByteArraySizeToBitvT(rbuf, totsz);
      cliTester.RcCli_->Send(RcFuncCode_Test, std::move(rbuf));
   }
}
