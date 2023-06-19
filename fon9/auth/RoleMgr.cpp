/// \file fon9/auth/RoleMgr.cpp
/// \author fonwinz@gmail.com
#include "fon9/auth/RoleMgr.hpp"
#include "fon9/auth/PolicyMaster.hpp"
#include "fon9/seed/FieldMaker.hpp"
#include "fon9/BitvArchive.hpp"

namespace fon9 { namespace auth {

class RoleTree : public MasterPolicyTree {
   fon9_NON_COPY_NON_MOVE(RoleTree);
   using base = MasterPolicyTree;

   using DetailTableImpl = PolicyKeys;
   using RoleConfigTree = DetailPolicyTreeTable<DetailTableImpl, KeyMakerCharVector>;
   using DetailTable = RoleConfigTree::DetailTable;

   struct RoleItem : public MasterPolicyItem {
      fon9_NON_COPY_NON_MOVE(RoleItem);
      using base = MasterPolicyItem;
      CharVector  Description_;
      RoleItem(const StrView& roleId, MasterPolicyTreeSP owner)
         : base(roleId, std::move(owner)) {
         this->DetailSapling_.reset(new RoleConfigTree{*this});
      }
      void LoadPolicy(DcQueue& buf) override {
         unsigned ver = 0;
         DetailTable::Locker pmap{static_cast<RoleConfigTree*>(this->DetailSapling())->DetailTable_};
         BitvInArchive{buf}(ver, this->Description_, *pmap);
      }
      void SavePolicy(RevBuffer& rbuf) override {
         const unsigned ver = 0;
         DetailTable::ConstLocker pmap{static_cast<RoleConfigTree*>(this->DetailSapling())->DetailTable_};
         BitvOutArchive{rbuf}(ver, this->Description_, *pmap);
      }
   };
   using RoleItemSP = intrusive_ptr<RoleItem>;

   PolicyItemSP MakePolicy(const StrView& roleId) override {
      return PolicyItemSP{new RoleItem(roleId, this)};
   }

   static seed::LayoutSP MakeLayout() {
      seed::Fields fields;
      fields.Add(fon9_MakeField(DetailTableImpl::value_type, second, "PolicyId"));
      seed::LayoutSP saplingLayout{
         new seed::Layout1(fon9_MakeField(DetailTableImpl::value_type, first, "PolicyName"),
                           new seed::Tab{Named{"RoleConfig"}, std::move(fields),
                                         seed::TabFlag::NoSapling | seed::TabFlag::Writable},
                           seed::TreeFlag::AddableRemovable)};

      fields.Add(fon9_MakeField2(RoleItem, Description));
      seed::TabSP tab{new seed::Tab(Named{"RoleCfg"}, std::move(fields), std::move(saplingLayout),
                                    seed::TabFlag::HasSapling | seed::TabFlag::Writable)};
      return new seed::Layout1(fon9_MakeField(PolicyItem, PolicyId_, "RoleId"),
                               std::move(tab), seed::TreeFlag::AddableRemovable);
   }

public:
   RoleTree() : base{MakeLayout()} {
   }

   bool GetRole(const StrView& roleId, RoleConfig& res, RoleMgr::GetRoleMode mode) const {
      struct ResultHandler {
         RoleConfig*          Result_;
         RoleMgr::GetRoleMode Mode_;
         uint32_t             Padding___;
         void InLocking(const PolicyItem&) {
         }
         void OnUnlocked(DetailPolicyTree& detailTree) {
            DetailTable::Locker pmap{static_cast<RoleConfigTree*>(&detailTree)->DetailTable_};
            switch (this->Mode_) {
            default:
            case RoleMgr::GetRoleMode_Renew:
               this->Result_->PolicyKeys_ = *pmap;
               return;
            case RoleMgr::GetRoleMode_Append:
            case RoleMgr::GetRoleMode_Replace:
               for (auto i : *pmap) {
                  auto& policy = this->Result_->PolicyKeys_.kfetch(i.first);
                  if (policy.second.empty() || this->Mode_ == RoleMgr::GetRoleMode_Replace)
                     policy.second = i.second;
               }
               break;
            }
         }
      };
      if (mode == RoleMgr::GetRoleMode_Renew)
         res.RoleId_.assign(roleId);
      return this->GetPolicy(roleId, ResultHandler{&res, mode, 0});
   }
};

//--------------------------------------------------------------------------//

RoleMgr::RoleMgr(std::string name)
   : base(new RoleTree{}, std::move(name)) {
}

bool RoleMgr::GetRole(StrView roleId, RoleConfig& res, GetRoleMode mode) {
   return static_cast<RoleTree*>(this->Sapling_.get())->GetRole(roleId, res, mode);
}

} } // namespaces
