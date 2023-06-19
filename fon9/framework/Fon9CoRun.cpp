// \file fon9/framework/Fon9CoRun.cpp
// fon9 console
// \author fonwinz@gmail.com
#include "fon9/framework/Framework.hpp"
#include "fon9/framework/SeedSession.hpp"
#include "fon9/auth/SaslScramSha256Server.hpp"
#include "fon9/auth/SaslClient.hpp"
#include "fon9/auth/PolicyAcl.hpp"
#include "fon9/auth/PoDupLogon.hpp"
#include "fon9/CountDownLatch.hpp"
#include "fon9/CmdArgs.hpp"
#include "fon9/Log.hpp"
#include "fon9/buffer/DcQueueList.hpp"
#include "fon9/CtrlBreakHandler.h"
#include "fon9/ConsoleIO.h"
//--------------------------------------------------------------------------//
// 若要提供 http 管理介面, 載入必要的 plugins, 底下的設定都是立即生效:
// 建立 TcpServer factory, 放在 DeviceFactoryPark=FpDevice
//    >ss,Enabled=Y,EntryName=TcpServer,Args='Name=TcpServer|AddTo=FpDevice' /MaPlugins/DevTcps
// 建立 Http request 靜態分配器, wwwroot/HttpStatic.cfg 可參考 fon9/web/HttpStatic.cfg:
//    >ss,Enabled=Y,EntryName=HttpStaticDispatcher,Args='Name=HttpRoot|Cfg=wwwroot/HttpStatic.cfg' /MaPlugins/Http-R
// 建立 Http session factory, 使用剛剛建立的 http 分配器(HttpRoot), 放在 SessionFactoryPark=FpSession
//    >ss,Enabled=Y,EntryName=HttpSession,Args='Name=HttpMa|HttpDispatcher=HttpRoot|AddTo=FpSession' /MaPlugins/HttpSes
// 建立 WsSeedVisitor(透過 web service 管理 fon9::seed), 使用 AuthMgr=MaAuth 檢核帳密權限, 放在剛剛建立的 http 分配器(HttpRoot)
//    >ss,Enabled=Y,EntryName=WsSeedVisitor,Args='Name=WsSeedVisitor|AuthMgr=MaAuth|AddTo=HttpRoot' /MaPlugins/HttpWsMa
// 建立 io 管理員, DeviceFactoryPark=FpDevice; SessionFactoryPark=FpSession; 設定儲存在 fon9cfg/MaIo.f9gv
//    >ss,Enabled=Y,EntryName=NamedIoManager,Args='Name=MaIo|DevFp=FpDevice|SesFp=FpSession|Cfg=MaIo.f9gv|SvcCfg="ThreadCount=2|Capacity=100"' /MaPlugins/MaIo
//
// 在 io 管理員(MaIo) 設定 HttpSession:
// 建立設定, 此時尚未套用(也尚未存檔):
//    >ss,Enabled=Y,Session=HttpMa,Device=TcpServer,DeviceArgs=6080 /MaIo/^edit:Config/MaHttp
// 查看設定及套用時的 SubmitId:
//    >gv /MaIo/^apply:Config
// 套用設定, 假設上一行指令的結果提示 SubmitId=123:
//    >/MaIo/^apply:Config submit 123
//--------------------------------------------------------------------------//

#ifdef __GNUC__
#include <mcheck.h>
static void prepare_mtrace(void) __attribute__((constructor));
static void prepare_mtrace(void) { mtrace(); }

// 在 int main() 的 cpp 裡面: 加上這個永遠不會被呼叫的 fn,
// 就可解決 g++: "Enable multithreading to use std::thread: Operation not permitted" 的問題了!
void FixBug_use_std_thread(pthread_t* thread, void *(*start_routine) (void *)) {
   pthread_create(thread, NULL, start_routine, NULL);
}
#endif

//--------------------------------------------------------------------------//

class ConsoleSeedSession : public fon9::SeedSession {
   fon9_NON_COPY_NON_MOVE(ConsoleSeedSession);
   using base = fon9::SeedSession;
   fon9::CountDownLatch Waiter_{1};

   void OnAuthEventInLocking(State, fon9::DcQueue&& msg) override {
      this->WriteToConsole(std::move(msg));
      this->Wakeup();
   }
   void OnRequestError(const fon9::seed::TicketRunner&, fon9::DcQueue&& errmsg) override {
      this->WriteToConsole(std::move(errmsg));
      this->Wakeup();
   }
   void OnRequestDone(const fon9::seed::TicketRunner&, fon9::DcQueue&& extmsg) override {
      if (!extmsg.empty())
         this->WriteToConsole(std::move(extmsg));
      this->Wakeup();
   }
   void OnSeedNotify(fon9::seed::VisitorSubr& subr, const fon9::seed::SeedNotifyArgs& args) override {
      fon9::RevBufferList rbuf{256};
      RevPutChar(rbuf, '\n');
      bool hasKeyTab = false;
      switch (args.NotifyKind_) {
      case fon9::seed::SeedNotifyKind::SubscribeOK:
         RevPrint(rbuf, "|SubscribeOK");
         break;
      case fon9::seed::SeedNotifyKind::PodRemoved:
         RevPrint(rbuf, "|PodRemoved");
         break;
      case fon9::seed::SeedNotifyKind::SeedChanged:
         RevPrint(rbuf, "|gv={", args.GetGridView(), '}');
         hasKeyTab = true;
         break;
      case fon9::seed::SeedNotifyKind::SeedRemoved:
         RevPrint(rbuf, "|SeedRemoved");
         hasKeyTab = true;
         break;
      case fon9::seed::SeedNotifyKind::ParentSeedClear:
         RevPrint(rbuf, "|OnParentSeedClear");
         break;
      case fon9::seed::SeedNotifyKind::TableChanged:
         RevPrint(rbuf, "/^", args.Tab_->Name_, "|TableChanged");
         break;
      case fon9::seed::SeedNotifyKind::SubscribeStreamOK:
         RevPrint(rbuf, "|SubscribeStreamOK");
         break;
      case fon9::seed::SeedNotifyKind::StreamData:
         RevPrint(rbuf, "|StreamData.Kind=", fon9::ToHex(args.StreamDataKind_));
         hasKeyTab = true;
         break;
      case fon9::seed::SeedNotifyKind::StreamRecover:
         RevPrint(rbuf, "|StreamRecover.Kind=", fon9::ToHex(args.StreamDataKind_));
         hasKeyTab = true;
         break;
      case fon9::seed::SeedNotifyKind::StreamRecoverEnd:
         RevPrint(rbuf, "|StreamRecoverEnd.Kind=", fon9::ToHex(args.StreamDataKind_));
         hasKeyTab = true;
         break;
      case fon9::seed::SeedNotifyKind::StreamEnd:
         RevPrint(rbuf, "|StreamEnd.Kind=", fon9::ToHex(args.StreamDataKind_));
         hasKeyTab = true;
         break;
      }
      if (hasKeyTab) {
         if (args.Tab_)
            RevPrint(rbuf, '^', args.Tab_->Name_);
         RevPrint(rbuf, '/', args.KeyText_);
      }
      RevPrint(rbuf, "\n" "SeedNotify|path=", subr.GetPath());
      this->WriteToConsole(fon9::DcQueueList{rbuf.MoveOut()});
   }

   void PutNewLineConsole() {
      fputs("\n", stdout);
   }
   void WritePrompt(fon9::StrView msg) {
      fwrite(msg.begin(), msg.size(), 1, stdout);
      fflush(stdout);
   }
   void WriteToConsole(fon9::DcQueue&& errmsg) {
      fon9::DeviceOutputBlock(std::move(errmsg), [](const void* buf, size_t sz) {
         fwrite(buf, sz, 1, stdout);
         return fon9::Outcome<size_t>{sz};
      });
      this->PutNewLineConsole();
   }

   uint16_t GetDefaultGridViewRowCount() override {
      fon9_winsize wsz;
      fon9_GetConsoleSize(&wsz);
      return static_cast<uint16_t>(wsz.ws_row);
   }

   State RunLoopImpl() {
      char        cmdbuf[1024 * 4];
      std::string prompt;
      for (;;) {
         this->GetPrompt(prompt);
         this->WritePrompt(&prompt);
         // TODO: fgets() 可以考慮使用 gnu readline library.
         //       Windows 的 fgets() 無法處理 utf8 字串!
         if (!fgets(cmdbuf, sizeof(cmdbuf), stdin))
             return State::Broken;
         fon9::StrView cmdln{fon9::StrView_cstr(cmdbuf)};
         if (StrTrim(&cmdln).empty())
            continue;
         if (cmdln.Get1st() == '#')
            continue;
         if (cmdln == "quit") {
            fon9_LOG_IMP("main.quit|user=", this->GetAuthr().AuthcId_);
            return State::QuitApp;
         }
         if (cmdln == "exit")
            return State::UserExit;
         this->FeedLine(cmdln);
         this->Wait();
      }
   }

public:
   std::string ConsoleId_;
   ConsoleSeedSession(fon9::Framework& fon9sys)
      : base(fon9sys.Root_, fon9sys.MaAuth_,
             fon9::RevPrintTo<std::string>("console:", fon9::LocalHostId_))
      , ConsoleId_{GetUFrom()} {
   }

   void Wakeup() {
      this->Waiter_.ForceWakeUp();
   }
   void Wait() {
      this->Waiter_.Wait();
      this->Waiter_.AddCounter(1);
   }

   State ReadUser() {
      this->WritePrompt("\n" "Fon9Co login: ");
      char  userbuf[256];
      if (!fgets(userbuf, sizeof(userbuf), stdin))
         return State::Broken;
      fon9::StrView  authc{fon9::StrView_cstr(userbuf)};
      if (fon9::StrTrim(&authc).empty())
         return State::UserExit;

      char     passbuf[1024];
      size_t   passlen = fon9_getpass(stdout, "Password: ", passbuf, sizeof(passbuf));
      this->PutNewLineConsole();
      fon9::StrView  authz{};
      return this->AuthUser(fon9::StrTrim(&authz), authc, fon9::StrView{passbuf, passlen}, fon9::ToStrView(this->ConsoleId_));
   }

   State RunLoop() {
      this->SetCurrPath(ToStrView(this->Fairy_->GetCurrPath()));
      State st = this->RunLoopImpl();
      this->Logout();
      return st;
   }
};

//--------------------------------------------------------------------------//

int fon9::Fon9CoRun(int argc, const char* argv[], int (*fnBeforeStart)(fon9::Framework&)) {
#if defined(_MSC_VER) && defined(_DEBUG)
   _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
   //_CrtSetBreakAlloc(176);
#endif

#if defined(NDEBUG)
   fon9::LogLevel_ = fon9::LogLevel::Info;
#endif

   fon9_SetConsoleUTF8();
   fon9_SetupCtrlBreakHandler();

   fon9::Framework   fon9sys;
   fon9sys.Initialize(argc, argv);

   // PlantScramSha256(), PolicyAclAgent::Plant() 要用 plugins 機制嗎?
   fon9::PlantMaAuth_UserAgent(fon9sys);

   return Fon9SysCoStart(fon9sys, argc, argv, fnBeforeStart);
}

void fon9::PlantMaAuth_UserAgent(Framework& fon9sys) {
   fon9::auth::PlantScramSha256(*fon9sys.MaAuth_);
   fon9::auth::PolicyAclAgent::Plant(*fon9sys.MaAuth_);
   fon9::auth::PoDupLogonAgent::Plant(*fon9sys.MaAuth_);
}

int fon9::Fon9SysCoStart(fon9::Framework& fon9sys, int argc, const char* argv[], int (*fnBeforeStart)(fon9::Framework&)) {
   int retval = fnBeforeStart ? fnBeforeStart(fon9sys) : 0;
   if (retval)
      fon9_AppBreakMsg = "Before fon9sys.Start()";
   else {
      fon9sys.Start();

      using CoSeedSessionSP = fon9::intrusive_ptr<ConsoleSeedSession>;
      CoSeedSessionSP           coSession{new ConsoleSeedSession{fon9sys}};
      fon9::SeedSession::State  res = ConsoleSeedSession::State::UserExit;

      // 使用 "--admin" 啟動 AdminMode.
      if (!fon9::GetCmdArg(argc, argv, fon9::StrView{}, "admin").IsNull()) {
         coSession->SetAdminMode();
         puts("Fon9Co admin mode.");
         res = coSession->RunLoop();
      }

      while (fon9_AppBreakMsg == nullptr) {
         switch (res) {
         case ConsoleSeedSession::State::None:
         case ConsoleSeedSession::State::AuthError:
         case ConsoleSeedSession::State::UserExit:
            res = coSession->ReadUser();
            break;
         case ConsoleSeedSession::State::UserReady:
            res = coSession->RunLoop();
            break;
         case ConsoleSeedSession::State::QuitApp:
            fon9_AppBreakMsg = "Normal QuitApp";
            break;
         default:
         case ConsoleSeedSession::State::Authing:
         case ConsoleSeedSession::State::Logouting:
            coSession->Wait();
            res = coSession->GetState();
            break;
         case ConsoleSeedSession::State::Broken:
            // stdin EOF, sleep() and retry read.
            // wait signal for quit app.
            // 一旦遇到 EOF, 就需要重新登入?
            int c;
            do {
               std::this_thread::sleep_for(std::chrono::milliseconds(500));
               clearerr(stdin);
               c = fgetc(stdin);
            } while (c == EOF && fon9_AppBreakMsg == nullptr);
            ungetc(c, stdin);
            res = coSession->GetState();
            continue;
         }
      }
   }
   fon9_LOG_IMP("main.quit|cause=console:", fon9_AppBreakMsg);
   printf("fon9: %s\n", fon9_AppBreakMsg);
   fon9_AppBreakMsg = nullptr;
   fon9sys.DisposeForAppQuit();
   return retval;
}
