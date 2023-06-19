/// \file fon9/framework/IoManagerTree.cpp
/// \author fonwinz@gmail.com
#include "fon9/framework/IoManagerTree.hpp"
#include "fon9/framework/FrameworkFlag.hpp"
#include "fon9/seed/TabTreeOp.hpp"
#include "fon9/seed/ConfigGridView.hpp"

namespace fon9 {

seed::LayoutSP IoManagerTree::MakeLayoutImpl() {
   seed::LayoutSP saplingLayout = IoManager::GetAcceptedClientLayout();
   seed::Fields   fields;
   fields.Add(fon9_MakeField(DeviceItem,  Config_.SessionName_, "Session"));
   fields.Add(fon9_MakeField2(DeviceItem, SessionSt));
   fields.Add(fon9_MakeField(DeviceItem,  Config_.DeviceName_,  "Device"));
   fields.Add(fon9_MakeField2(DeviceItem, DeviceSt));
   fields.Add(fon9_MakeField2(DeviceItem, OpenArgs));
   seed::TabSP tabSt{new seed::Tab(Named{"Status"}, std::move(fields), saplingLayout)};

   fields.Add(fon9_MakeField(DeviceItem, Config_.Enabled_,     "Enabled"));
   fields.Add(fon9_MakeField(DeviceItem, Config_.SchArgs_,     "Sch"));
   fields.Add(fon9_MakeField(DeviceItem, Config_.SessionName_, "Session"));
   fields.Add(fon9_MakeField(DeviceItem, Config_.SessionArgs_, "SessionArgs"));
   fields.Add(fon9_MakeField(DeviceItem, Config_.DeviceName_,  "Device"));
   fields.Add(fon9_MakeField(DeviceItem, Config_.DeviceArgs_,  "DeviceArgs"));
   seed::TabSP tabConfig{new seed::Tab(Named{"Config"}, std::move(fields), saplingLayout, seed::TabFlag::NeedsApply)};

   #define kTabStatusIndex  0
   #define kTabConfigIndex  1
   return new seed::LayoutN(fon9_MakeField2(DeviceItem, Id),
                            std::move(tabSt),
                            std::move(tabConfig));
}
seed::LayoutSP IoManagerTree::GetLayout() {
   static seed::LayoutSP Layout_{MakeLayoutImpl()};
   return Layout_;
}
//--------------------------------------------------------------------------//
fon9_WARN_DISABLE_PADDING;
fon9_MSC_WARN_DISABLE_NO_PUSH(4355 /* 'this' : used in base member initializer list*/);
IoManagerTree::IoManagerTree(const IoManagerArgs& args, TimeInterval afterOpen)
   : baseTree{IoManagerTree::GetLayout()}
   , baseIoMgr(args)
   , TabTreeOp_{new seed::TabTreeOp(*this->LayoutSP_->GetTab(kTabConfigIndex))} {
   this->SubConnDev_ = this->DeviceFactoryPark_->Subscribe([this](DeviceFactory*, seed::ParkTree::EventType) {
      this->OnFactoryParkChanged();
   });
   this->SubConnSes_ = this->SessionFactoryPark_->Subscribe([this](SessionFactory*, seed::ParkTree::EventType) {
      this->OnFactoryParkChanged();
   });
   if (args.CfgFileName_.empty())
      args.Result_.clear();
   else
      args.Result_ = this->BindConfigFile(args.CfgFileName_, afterOpen);
}
IoManagerTree::~IoManagerTree() {
   this->DeviceFactoryPark_->Unsubscribe(this->SubConnDev_);
   this->SessionFactoryPark_->Unsubscribe(this->SubConnSes_);
   this->Timer_.DisposeAndWait();
}
unsigned IoManagerTree::IoManagerAddRef() {
   return intrusive_ptr_add_ref(static_cast<baseTree*>(this));
}
unsigned IoManagerTree::IoManagerRelease() {
   return intrusive_ptr_release(static_cast<baseTree*>(this));
}
void IoManagerTree::OnFactoryParkChanged() {
   this->Timer_.RunAfter(TimeInterval_Second(1));
}

std::string IoManagerTree::BindConfigFile(std::string cfgfn, TimeInterval afterOpen) {
   std::string logHeader = "IoManager." + this->Name_;
   std::string retval = this->ConfigFileBinder_.OpenRead(&logHeader, cfgfn);
   if (retval.empty())
      this->LoadConfigStr(&this->ConfigFileBinder_.GetConfigStr(), afterOpen);
   return retval;
}
std::string IoManagerTree::LoadConfigStr(StrView cfgstr, TimeInterval afterOpen) {
   struct ToContainer : public seed::GridViewToContainer {
      DeviceMapImpl* Container_;
      ToContainer(DeviceMapImpl* container) : Container_{container} {
      }
      void OnNewRow(StrView keyText, StrView ln) override {
         DeviceMapImpl::value_type& item = *this->Container_->insert(new DeviceItem{keyText}).first;
         this->FillRaw(seed::SimpleRawWr{*item}, ln);
         item->Sch_.Parse(ToStrView(item->Config_.SchArgs_));
      }
   };
   DeviceMap::Locker curmap{this->DeviceMap_};
   ToContainer       toContainer{curmap.get()};
   toContainer.ParseConfigStr(this->LayoutSP_->GetTab(kTabConfigIndex)->Fields_, cfgstr);
   if (!afterOpen.IsNull())
      this->StartTimerForOpen(afterOpen);
   return std::string{};
}
void IoManagerTree::StartTimerForOpen(TimeInterval afterOpen) {
   this->Timer_.RunAfter(afterOpen);
   this->TimerFor_ = TimerFor::Open;
}
void IoManagerTree::EmitOnTimer(TimerEntry* timer, TimeStamp now) {
   IoManagerTree&    rthis = ContainerOf(*static_cast<Timer*>(timer), &IoManagerTree::Timer_);
   TimeStamp         nextCheckTime = TimeStamp::Null();
   DeviceMap::Locker curmap{rthis.DeviceMap_};
   for (auto& item : *curmap) {
      bool isStMessageChanged = false;
      switch (rthis.TimerFor_) {
      case TimerFor::Open:
         if (IsFrameworkInitializing) {
            // 系統還在啟動中, 晚一點再開啟, 避免有些資源(例: MaAuth)尚未設定完成.
            timer->RunAfter(TimeInterval_Second(1));
            return;
         }
         if (item->Config_.Enabled_ != EnabledYN::Yes) {
            isStMessageChanged = !item->AsyncDisposeDevice(now, "Disabled");
            break;
         }
         /* fall through */ // 檢查 item 是否在時間排程內.
      case TimerFor::CheckSch:
      default:
         if (item->Config_.Enabled_ != EnabledYN::Yes)
            break;
         // 必須是 Enabled 才需檢查排程.
         auto res = item->Sch_.Check(now, item->SchSt_);
         if (!res.NextCheckTime_.IsNullOrZero()) {
            if (nextCheckTime.IsNull() || nextCheckTime > res.NextCheckTime_)
               nextCheckTime = res.NextCheckTime_;
         }
         if (item->SchSt_ != res.SchSt_)
            item->SchSt_ = res.SchSt_;
         else {
            // 在 TimerFor::CheckSch 且 SchSt 相同 => 不考慮 item 是否需要關閉或啟動.
            if (rthis.TimerFor_ == TimerFor::CheckSch)
               break;
            // 如果是 TimerFor::Open, 即使 SchSt 相同 => 也要依照 SchSt 決定關閉或啟動 item.
         }

         if (res.SchSt_ == SchSt::InSch) {
            rthis.CheckOpenDevice(*item);
            if (!item->Device_) {
               // 開啟失敗: Factory 不正確? Factory.CreateDevice() 失敗?
               // 設為 SchSt::Unknown, 讓下次檢查 sch 時, 再 CheckOpenDevice() 一次.
               item->SchSt_ = SchSt::Unknown;
            }
            isStMessageChanged = true;
         }
         else {
            isStMessageChanged = !item->AsyncDisposeDevice(now, "Out of schedule");
         }
         break;
      }
      if (isStMessageChanged)
         rthis.NotifyChanged(*item);
   }
   // 必須在 locked 狀態下啟動 timer,
   // 因為如果此時其他 thread 設定 TimerFor::Open; timer->RunAfter();
   // 然後才執行下面這行, 那時間就錯了!
   if (!nextCheckTime.IsNull()) {
      rthis.TimerFor_ = TimerFor::CheckSch;
      timer->RunAt(nextCheckTime);
   }
}
void IoManagerTree::NotifyChanged(DeviceItem& item) {
   seed::SeedSubj_Notify(this->StatusSubj_, *this, *this->LayoutSP_->GetTab(kTabStatusIndex), ToStrView(item.Id_), item);
}
void IoManagerTree::NotifyChanged(DeviceRun& item) {
   if (item.IsDeviceItem())
      this->NotifyChanged(*static_cast<DeviceItem*>(&item));
}

void IoManagerTree::DisposeAndReopen(std::string cause, TimeInterval afterReopen) {
   this->DisposeDevices(std::move(cause));
   this->Timer_.RunAfter(afterReopen);
   this->TimerFor_ = TimerFor::Open;
}
void IoManagerTree::OnParentSeedClear() {
   DeviceMap::Locker map{this->DeviceMap_};
   this->LockedDisposeDevices(map, "Parent clear");
   SeedSubj_ParentSeedClear(this->StatusSubj_, *this);
}
//--------------------------------------------------------------------------//
struct IoManagerTree::PodOp : public seed::PodOpLockerNoWrite<PodOp, DeviceMap::Locker> {
   fon9_NON_COPY_NON_MOVE(PodOp);
   using base = seed::PodOpLockerNoWrite<PodOp, DeviceMap::Locker>;
   DeviceItemSP Seed_;
   PodOp(DeviceItem& seed, Tree& sender, seed::OpResult res, DeviceMap::Locker& locker)
      : base{*this, sender, res, ToStrView(seed.Id_), locker}
      , Seed_{&seed} {
   }
   DeviceItem& GetSeedRW(seed::Tab&) {
      return *this->Seed_;
   }
   seed::TreeSP HandleGetSapling(seed::Tab&) {
      return this->Seed_->Sapling_;
   }
   void HandleSeedCommand(DeviceMap::Locker& locker, SeedOpResult& res, StrView cmdln, seed::FnCommandResultHandler&& resHandler) {
      res.OpResult_ = seed::OpResult::no_error;
      io::DeviceSP dev = this->Seed_->Device_;
      if (dev) {
         locker.unlock();
         std::string msg = dev->DeviceCommand(cmdln);
         resHandler(res, &msg);
         return;
      }
      StrView cmd = StrFetchTrim(cmdln, &isspace);
      if (cmd == "open") {
         if (static_cast<IoManagerTree*>(this->Sender_)->CreateDevice(*this->Seed_) == DeviceOpenResult::NewDeviceCreated) {
            if (cmdln.empty())
               cmdln = ToStrView(this->Seed_->Config_.DeviceArgs_);
            AssignStStr(this->Seed_->DeviceSt_, UtcNow(), "Async opening by command.");
            this->Seed_->Device_->AsyncOpen(cmdln.ToString());
         }
         auto msg = this->Seed_->DeviceSt_;
         locker.unlock();
         resHandler(res, ToStrView(msg));
         return;
      }
      locker.unlock();
      if (cmd == "?") {
         res.OpResult_ = seed::OpResult::no_error;
         resHandler(res,
                    "open" fon9_kCSTR_CELLSPL "Open device" fon9_kCSTR_CELLSPL "[ConfigString] or Default config.");
      }
      else {
         res.OpResult_ = seed::OpResult::not_supported_cmd;
         resHandler(res, cmdln);
      }
   }
};
struct IoManagerTree::TreeOp : public seed::TreeOp {
   fon9_NON_COPY_NON_MOVE(TreeOp);
   using base = seed::TreeOp;
   TreeOp(Tree& tree) : base(tree) {
   }
   static void MakePolicyRecordView(DeviceMapImpl::iterator ivalue, seed::Tab* tab, RevBuffer& rbuf) {
      if (tab)
         FieldsCellRevPrint(tab->Fields_, seed::SimpleRawRd{**ivalue}, rbuf, seed::GridViewResult::kCellSplitter);
      RevPrint(rbuf, (*ivalue)->Id_);
   }
   void GridView(const seed::GridViewRequest& req, seed::FnGridViewOp fnCallback) override {
      seed::GridViewResult res{this->Tree_, req.Tab_};
      {
         DeviceMap::Locker  map{static_cast<IoManagerTree*>(&this->Tree_)->DeviceMap_};
         seed::MakeGridView(*map, seed::GetIteratorForGv(*map, req.OrigKey_),
                            req, res, &MakePolicyRecordView);
      } // unlock.
      fnCallback(res);
   }
   void Get(StrView strKeyText, seed::FnPodOp fnCallback) override {
      {
         DeviceMap::Locker  map{static_cast<IoManagerTree*>(&this->Tree_)->DeviceMap_};
         auto               ifind = seed::GetIteratorForPod(*map, strKeyText);
         if (ifind != map->end()) {
            PodOp op(**ifind, *static_cast<IoManagerTree*>(&this->Tree_), seed::OpResult::no_error, map);
            fnCallback(op, &op);
            return;
         }
      } // unlock.
      fnCallback(seed::PodOpResult{this->Tree_, seed::OpResult::not_found_key, strKeyText}, nullptr);
   }
   void GridApplySubmit(const seed::GridApplySubmitRequest& req, seed::FnCommandResultHandler fnCallback) override {
      if (!req.Tab_ || req.Tab_->GetIndex() != kTabConfigIndex)
         return base::GridApplySubmit(req, std::move(fnCallback));

      StrView              resmsg;
      seed::SeedOpResult   res{this->Tree_, seed::OpResult::no_error, seed::kTabTree_KeyApply, req.Tab_};
      seed::GridViewResult gvres{this->Tree_, req.Tab_};
      DeviceMapImpl        oldmap;
      {  // lock curmap.
         DeviceMap::Locker curmap{static_cast<IoManagerTree*>(&this->Tree_)->DeviceMap_};
         if (!req.BeforeGrid_.IsNull()) {
            seed::MakeGridView(*curmap, curmap->begin(), seed::GridViewRequestFull{*req.Tab_}, gvres, &MakePolicyRecordView);
            if (req.BeforeGrid_ != ToStrView(gvres.GridView_)) {
               res.OpResult_ = seed::OpResult::bad_apply_submit;
               resmsg = "Orig tree changed";
               goto __UNLOCK_AND_CALLBACK;
            }
         }
         IoManagerTree::Apply(req.Tab_->Fields_, req.EditingGrid_, *curmap, oldmap);
         seed::SeedSubj_TableChanged(static_cast<IoManagerTree*>(&this->Tree_)->StatusSubj_,
                                     this->Tree_, *this->Tree_.LayoutSP_->GetTab(kTabStatusIndex));
      }  // unlock curmap.
      // After grid view apply. oldmap = removed items.
      if (!oldmap.empty()) {
         TimeStamp now = UtcNow();
         for (auto& i : oldmap) {
            i->AsyncDisposeDevice(now, "Removed from ApplySubmit");
            // StatusSubj_ 使用 [整表] 通知: seed::SeedSubj_TableChanged();
            // 所以這裡就不用觸發 PodRemoved() 事件了.
            // seed::SeedSubj_NotifyPodRemoved(static_cast<IoManagerTree*>(&this->Tree_)->StatusSubj_, this->Tree_, ToStrView(i->Id_));
         }
      }
      if (static_cast<IoManagerTree*>(&this->Tree_)->ConfigFileBinder_.HasBinding()) {
         RevBufferList rbuf{128};
         RevPutChar(rbuf, *fon9_kCSTR_ROWSPL);
         seed::RevPrintConfigFieldNames(rbuf, req.Tab_->Fields_, &this->Tree_.LayoutSP_->KeyField_->Name_);
         static_cast<IoManagerTree*>(&this->Tree_)->ConfigFileBinder_.WriteConfig(
            ToStrView("IoManager." + static_cast<IoManagerTree*>(&this->Tree_)->Name_),
            BufferTo<std::string>(rbuf.MoveOut()) + req.EditingGrid_.ToString());
      }
      static_cast<IoManagerTree*>(&this->Tree_)->StartTimerForOpen(TimeInterval_Second(1));

   __UNLOCK_AND_CALLBACK:
      fnCallback(res, resmsg);
   }
   seed::OpResult Subscribe(SubConn* pSubConn, seed::Tab& tab, seed::FnSeedSubr subr) override {
      if (&tab != this->Tree_.LayoutSP_->GetTab(kTabStatusIndex))
         return seed::SubscribeUnsupported(pSubConn);
      return static_cast<IoManagerTree*>(&this->Tree_)->StatusSubj_.SafeSubscribe(
         DeviceMap::Locker{static_cast<IoManagerTree*>(&this->Tree_)->DeviceMap_},
         *this, pSubConn, tab, subr);
   }
   seed::OpResult Unsubscribe(SubConn* pSubConn, seed::Tab& tab) override {
      if (&tab != this->Tree_.LayoutSP_->GetTab(kTabStatusIndex))
         return seed::OpResult::not_supported_subscribe;
      return static_cast<IoManagerTree*>(&this->Tree_)->StatusSubj_.SafeUnsubscribe(
         DeviceMap::Locker{static_cast<IoManagerTree*>(&this->Tree_)->DeviceMap_}, pSubConn);
   }
};
fon9_WARN_POP;

void IoManagerTree::OnTreeOp(seed::FnTreeOp fnCallback) {
   TreeOp op{*this};
   fnCallback(seed::TreeOpResult{this, seed::OpResult::no_error}, &op);
}
void IoManagerTree::OnTabTreeOp(seed::FnTreeOp fnCallback) {
   TreeOp op{*this};
   static_cast<seed::TabTreeOp*>(this->TabTreeOp_.get())->HandleTabTreeOp(op, std::move(fnCallback));
}
//--------------------------------------------------------------------------//
void IoManagerTree::Apply(const seed::Fields& flds, StrView src, DeviceMapImpl& curmap, DeviceMapImpl& oldmap) {
   struct ToContainer : public seed::GridViewToContainer {
      DeviceMapImpl* CurContainer_;
      DeviceMapImpl* OldContainer_;
      IoConfigItem   OldCfg_;
      TimeStamp      Now_{UtcNow()};
      void OnNewRow(StrView keyText, StrView ln) override {
         auto        ifind = this->OldContainer_->find(keyText);
         DeviceItem* item;
         bool        isNewItem = (ifind == this->OldContainer_->end());
         if (isNewItem)
            item = this->CurContainer_->insert(new DeviceItem{keyText}).first->get();
         else {
            item = this->CurContainer_->insert(std::move(*ifind)).first->get();
            this->OldContainer_->erase(ifind);
            this->OldCfg_ = item->Config_;
         }
         this->FillRaw(seed::SimpleRawWr{*item}, ln);
         if (!isNewItem) {
            if (item->Config_ == this->OldCfg_)
               return;
            item->AsyncDisposeDevice(this->Now_, "Io Config changed");
         }
         item->Sch_.Parse(ToStrView(item->Config_.SchArgs_));
      }
   };
   ToContainer toContainer;
   toContainer.CurContainer_ = &curmap;
   toContainer.OldContainer_ = &oldmap;
   curmap.swap(oldmap);
   flds.GetAll(toContainer.Fields_);
   toContainer.ParseConfigStr(src);
}

} // namespaces
