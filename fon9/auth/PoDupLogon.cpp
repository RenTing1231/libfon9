/// \file fon9/auth/PoDupLogon.cpp
/// \author fonwinz@gmail.com
#include "fon9/auth/PoDupLogon.hpp"
#include "fon9/auth/PolicyMaster.hpp"
#include "fon9/seed/FieldMaker.hpp"
#include "fon9/BitvArchive.hpp"

namespace fon9 { namespace auth {
using namespace seed;

PoDupLogonClient::~PoDupLogonClient() {
}
//--------------------------------------------------------------------------//
using PoDupLogonSeqNo = uint64_t;
struct PoDupLogonOnline : public intrusive_ref_counter<PoDupLogonOnline> {
   fon9_NON_COPY_NON_MOVE(PoDupLogonOnline);
   char                       Padding____[4];
   const PoDupLogonClientSP   Client_;
   void* const                Owner_;
   const PoDupLogonSeqNo      SeqNo_;
   CharVector                 From_;
   CharVector                 AuthzId_;
   TimeStamp                  LogonTime_{LocalNow()};

   PoDupLogonOnline(PoDupLogonClientSP&& cli, PoDupLogonSeqNo seqNo, void* owner)
      : Client_{std::move(cli)}
      , Owner_{owner}
      , SeqNo_{seqNo} {
      assert(this->Client_->Online_ == nullptr);
      this->Client_->Online_ = this;
   }
   ~PoDupLogonOnline() {
      if (this->Client_->Online_)
         this->ForceLogout("Online.dtor");
   }

   void ForceLogout(StrView reason) {
      assert(this->Client_->Online_ == this);
      this->Client_->Online_ = nullptr;
      this->Client_->PoDupLogonClient_ForceLogout(reason);
   }
   void OnLogonClientClosed(PoDupLogonClient& cli) {
      assert(this->Client_.get() == &cli);
      cli.Online_ = nullptr;
   }

   PoDupLogonOnline& GetSeedRW(Tab&) {
      return *this;
   }
   TreeSP HandleGetSapling(Tab&) {
      return TreeSP{};
   }
   template <class Locker>
   void HandleSeedCommand(Locker& locker, SeedOpResult& res, StrView cmdln, FnCommandResultHandler&& resHandler) {
      PoDupLogonClientSP cli = this->Client_;
      locker.unlock();
      res.OpResult_ = OpResult::no_error;
      StrView cmd = StrFetchTrim(cmdln, &isspace);
      if (cmd == "reject") { // reject reason
         StrTrim(&cmdln);
         std::string reason = cmdln.ToString("SeedForceLogout:");
         this->Client_->PoDupLogonClient_ForceLogout(&reason);
         resHandler(res, ToStrView(reason));
         return;
      }
      if (cmd == "?") {
         resHandler(res,
                    "reject" fon9_kCSTR_CELLSPL "Force logout" fon9_kCSTR_CELLSPL "Force logout reason.");
      }
      else {
         res.OpResult_ = OpResult::not_supported_cmd;
         resHandler(res, cmdln);
      }
   }
};
using PoDupLogonOnlineSP = intrusive_ptr<PoDupLogonOnline>;
//--------------------------------------------------------------------------//
using OnlineUserTable = SortedVector<PoDupLogonSeqNo, PoDupLogonOnlineSP>;
static inline OnlineUserTable::iterator ContainerLowerBound(OnlineUserTable& container, StrView strKeyText) {
   return container.lower_bound(StrTo(strKeyText, PoDupLogonSeqNo{}));
}
static inline OnlineUserTable::iterator ContainerFind(OnlineUserTable& container, StrView strKeyText) {
   return container.find(StrTo(strKeyText, PoDupLogonSeqNo{}));
}
//--------------------------------------------------------------------------//
struct PoDupLogonRec {
   // 也許可以增加一個 FromOnly 欄位: 限制登入來源;
   PoDupLogonAct     PoDupLogonAct_{PoDupLogonAct::RejectNew};
   EnabledYN         BindFromYN_{};
   char              Padding___[2];
   uint16_t          OnlineMax_{0};
   uint16_t          OnlineCount_{0};
   PoDupLogonSeqNo   NextSeqNo_{0};
   CharVector        BindFrom_;
};

template <class Archive>
static void SerializeVer(Archive& ar, ArchiveWorker<Archive, PoDupLogonRec>& rec, unsigned ver) {
   (void)ver; assert(ver == 0);
   ar( rec.PoDupLogonAct_
     , rec.OnlineMax_
     , rec.BindFromYN_
     , rec.BindFrom_
   );
}

template <class Archive>
static void PoDupLogonRec_Serialize(const Archive& ar, ArchiveWorker<Archive, PoDupLogonRec>& rec) {
   CompositeVer<decltype(rec)> vrec{rec, 0};
   ar(vrec);
}
//--------------------------------------------------------------------------//
class PoDupLogonTree : public PolicyTree {
   fon9_NON_COPY_NON_MOVE(PoDupLogonTree);
   using base = PolicyTree;
   using DetailTableImpl = OnlineUserTable;
   using DetailTable = MustLock<DetailTableImpl>;
   struct DetailTree : public Tree {
      fon9_NON_COPY_NON_MOVE(DetailTree);
      using base = Tree;
      using Locker = DetailTable::Locker;
      using PodOp = PodOpLockerNoWrite<PoDupLogonOnline, Locker>;
      DetailTable Container_;

      struct TreeOp : public seed::TreeOp {
         fon9_NON_COPY_NON_MOVE(TreeOp);
         using base = seed::TreeOp;
         using base::base;

         static void MakeRecordView(DetailTableImpl::iterator ivalue, Tab* tab, RevBuffer& rbuf) {
            if (tab)
               FieldsCellRevPrint(tab->Fields_, SimpleRawRd{*ivalue->second}, rbuf);
            RevPrint(rbuf, ivalue->first);
         }
         void GridView(const GridViewRequest& req, FnGridViewOp fnCallback) override {
            TreeOp_GridView_MustLock(*this, static_cast<DetailTree*>(&this->Tree_)->Container_,
                                     req, std::move(fnCallback), &MakeRecordView);
         }
         void Get(StrView strKeyText, FnPodOp fnCallback) override {
            {
               Locker container{static_cast<DetailTree*>(&this->Tree_)->Container_};
               auto   ifind = GetIteratorForPod(*container, strKeyText);
               if (ifind != container->end()) {
                  PodOp podOp{*ifind->second, this->Tree_, OpResult::no_error, strKeyText, container};
                  fnCallback(podOp, &podOp);
                  return;
               }
            } // unlock.
            fnCallback(PodOpResult{this->Tree_, OpResult::not_found_key, strKeyText}, nullptr);
         }
      };
   public:
      DetailTree(PoDupLogonTree& parent)
         : base{parent.LayoutSP_->GetTab(0)->SaplingLayout_} {
      }
      void OnTreeOp(FnTreeOp fnCallback) override {
         TreeOp op{*this};
         fnCallback(TreeOpResult{this, OpResult::no_error}, &op);
      }
   };

   struct MasterItem : public PolicyItem, public PoDupLogonRec {
      fon9_NON_COPY_NON_MOVE(MasterItem);
      using base = PolicyItem;
      const TreeSPT<DetailTree>  Sapling_;

      MasterItem(const StrView& policyId, PoDupLogonTree& owner)
         : base(policyId)
         , Sapling_{new DetailTree{owner}} {
      }
      TreeSP GetSapling() override {
         return this->Sapling_;
      }
      void LoadPolicy(DcQueue& buf) override {
         PoDupLogonRec_Serialize(BitvInArchive{buf}, *this);
      }
      void SavePolicy(RevBuffer& rbuf) override {
         PoDupLogonRec_Serialize(BitvOutArchive{rbuf}, *this);
      }
      // TODO: cmdln: "logout reason"
      // void OnSeedCommand(PolicyMaps::Locker& locker, SeedOpResult& res, StrView cmdln, FnCommandResultHandler resHandler) override {
      //    base::OnSeedCommand(locker, res, cmdln, std::move(resHandler));
      // }
   };
   using MasterItemSP = intrusive_ptr<MasterItem>;

   PolicyItemSP MakePolicy(const StrView& policyId) override {
      return PolicyItemSP{new MasterItem(policyId, *this)};
   }

   static LayoutSP MakeDetailLayout() {
      Fields fields;
      fields.Add(fon9_MakeField2(PoDupLogonOnline, AuthzId));
      fields.Add(fon9_MakeField2(PoDupLogonOnline, From));
      fields.Add(fon9_MakeField2(PoDupLogonOnline, LogonTime));
      return LayoutSP{new Layout1(fon9_MakeField2(PoDupLogonOnline, SeqNo),
                new Tab{Named{"OnlineUsers"}, std::move(fields),
                        TabFlag::NoSapling | TabFlag::HasSameCommandsSet},
                TreeFlag{})};
   }
   static LayoutSP MakeLayout() {
      Fields fields;
      fields.Add(fon9_MakeField       (MasterItem, PoDupLogonAct_, "DupAct", "DupAct(Reject)"));
      fields.Add(fon9_MakeField2      (MasterItem, BindFromYN));
      fields.Add(fon9_MakeField2      (MasterItem, BindFrom));
      fields.Add(fon9_MakeField2      (MasterItem, OnlineMax));
      fields.Add(fon9_MakeField2_const(MasterItem, OnlineCount));
      fields.Add(fon9_MakeField2_const(MasterItem, NextSeqNo));
      TabSP tab(new Tab(Named{"DupAct"},
                        std::move(fields),
                        MakeDetailLayout(),
                        TabFlag::Writable | TabFlag::HasSapling
                           | TabFlag::HasSameCommandsSet // "reject all"
               ));
      return new Layout1(fon9_MakeField(PolicyItem, PolicyId_, "LogonId"),
                         std::move(tab), TreeFlag::AddableRemovable);
   }

public:
   PoDupLogonTree() : base{MakeLayout()} {
   }
   PoDupLogonAct CheckLogon(StrView logonId, StrView authzId, StrView protocolId, const StrView userFrom,
                            PoDupLogonClientSP cli, auth::AuthR* rcode, PoDupLogonAct* cfg) {
      if (cfg)
         *cfg = PoDupLogonAct::RejectOld;
      CharVector policyId{logonId};
      if (!protocolId.empty()) {
         policyId.push_back('.');
         policyId.append(protocolId);
      }
      struct AutoDupLogout : public std::vector<PoDupLogonOnlineSP> {
         StrView NewFrom_;
         AutoDupLogout(StrView newFrom) : NewFrom_{newFrom} {
         }
         ~AutoDupLogout() {
            if (this->empty())
               return;
            std::string reason = this->NewFrom_.ToString("DupLogon:");
            for (PoDupLogonOnlineSP& i : *this) {
               i->ForceLogout(&reason);
            }
         }
      };
      AutoDupLogout           autoDupLogout{userFrom};
      PolicyMaps::ConstLocker maps{this->PolicyMaps_};
      auto                    ifind = maps->ItemMap_.find(policyId);
      if (ifind == maps->ItemMap_.end()) {
         if (protocolId.empty())
            return PoDupLogonAct::RejectOld; // 沒有設定 = 允許登入 = 不限制, 且不納入管制.
         policyId.resize(logonId.size());
         ifind = maps->ItemMap_.find(policyId);
         if (ifind == maps->ItemMap_.end())
            return PoDupLogonAct::RejectOld;
      }
      auto* master = static_cast<MasterItem*>(ifind->get());
      if (cfg)
         *cfg = master->PoDupLogonAct_;
      if (!cli)
         return master->PoDupLogonAct_;
      ++master->NextSeqNo_;
      if (master->OnlineMax_ > 0 && Is_PoDupLogonAct_RejectNew(master->PoDupLogonAct_)) {
         if (master->OnlineCount_ >= master->OnlineMax_) {
            if (rcode) {
               rcode->RCode_ = fon9_Auth_EDupLogon;
               rcode->Info_ = "e=DupLogon";
            }
            return PoDupLogonAct::RejectNew;
         }
      }
      if (master->BindFromYN_ == EnabledYN::Yes && !master->BindFrom_.empty()) {
         StrView remoteIp = StrFindValue(userFrom, "R");
         remoteIp = StrFetchNoTrim(remoteIp, ':');
         if (ToStrView(master->BindFrom_) != remoteIp) {
            if (rcode) {
               rcode->RCode_ = fon9_Auth_EBindFrom;
               rcode->Info_ = "e=BindFrom";
            }
            return PoDupLogonAct::RejectNew;
         }
      }
      DetailTree::Locker dmap{master->Sapling_->Container_};
      if (master->OnlineMax_ > 0 && Is_PoDupLogonAct_RejectOld(master->PoDupLogonAct_)) {
         while (master->OnlineCount_ >= master->OnlineMax_) {
            --master->OnlineCount_;
            autoDupLogout.push_back(std::move(dmap->begin()->second));
            dmap->erase(dmap->begin());
         }
      }
      ++master->OnlineCount_;
      DetailTableImpl::value_type ousr{master->NextSeqNo_,
         new PoDupLogonOnline{std::move(cli), master->NextSeqNo_, master}};
      ousr.second->AuthzId_.assign(authzId);
      ousr.second->From_.reserve(protocolId.size() + userFrom.size() + 1);
      ousr.second->From_.assign(protocolId);
      ousr.second->From_.push_back('|');
      ousr.second->From_.append(userFrom);
      dmap->insert(dmap->end(), std::move(ousr));
      return PoDupLogonAct::RejectOld;
   }
   void OnLogonClientClosed(PoDupLogonClient& cli) {
      PoDupLogonOnline* online = cli.Online();
      if (!online)
         return;
      PolicyMaps::ConstLocker maps{this->PolicyMaps_};
      if ((online = cli.Online()) == nullptr)
         return;
      auto*                master = static_cast<MasterItem*>(online->Owner_);
      DetailTree::Locker   dmap{master->Sapling_->Container_};
      auto                 ifind = dmap->find(online->SeqNo_);
      if (ifind != dmap->end()) {
         ifind->second->OnLogonClientClosed(cli);
         --master->OnlineCount_;
         dmap->erase(ifind);
      }
   }
};
//--------------------------------------------------------------------------//
PoDupLogonAgent::PoDupLogonAgent(MaTree* authMgrAgents, std::string name)
   : base(new PoDupLogonTree{}, std::move(name)) {
   (void)authMgrAgents;
}
void PoDupLogonAgent::OnLogonClientClosed(PoDupLogonClient& cli) {
   static_cast<PoDupLogonTree*>(this->Sapling_.get())->OnLogonClientClosed(cli);
}
bool PoDupLogonAgent::CheckLogon(const AuthResult& authr, const StrView& protocolId, const StrView& userFrom,
                                 PoDupLogonClientSP cli, auth::AuthR& rcode) {
   return Is_PoDupLogonAct_RejectOld(
      static_cast<PoDupLogonTree*>(this->Sapling_.get())
         ->CheckLogon(ToStrView(authr.AuthcId_), ToStrView(authr.AuthzId_), protocolId,
                      userFrom, std::move(cli), &rcode, nullptr));
}
bool PoDupLogonAgent::CheckLogonR(const AuthResult& authr, const StrView& protocolId, const StrView& userFrom,
                                  PoDupLogonClientSP cli, auth::AuthR& rcode, PoDupLogonAct& cfg) {
   return Is_PoDupLogonAct_RejectOld(
      static_cast<PoDupLogonTree*>(this->Sapling_.get())
         ->CheckLogon(ToStrView(authr.AuthcId_), ToStrView(authr.AuthzId_), protocolId,
                      userFrom, std::move(cli), &rcode, &cfg));
}
PoDupLogonAct PoDupLogonAgent::GetDupLogonAct(const StrView& userId, const StrView& protocolId) const {
   return static_cast<PoDupLogonTree*>(this->Sapling_.get())
      ->CheckLogon(userId, StrView{}, protocolId, nullptr, nullptr, nullptr, nullptr);
}

} } // namespaces
