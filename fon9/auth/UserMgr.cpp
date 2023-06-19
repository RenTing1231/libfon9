/// \file fon9/auth/UserMgr.cpp
/// \author fonwinz@gmail.com
#include "fon9/auth/UserMgr.hpp"
#include "fon9/seed/FieldMaker.hpp"
#include "fon9/BitvArchive.hpp"
#include "fon9/ThreadId.hpp"
#include "fon9/Log.hpp"

namespace fon9 { namespace auth {

template <class Archive>
static void SerializeVer(Archive& ar, ArchiveWorker<Archive, UserRec>& rec, unsigned ver) {
   (void)ver; assert(ver == 0);
   ar(rec.Pass_.AlgParam_,
      rec.Pass_.Salt_,
      rec.Pass_.SaltedPass_,
      rec.RoleId_,
      rec.NotBefore_,
      rec.NotAfter_,
      rec.EvChgPass_.Time_,
      rec.EvChgPass_.From_,
      rec.EvLastAuth_.Time_,
      rec.EvLastAuth_.From_,
      rec.EvLastErr_.Time_,
      rec.EvLastErr_.From_,
      rec.ErrCount_,
      rec.UserFlags_,
      rec.AuthcList_
   );
}

template <class Archive>
static void UserRecSerialize(const Archive& ar, ArchiveWorker<Archive, UserRec>& rec) {
   CompositeVer<decltype(rec)> vrec{rec, 0};
   ar(vrec);
}

void UserRec::LoadPolicy(DcQueue& buf) {
   UserRecSerialize(InArchiveClearBack<BitvInArchive>{buf}, *this);
}
void UserRec::SavePolicy(RevBuffer& rbuf) {
   UserRecSerialize(BitvOutArchive{rbuf}, *this);
}

void UserRec::OnSeedCommand(PolicyMaps::Locker& locker, seed::SeedOpResult& res, StrView cmdln, seed::FnCommandResultHandler resHandler) {
   // cmd:
   //    repw [NewPass]
   //
#define kCSTR_repw   "repw"
   StrView cmd = StrFetchTrim(cmdln, &isspace);
   StrTrim(&cmdln);
   if (cmd == kCSTR_repw) {
      char  newpass[13];
      if (cmdln.empty()) {
         RandomString(newpass, sizeof(newpass));
         cmdln.Reset(newpass, newpass + sizeof(newpass) - 1);
      }
      else {
         newpass[0] = 0;
      }
      static const char kCSTR_LOG_repw[] = "UserRec.OnSeedCommand." kCSTR_repw "|userId=";
      auto& fnHashPass = static_cast<UserTree*>(res.Sender_)->FnHashPass_;
      PassRec passRec;
      if (!fnHashPass(cmdln.begin(), cmdln.size(), passRec, HashPassFlag::ResetAll)) {
         locker.unlock();
         res.OpResult_ = seed::OpResult::mem_alloc_error;
         resHandler(res, "FnHashPass");
         fon9_LOG_ERROR(kCSTR_LOG_repw, this->PolicyId_, "|err=FnHashPass()");
      }
      else {
         this->Pass_ = std::move(passRec);
         locker->WriteUpdated(*this);
         locker.unlock();
         std::string msg = "The new password is: ";
         cmdln.AppendTo(msg);
         res.OpResult_ = seed::OpResult::no_error;
         res.LogStr_ = "Password changed.";
         resHandler(res, &msg);
         fon9_LOG_INFO(kCSTR_LOG_repw, this->PolicyId_);
      }
      return;
   }
   locker.unlock();
   if (cmd == "?") {
      res.OpResult_ = seed::OpResult::no_error;
      resHandler(res,
                 kCSTR_repw fon9_kCSTR_CELLSPL "Reset password" fon9_kCSTR_CELLSPL "[NewPass] or Random new password.");
      return;
   }
   res.OpResult_ = seed::OpResult::not_supported_cmd;
   resHandler(res, cmd);
}
StrView UserRec::GetSeedCommandLogStr(StrView cmdln) {
   constexpr size_t kCSTR_repw_size = sizeof(kCSTR_repw) - 1;
   if (cmdln.size() >= kCSTR_repw_size && memcmp(cmdln.begin(), kCSTR_repw, kCSTR_repw_size) == 0)
      return StrView{kCSTR_repw};
   return base::GetSeedCommandLogStr(cmdln);
}

//--------------------------------------------------------------------------//

seed::Fields UserMgr::MakeFields() {
   seed::Fields fields;
   fields.Add(fon9_MakeField2(UserRec, RoleId));

   fields.Add(fon9_MakeField_const(UserRec, Pass_.AlgParam_,   "AlgParam"));
 //fields.Add(fon9_MakeField_const(UserRec, Pass_.Salt_,       "Salt"));
 //fields.Add(fon9_MakeField_const(UserRec, Pass_.SaltedPass_, "SaltedPass"));

   fields.Add(fon9_MakeField (UserRec, UserFlags_, "Flags"));
   fields.Add(fon9_MakeField2(UserRec, AuthcList));
   fields.Add(fon9_MakeField2(UserRec, NotBefore));
   fields.Add(fon9_MakeField2(UserRec, NotAfter));

   fields.Add(fon9_MakeField (UserRec, EvChgPass_.Time_,  "ChgPassTime"));
   fields.Add(fon9_MakeField (UserRec, EvChgPass_.From_,  "ChgPassFrom"));
   fields.Add(fon9_MakeField (UserRec, EvLastAuth_.Time_, "LastAuthTime"));
   fields.Add(fon9_MakeField (UserRec, EvLastAuth_.From_, "LastAuthFrom"));
   fields.Add(fon9_MakeField (UserRec, EvLastErr_.Time_,  "LastErrTime"));
   fields.Add(fon9_MakeField (UserRec, EvLastErr_.From_,  "LastErrFrom"));
   fields.Add(fon9_MakeField2(UserRec, ErrCount));
   return fields;
}

UserMgr::UserMgr(const seed::MaTree* authMgrAgents, std::string name, FnHashPass fnHashPass)
   : base(new UserTree(MakeFields(), std::move(fnHashPass)),
          std::move(name)) {
   (void)authMgrAgents;
}

//--------------------------------------------------------------------------//

UserTree::LockedUser UserTree::GetLockedUserForAuthz(const AuthResult& authr) {
   Locker   container{this->PolicyMaps_};
   auto     ifind = container->ItemMap_.find(authr.GetUserIdForAuthz());
   if (ifind == container->ItemMap_.end())
      return LockedUser{};
   return LockedUser{std::move(container), static_cast<UserRec*>(ifind->get())};
}

AuthR UserTree::AuthUpdate(fon9_Auth_R rcode, const AuthRequest& req, AuthResult& authr, const PassRec* passRec, UserMgr& owner) {
   struct LogAux {
      fon9_NON_COPY_NON_MOVE(LogAux);
      LogArgs              LogArgs_{LogLevel::Warn};
      RevBufferList        RBuf_{sizeof(NumOutBuf) + 128};
      const AuthRequest&   Req_;
      const AuthResult&    Authr_;
      const UserMgr&       Owner_;
      LogAux(const AuthRequest& req, const AuthResult& authr, const UserMgr& owner)
         : Req_(req)
         , Authr_(authr)
         , Owner_(owner) {
         RevPutChar(this->RBuf_, '\n');
      }
      ~LogAux() {
         if (fon9::LogLevel_ > this->LogArgs_.Level_)
            return;
         if (!this->Authr_.AuthzId_.empty())
            RevPrint(this->RBuf_, "|authzId=", this->Authr_.AuthzId_);
         RevPrint(this->RBuf_, "UserMgr.AuthUpdate"
                  "|table=", this->Owner_.Name_,
                  "|from=", this->Req_.UserFrom_,
                  "|authcId=", this->Authr_.AuthcId_);
         AddLogHeader(this->RBuf_, this->LogArgs_.UtcTime_, this->LogArgs_.Level_);
         LogWrite(this->LogArgs_, this->RBuf_.MoveOut());
      }
   };

   #define ERR_RETURN(rc, err) do { rcode = rc; errstr = err; goto __ERR_RETURN; } while(0)
   StrView          errstr;
   LogAux           aux{req, authr, owner};
   const TimeStamp  now = aux.LogArgs_.UtcTime_;
   auto             lockedUser = this->GetLockedUserForAuthz(authr);
   if (UserRec* user = lockedUser.second) {
      fon9_WARN_DISABLE_SWITCH;
      switch (rcode) {
      case fon9_Auth_CheckLogon:
      {
         auto pass = user->Pass_;
         this->FnHashPass_(req.Response_.c_str(), req.Response_.size(), pass, HashPassFlag{});
         if (pass.SaltedPass_ == user->Pass_.SaltedPass_) {
            rcode = fon9_Auth_Success;
            authr.RoleId_.assign(user->RoleId_.empty() ? authr.GetUserIdForAuthz() : ToStrView(user->RoleId_));
            goto __PASS;
         }
      }
      /* fall through */
      default:
         if (++user->ErrCount_ == 0)
            --user->ErrCount_;
         user->EvLastErr_.Time_ = now;
         user->EvLastErr_.From_ = req.UserFrom_;
         break;
      case fon9_Auth_PassChanged:
      case fon9_Auth_Success:
      __PASS:;
         if (!user->NotBefore_.IsNull() && now < user->NotBefore_)
            ERR_RETURN(fon9_Auth_ENotBefore, "not-before");
         if (!user->NotAfter_.IsNull() && user->NotAfter_ < now)
            ERR_RETURN(fon9_Auth_ENotBefore, "not-after");
         if (IsEnumContains(user->UserFlags_, UserFlags::Locked))
            ERR_RETURN(fon9_Auth_EUserLocked, "user-locked");

         RevBufferList rbuf{128};
         if (!authr.AuthzId_.empty())
            RevPrint(rbuf, "|ForAuthc=", authr.AuthcId_);
         RevPrint(rbuf,
                  "Last logon: ", user->EvLastAuth_.Time_, kFmtYsMsD_HH_MM_SS_L,
                  " from ", user->EvLastAuth_.From_,
                  "|ChgPass: ", user->EvChgPass_.Time_, kFmtYsMsD_HH_MM_SS_L,
                  " from ", user->EvChgPass_.From_);
         if (rcode == fon9_Auth_PassChanged) {
            user->UserFlags_ -= UserFlags::NeedChgPass;
            user->EvChgPass_.Time_ = now;
            user->EvChgPass_.From_ = req.UserFrom_;
            user->Pass_ = *passRec;
         }
         else {
            if (IsEnumContains(user->UserFlags_, UserFlags::NeedChgPass))
               ERR_RETURN(fon9_Auth_ENeedChgPass, "need-change-pass");
            user->EvLastAuth_.Time_ = now;
            user->EvLastAuth_.From_ = req.UserFrom_;
         }
         authr.ExtInfo_ = BufferTo<std::string>(rbuf.MoveOut());
         user->ErrCount_ = 0;
         aux.LogArgs_.Level_ = LogLevel::Info;
         break;
      }
      fon9_WARN_POP;

      lockedUser.first->WriteUpdated(*user);
      if (rcode == fon9_Auth_PassChanged) { // 提示更改密碼?
         lockedUser.first.unlock();
         RevPrint(aux.RBuf_, "|info=PassChanged");
         return AuthR{fon9_Auth_PassChanged};
      }
      if (rcode == fon9_Auth_Success) { // 密碼即將過期? 提示更改密碼?
         authr.UserFlags_ = user->UserFlags_;
         if (fon9_UNLIKELY(!authr.AuthzId_.empty())) {
            if (!IsEnumContains(user->UserFlags_, UserFlags::AllowBeAuthz))
               ERR_RETURN(fon9_Auth_EAuthz, "no-AllowBeAuthz");
            StrView searchId;
            if (IsEnumContains(user->UserFlags_, UserFlags::AuthcOnlyAllowAtt)) {
               // authc 必須是 authz 的附屬Id, 所以 AuthcId 的前面必須與  AuthzId 相同;
               // 例: AuthcId="fon.pc"; AuthzId="fon"; 此時 attId=".pc" 則必須在存在於 user->AuthcList_;
               if (authr.AuthcId_.size() <= authr.AuthzId_.size()
                   || memcmp(authr.AuthcId_.begin(), authr.AuthzId_.begin(), authr.AuthzId_.size()) != 0)
                  ERR_RETURN(fon9_Auth_EAuthz, "not-att-id");
               searchId.Reset(authr.AuthcId_.begin() + authr.AuthzId_.size(), authr.AuthcId_.end());
            }
            else {
               searchId = ToStrView(authr.AuthcId_);
            }
            if (StrSearchSubstr(ToStrView(user->AuthcList_), searchId, ';', true)) {
               if (IsEnumContains(user->UserFlags_, UserFlags::AuthcListIsDeny))
                  ERR_RETURN(fon9_Auth_EAuthz, "AuthcListIsDeny");
            }
            else {
               if (!IsEnumContains(user->UserFlags_, UserFlags::AuthcListIsDeny))
                  ERR_RETURN(fon9_Auth_EAuthz, "not-in-AuthcList");
            }
            auto     iAuthcRec = lockedUser.first->ItemMap_.find(authr.AuthcId_);
            UserRec* authcRec = (iAuthcRec == lockedUser.first->ItemMap_.end() ? nullptr : static_cast<UserRec*>(iAuthcRec->get()));
            if (IsEnumContains(user->UserFlags_, UserFlags::AuthcMustExists) && authcRec == nullptr)
               ERR_RETURN(fon9_Auth_EAuthz, "AuthcMustExists");
            RoleId* authcRoleIdTo;
            if (IsEnumContains(user->UserFlags_, UserFlags::AuthcRoleFirst)) {
               authr.RoleId2_ = std::move(authr.RoleId_);
               authcRoleIdTo = &authr.RoleId_;
            }
            else {
               authcRoleIdTo = &authr.RoleId2_;
            }
            *authcRoleIdTo = ((authcRec == nullptr || authcRec->RoleId_.empty()) ? authr.AuthcId_ : authcRec->RoleId_);
            if (!IsEnumContains(user->UserFlags_, UserFlags::AuthcRoleMerge))
               authr.RoleId2_.clear();
            if (authcRec) {
               user->EvLastAuth_.Time_ = now;
               user->EvLastAuth_.From_ = req.UserFrom_;
               lockedUser.first->WriteUpdated(*authcRec);
            }
         }
         return AuthR{fon9_Auth_Success};
      }
      RevPrint(aux.RBuf_, ":EPass");
   }
   else
      RevPrint(aux.RBuf_, ":EUser");
   ERR_RETURN(fon9_Auth_EUserPass, "invalid-proof(maybe UserId or Password error)");
#undef ERR_RETURN

__ERR_RETURN:;
   if (lockedUser.first.owns_lock())
      lockedUser.first.unlock();
   RevPrint(aux.RBuf_, "|err=", errstr);
   return AuthR(rcode, errstr.ToString("e="));
}

} } // namespaces
