// \file fon9/rc/RcClientSession.cpp
// \author fonwinz@gmail.com
#include "fon9/rc/RcClientSession.hpp"
#include "fon9/rc/RcFuncConn.hpp"
#include "fon9/Log.hpp"

fon9_IoManager::fon9_IoManager(const fon9::IoManagerArgs& ioMgrArgs) : baseIoMgr{ioMgrArgs} {
   this->DeviceFactoryPark_->Add(fon9::MakeIoFactoryTcpClient("TcpClient"));
   this->DeviceFactoryPark_->Add(fon9::MakeIoFactoryDgram("Dgram"));
}
unsigned fon9_IoManager::IoManagerAddRef() {
   return intrusive_ptr_add_ref(this);
}
unsigned fon9_IoManager::IoManagerRelease() {
   return intrusive_ptr_release(this);
}
void fon9_IoManager::NotifyChanged(baseIoMgr::DeviceRun&) {
}
void fon9_IoManager::NotifyChanged(baseIoMgr::DeviceItem&) {
}

bool fon9_IoManager::AddSessionAndOpen(baseIoMgr::DeviceItemSP item) {
   if (!DeviceMap::Locker{this->DeviceMap_}->insert(item).second)
      return false;
   item->Device_->AsyncOpen(item->Config_.DeviceArgs_.ToString());
   return true;
}
fon9_IoManager::DeviceItemSP fon9_IoManager::CreateSession(const fon9_IoSessionParams& params) {
   static std::atomic<uint32_t>  id{0};
   fon9::NumOutBuf nbuf;
   char* pout = nbuf.end();
   *--pout = '_';
   pout = fon9::HexToStrRev(pout, ++id);
   *--pout = '_';
   DeviceItemSP    item{new DeviceItem{fon9::StrView{pout, nbuf.end()}}};
   item->Config_.Enabled_ = fon9::EnabledYN::Yes;
   item->Config_.DeviceName_.assign(fon9::StrView_cstr(params.DevName_));
   item->Config_.DeviceArgs_.assign(fon9::StrView_cstr(params.DevParams_));
   item->Config_.SessionName_.assign(fon9::StrView_cstr(params.SesName_));
   item->Config_.SessionArgs_.assign(fon9::StrView_cstr(params.SesParams_));
   this->CheckCreateDevice(*item);
   return(item->Device_ ? item : nullptr);
}
void fon9_IoManager::DestroyDevice(fon9::io::DeviceSP dev, std::string cause, int isWait) {
   dev->AsyncDispose(std::move(cause));
   if (isWait) {
      dev->WaitGetDeviceId();
      while (dev->use_count() > 1)
         std::this_thread::yield();
   }
}

//--------------------------------------------------------------------------//

namespace fon9 { namespace rc {

RcClientSessionEvHandler::~RcClientSessionEvHandler() {
}

RcClientSession::RcClientSession(RcFunctionMgrSP funcMgr, const f9rc_ClientSessionParams* params)
   : base(std::move(funcMgr), RcSessionRole::User, params->RcFlags_, StrView_cstr(params->UserId_))
   , Password_{params->Password_}
   , FnOnLinkEv_(params->FnOnLinkEv_) {
   this->UserData_ = params->UserData_;
   this->LogFlags_ = params->LogFlags_;
   for (unsigned L = 0; L < f9rc_FunctionCode_Count; ++L) {
      if (auto agent = dynamic_cast<RcClientFunctionAgent*>(this->FunctionMgr_->Get(static_cast<f9rc_FunctionCode>(L))))
         agent->OnSessionCtor(*this, params);
   }
}
StrView RcClientSession::GetAuthPassword() const {
   return StrView{&this->Password_};
}

static inline bool EvCaller_BeforeLinkEv(RcClientSessionEvHandler* handler, RcClientSession* pthis) {
   handler->OnRcClientSession_BeforeLinkEv(*pthis);
   return true;
}
void RcClientSession::OnDevice_StateChanged(io::Device& dev, const io::StateChangedArgs& e) {
   this->EvHandlers_.Combine(EvCaller_BeforeLinkEv, this);
   base::OnDevice_StateChanged(dev, e);
}

static inline bool EvCaller_ApReady(RcClientSessionEvHandler* handler, RcClientSession* pthis) {
   handler->OnRcClientSession_ApReady(*pthis);
   return true;
}
void RcClientSession::SetApReady(StrView info) {
   base::SetApReady(info);
   this->EvHandlers_.Combine(EvCaller_ApReady, this);
}
//--------------------------------------------------------------------------//
RcClientFunctionAgent::~RcClientFunctionAgent() {
}
void RcClientFunctionAgent::OnSessionCtor(RcClientSession&, const f9rc_ClientSessionParams*) {
}
//--------------------------------------------------------------------------//
RcClientMgr::RcClientSessionFactory::RcClientSessionFactory() : SessionFactory{"RcClientSession"} {
}
io::SessionSP RcClientMgr::RcClientSessionFactory::CreateSession(IoManager& mgr, const IoConfigItem& iocfg, std::string& errReason) {
   (void)errReason;
   return new RcClientSession(static_cast<RcClientMgr*>(&mgr), static_cast<const RcClientConfigItem*>(&iocfg)->Params_);
}
io::SessionServerSP RcClientMgr::RcClientSessionFactory::CreateSessionServer(IoManager&, const IoConfigItem&, std::string& errReason) {
   errReason = "RcClientSession: Not support server.";
   return nullptr;
}
//--------------------------------------------------------------------------//
RcClientMgr::RcClientMgr(const IoManagerArgs& ioMgrArgs) : base{ioMgrArgs} {
   this->Add(RcFunctionAgentSP{new RcFuncConnClient("f9rcAPI.0", "f9rc RcClient API")});
   this->Add(RcFunctionAgentSP{new RcFuncSaslClient{}});
}
int RcClientMgr::CreateRcClientSession(f9rc_ClientSession** result, const RcClientConfigItem& cfg) {
   auto fac = this->DeviceFactoryPark_->Get(StrView_cstr(cfg.Params_->DevName_));
   if (!fac)
      return false;
   std::string errReason;
   auto dev = fac->CreateDevice(this, this->RcClientSessionFactory_, cfg, errReason);
   if (result)
      *result = static_cast<RcClientSession*>(dev->Session_.get());
   dev->Initialize();
   dev->AsyncOpen(cfg.Params_->DevParams_);
   dev.detach(); // 配合在 this->DestroyRcClientSession() 裡面的 dev 使用 add_ref = false;
   return true;
}
void RcClientMgr::DestroyRcClientSession(RcClientSession* ses, int isWait) {
   // add_ref = false: 配合在 CreateRcClientSession(); 裡面有呼叫 dev.detach();
   base::DestroyDevice(io::DeviceSP(ses->GetDevice(), false), "DestroyRcClientSession", isWait);
}
static void RcClientMgr_LogDevSt(StrView head, io::Device& dev, const io::StateUpdatedArgs& e) {
   assert(dynamic_cast<RcClientSession*>(dev.Session_.get()) != nullptr);
   auto* ses = static_cast<RcClientSession*>(dev.Session_.get());
   if (ses->LogFlags_ & f9rc_ClientLogFlag_Link)
      fon9_LOG_INFO(static_cast<RcClientMgr*>(dev.Manager_.get())->Name_, head,
                    "|ses=", ToPtr(static_cast<f9rc_ClientSession*>(ses)),
                    "|dev=", ToPtr(&dev),
                    "|st=", io::GetStateStr(e.State_),
                    "|devid=", e.DeviceId_,
                    "|info=", e.Info_);
   if (ses->FnOnLinkEv_)
      ses->FnOnLinkEv_(ses, static_cast<f9io_State>(e.State_), e.Info_.ToCStrView());
}
void RcClientMgr::OnDevice_StateChanged(io::Device& dev, const io::StateChangedArgs& e) {
   RcClientMgr_LogDevSt(".Rc.OnDeviceSt", dev, e.After_);
}
void RcClientMgr::OnDevice_StateUpdated(io::Device& dev, const io::StateUpdatedArgs& e) {
   RcClientMgr_LogDevSt(".Rc.OnDeviceStU", dev, e);
}
void RcClientMgr::OnSession_StateUpdated(io::Device& dev, StrView stmsg, LogLevel) {
   assert(dynamic_cast<RcClientSession*>(dev.Session_.get()) != nullptr);
   auto* ses = static_cast<RcClientSession*>(dev.Session_.get());
   if (ses->GetSessionSt() == RcSessionSt::ApReady && ses->LogFlags_ & f9rc_ClientLogFlag_Link)
      fon9_LOG_INFO(this->Name_, ".Rc.ApReady"
                    "|ses=", ToPtr(static_cast<f9rc_ClientSession*>(ses)),
                    "|dev=", ToPtr(ses->GetDevice()),
                    stmsg);
}

} } // namespace
