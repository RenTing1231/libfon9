/// \file fon9/auth/PolicyAcl.cpp
/// \author fonwinz@gmail.com
#include "fon9/auth/PolicyAcl.hpp"
#include "fon9/auth/PolicyMaster.hpp"
#include "fon9/seed/FieldMaker.hpp"
#include "fon9/BitvArchive.hpp"

namespace fon9 { namespace auth {

class PolicyAclTree : public MasterPolicyTree {
   fon9_NON_COPY_NON_MOVE(PolicyAclTree);
   using base = MasterPolicyTree;

   using DetailTableImpl = seed::AccessList;
   using DetailTree = DetailPolicyTreeTable<DetailTableImpl, KeyMakerCharVector>;
   using DetailTable = DetailTree::DetailTable;

   struct MasterItem : public MasterPolicyItem, public seed::AclHomeConfig {
      fon9_NON_COPY_NON_MOVE(MasterItem);
      using base = MasterPolicyItem;
      MasterItem(const StrView& policyId, MasterPolicyTreeSP owner)
         : base(policyId, std::move(owner)) {
         this->DetailSapling_.reset(new DetailTree{*this});
      }
      void LoadPolicy(DcQueue& buf) override {
         // 因為 Acl 儲存打包 [MasterItem + Container(Detail)],
         // 所以若有增減欄位, 則不適用 InArchiveClearBack<> 的方式處理;
         unsigned            ver = 0;
         DetailTable::Locker pmap{static_cast<DetailTree*>(this->DetailSapling())->DetailTable_};
         BitvInArchive{buf}(ver, this->Home_);
         if (fon9_LIKELY(ver > 0)) {
            BitvTo(buf, this->MaxSubrCount_);
            BitvTo(buf, this->FcQuery_.FcCount_);
            BitvTo(buf, this->FcQuery_.FcTimeMS_);
            if (fon9_LIKELY(ver > 1)) {
               BitvTo(buf, this->FcRecover_.FcCount_);
               BitvTo(buf, this->FcRecover_.FcTimeMS_);
            }
         }
         pmap->clear();
         if (size_t sz = BitvToContainerElementCount(buf)) {
            ContainerReserve(*pmap, sz);
            seed::AclPath        key;
            seed::AccessControl  val;
            do {
               BitvTo(buf, key);
               BitvTo(buf, val.Rights_);
               if (ver > 0) {
                  BitvTo(buf, val.MaxSubrCount_);
               }
               pmap->insert(pmap->end(), seed::AccessList::value_type{std::move(key), std::move(val)});
            } while (--sz > 0);
         }
      }
      void SavePolicy(RevBuffer& rbuf) override {
         const unsigned           ver = 2;
         DetailTable::ConstLocker pmap{static_cast<DetailTree*>(this->DetailSapling())->DetailTable_};
         const auto               ibeg = pmap->begin();
         for (auto i = pmap->end(); i != ibeg;) {
            --i;
            ToBitv(rbuf, i->second.MaxSubrCount_);
            ToBitv(rbuf, i->second.Rights_);
            ToBitv(rbuf, i->first);
         }
         ElementCountToBitv(rbuf, *pmap);
         BitvOutArchive{rbuf}(ver, this->Home_,          // 何時加入的欄位?
                              this->MaxSubrCount_,       // ver=1
                              this->FcQuery_.FcCount_,   //
                              this->FcQuery_.FcTimeMS_,  //
                              this->FcRecover_.FcCount_, // ver=2
                              this->FcRecover_.FcTimeMS_);
      }
   };
   using MasterItemSP = intrusive_ptr<MasterItem>;

   PolicyItemSP MakePolicy(const StrView& policyId) override {
      return PolicyItemSP{new MasterItem(policyId, this)};
   }

   static seed::LayoutSP MakeLayout() {
      seed::Fields fields;
      fields.Add(fon9_MakeField (MasterItem, Home_, "HomePath"));
      fields.Add(fon9_MakeField2(MasterItem, MaxSubrCount));
      fields.Add(fon9_MakeField (MasterItem, FcQuery_.FcCount_,    "FcQryCount"));
      fields.Add(fon9_MakeField (MasterItem, FcQuery_.FcTimeMS_,   "FcQryMS"));
      fields.Add(fon9_MakeField (MasterItem, FcRecover_.FcCount_,  "FcRecoverKB"));
      fields.Add(fon9_MakeField (MasterItem, FcRecover_.FcTimeMS_, "FcRecoverMS"));
      seed::TabSP tab{new seed::Tab(Named{"Acl"}, std::move(fields), seed::MakeAclTreeLayoutWritable(),
                                    seed::TabFlag::Writable | seed::TabFlag::HasSapling)};
      return new seed::Layout1(fon9_MakeField2(PolicyItem, PolicyId),
                               std::move(tab), seed::TreeFlag::AddableRemovable);
   }

public:
   PolicyAclTree() : base{MakeLayout()} {
   }

   bool GetPolicy(const StrView& policyId, seed::AclConfig& res) const {
      struct ResultHandler {
         seed::AclConfig* Result_;
         void InLocking(const PolicyItem& master) {
            *static_cast<seed::AclHomeConfig*>(this->Result_) = *static_cast<const MasterItem*>(&master);
         }
         void OnUnlocked(DetailPolicyTree& detailTree) {
            DetailTable::Locker pmap{static_cast<DetailTree*>(&detailTree)->DetailTable_};
            this->Result_->Acl_ = *pmap;
         }
      };
      return base::GetPolicy(policyId, ResultHandler{&res});
   }
};

//--------------------------------------------------------------------------//

PolicyAclAgent::PolicyAclAgent(seed::MaTree* authMgrAgents, std::string name)
   : base(new PolicyAclTree{}, std::move(name)) {
   (void)authMgrAgents;
}

bool PolicyAclAgent::GetPolicy(const AuthResult& authr, PolicyConfig& res) {
   if (!static_cast<PolicyAclTree*>(this->Sapling_.get())->GetPolicy(authr.GetPolicyId(ToStrView(this->Name_)), res))
      return false;
   static const StrView kOldStrA[] = {"UserId}",                "Authz}"};
   const StrView        newStrA[] = {ToStrView(authr.AuthcId_), authr.GetUserIdForAuthz()};
   res.Home_ = CharVectorReplaceA(ToStrView(res.Home_), '{', kOldStrA, newStrA, 2);
   seed::AccessList acl{std::move(res.Acl_)};
   for (auto& v : acl) {
      res.Acl_.kfetch(CharVectorReplaceA(ToStrView(v.first), '{', kOldStrA, newStrA, 2)).second = v.second;
   }
   PoAclAdjusters_.Publish(authr, res);
   return true;
}
void PolicyAclAgent::MakeGridView(RevBuffer& rbuf, const PolicyConfig& aclcfg) {
   auto* gvLayout = this->Sapling_->LayoutSP_->GetTab(0)->SaplingLayout_.get();
   auto* gvTab = gvLayout->GetTab(0);
   seed::SimpleMakeFullGridView(aclcfg.Acl_, *gvTab, rbuf);
   seed::FieldsNameRevPrint(gvLayout, *gvTab, rbuf);
}

} } // namespaces
