/// \file fon9/auth/AuthMgr.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_auth_AuthMgr_hpp__
#define __fon9_auth_AuthMgr_hpp__
#include "fon9/auth/RoleMgr.hpp"

namespace fon9 { namespace auth {

/// \ingroup auth
/// 使用者的 [狀態 or 管制] 旗標.
enum class UserFlags : uint32_t {
   /// 必須修改密碼後才能登入.
   NeedChgPass = 0x01,
   /// 使用者鎖定, 禁止認證.
   Locked = 0x02,

   /// 若無此旗標, 則底下有關 Authc/Authz 的設定都沒有作用.
   AllowBeAuthz = 0x010,
   /// 禁止 AuthcList_ 設定的 UserId 進行認證.
   /// 若無此旗標, 則 UserRec.AuthcList_ 設定的為 [允許] 的 UserId;
   AuthcListIsDeny = 0x020,
   /// Authc 使用此 UserRec 當成 Authz 時, Authc 必須存在於 UserMgr;
   /// 若無此旗標, 則 Authc 可以不存在於 UserMgr;
   AuthcMustExists = 0x040,
   /// 僅允許附屬帳號使用此 UserRec 登入; 此時 AuthcList 設定的是附屬 Id 列表;
   /// 例: 此筆 UserId="fon"; AuthcList = ".web;.pc;.mobile"; 且有設定此旗標; 且無設定 AuthcListIsDeny;
   /// 則: 僅允許登入時使用: "fon.web" 或 "fon.pc" 或 "fon.mobile"; 將 "fon" 當成 Authz 來執行登入;
   AuthcOnlyAllowAtt = 0x080,
   /// 優先使用 Authc 的 RoleId 設定;
   /// - 若 Authc 不存在 or Authc.RoleId.empty() 則 AuthResult.RoleId_ = AuthcId;
   /// - 若 Authc 存在, 且 !Authc.RoleId.empty() 則 AuthResult.RoleId_ = Authc.RoleId;
   /// - 若無此旗標,      則 AuthResult.RoleId_ = AuthzId;
   AuthcRoleFirst = 0x100,
   /// 需要繼續匯入[另一個] User 的 PolicyKeys:
   /// - 這裡的 [另一個] User 的 RoleId = IsEnumContains(AuthcRoleFirst ? Authz.RoleId : Authc.RoleId);
   /// - 先處理 AuthcRoleFirst 的規則.
   /// - 然後將另一個 User 設定的 RoleMgr.GetRole(RoleId) 匯入到 AuthResult.PolicyKeys_;
   /// - 在取出 Policy 內容時, 若用 PolicyId = ([PolicyKeys 設定的 PolicyId], 若不存在則用 [優先者的 RoleId]);
   ///   找不到需要的 Policy 時, 也 [不會] 再去用 [另一個的 RoleId] 去嘗試。
   AuthcRoleMerge = 0x200,
   /// - 在有 AuthcRoleMerge 的情況下:
   /// - 若提供此旗標, 則: 匯入 PolicyKeys 不存在的 Policy, 若 Policy 已存在, 則取代;
   /// - 無提供此旗標, 則: 匯入 PolicyKeys 不存在的 Policy, 若 Policy 已存在, 則不匯入;
   /// - 匯入方式, 例 [優先使用的 PolicyKeys 已有 PoAcl, 且後續的也有設定 PoAcl]:
   ///   - 若無此旗標, 則使用優先匯入的, 而[不會]將2個的 PoAcl 合併;
   ///   - 若有此旗標, 則使用之後匯入的, 而[不會]將2個的 PoAcl 合併;
   AuthcRoleReplace = 0x400,
};
fon9_ENABLE_ENUM_BITWISE_OP(UserFlags);

/// \ingroup auth
/// 認證結果.
struct fon9_API AuthResult : public RoleConfig {
   AuthMgrSP   AuthMgr_;
   /// manager? guest? trader?
   /// - 例如執行 sudo, 這裡用的是 "root"
   /// - 例如執行主管強迫, 這裡用的是主管Id.
   UserId      AuthzId_;
   /// Tony? Alice? Bob?
   /// - 例如執行 sudo, 這裡用的是原本登入時的 UserId
   /// - 例如執行主管強迫, 這裡用的是營業員(or Keyin)的Id.
   /// - 通常情況下, 這裡才使應用系統該應使用的 UserId, 例:
   ///   Authc="fon.pc"; Authz="fon"; 此時登入後下單, 下單歸屬的 UserId 應為 "fon.pc";
   ///   若需要詳查當初的 [授權Id:AuthzId], 則應從系統 log 尋找;
   UserId      AuthcId_;
   /// - 認證成功後的提示訊息: e.g. "Last login: 2017/09/08 10:50:55 from 192.168.1.3";
   std::string ExtInfo_;
   UserFlags   UserFlags_{};
   char        Padding____[4];
   /// 若有 AuthzId_; 則可能需要額外匯入另一組 Role 設定;
   RoleId      RoleId2_;

   AuthResult(AuthMgrSP authMgr) : AuthMgr_{std::move(authMgr)} {
   }
   AuthResult(AuthMgrSP authMgr, UserId authcId, RoleConfig roleCfg)
      : RoleConfig(std::move(roleCfg))
      , AuthMgr_{std::move(authMgr)}
      , AuthcId_{std::move(authcId)} {
   }

   void Clear() {
      this->RoleId_.clear();
      this->PolicyKeys_.clear();
      this->AuthzId_.clear();
      this->AuthcId_.clear();
      this->ExtInfo_.clear();
      this->UserFlags_ = UserFlags{};
      this->RoleId2_.clear();
   }

   /// 取得此次需要使用哪個[授權]?
   /// - 若 this->AuthzId_.empty()  返回: AuthcId_; 表示 Authc 沒有請求另外的授權;
   /// - 若 !this->AuthzId_.empty() 返回: AuthzId_; 表示 Authc 請求使用 Authz 的授權;
   StrView GetUserIdForAuthz() const {
      return this->AuthzId_.empty() ? ToStrView(this->AuthcId_) : ToStrView(this->AuthzId_);
   }

   /// 若 this->AuthzId_.empty()  輸出: AuthcId_;
   /// 若 !this->AuthzId_.empty() 輸出: AuthcId_ + "?" + AuthzId_;
   void RevPrintUser(RevBuffer& rbuf) const;
   /// 輸出: RevPrintUser() + "|" + deviceId;
   void RevPrintUFrom(RevBuffer& rbuf, StrView deviceId) const;
   /// 輸出: RevPrintUser() + "|" + deviceId;
   std::string MakeUFrom(StrView deviceId) const;

   /// 登入成功後, 使用 AuthResult 的人, 必須自行決定是否需要更新 RoleConfig.
   /// 因為有些登入只想要驗證 userid/pass 是否正確.
   void UpdateRoleConfig();

   /// 若 policyName 在 this->PolicyKeys_ 找不到,
   /// 則應直接用 RoleId_ 當作該 policyName 的 PolicyId.
   StrView GetPolicyId(StrView policyName) const;
};

/// \ingroup auth
/// 使用者認證要求
struct AuthRequest {
   /// 依據認證機制有不同的涵義:
   /// - SASL PLAIN: authzid<EOS>authcid<EOS>password
   /// - SASL SCRAM: 依協商階段提供不同的訊息.
   std::string Response_;
   /// 使用者來自哪裡?
   /// - "console"
   /// - "R=ip:port|L=ip:port"
   std::string UserFrom_;
};

//--------------------------------------------------------------------------//

/// \ingroup auth
/// 使用者認證結果 callback.
using FnOnAuthVerifyCB = std::function<void(AuthR authr, AuthSessionSP authSession)>;

fon9_WARN_DISABLE_PADDING;
/// \ingroup auth
/// 認證處理階段.
/// - 一般而言, 每個認證要求, 都會有一個 AuthSession 來負責處理每個認證步驟.
/// - 當 FnOnAuthVerifyCB 收到 fon9_Auth_NeedsMore 時,
///   取得後續的 response 之後, 再包成 AuthRequest 丟給 AuthVerify() 處理.
class fon9_API AuthSession : public intrusive_ref_counter<AuthSession> {
   fon9_NON_COPY_NON_MOVE(AuthSession);
protected:
   const FnOnAuthVerifyCB  OnVerifyCB_;
   AuthResult              AuthResult_;
public:
   AuthSession(AuthMgrSP authMgr, FnOnAuthVerifyCB&& cb)
      : OnVerifyCB_(std::move(cb)), AuthResult_(std::move(authMgr)) {
   }
   virtual ~AuthSession();

   /// 除了依靠 AuthSessionSP 自動解構外,
   /// 當確定 AuthSession 不再使用時(此時可能已認證成功, 或仍在進行認證中),
   /// 應呼叫此處, 讓 AuthSession 的衍生者可以先清除資源.
   /// 預設: do nothing.
   virtual void Dispose();

   /// 當收到 client 的 request 時, 透過這裡處理.
   /// 如果認證過程, 有多個步驟, 一樣透過這裡處理.
   virtual void AuthVerify(const AuthRequest& req) = 0;

   /// 僅保證在 FnOnAuthVerifyCB 事件, 或認證結束後, 才能安全的取得及使用.
   /// 在認證處理的過程中, 不應該呼叫此處.
   /// 一旦認證結束, 此處的資料都不會再變動(包含 ExtInfo_ 也不會變).
   AuthResult& GetAuthResult() {
      return this->AuthResult_;
   }
};
fon9_WARN_POP;

//--------------------------------------------------------------------------//

/// \ingroup auth
/// 實際執行認證的元件基底.
/// 例如: AuthAgentSaslScramSha256Server.cpp
class fon9_API AuthAgent : public seed::NamedSeed {
   fon9_NON_COPY_NON_MOVE(AuthAgent);
   using base = seed::NamedSeed;
public:
   using base::base;

   virtual AuthSessionSP CreateAuthSession(AuthMgrSP authMgr, FnOnAuthVerifyCB cb) = 0;
};
using AuthAgentSP = intrusive_ptr<AuthAgent>;

//--------------------------------------------------------------------------//

/// \ingroup auth
/// 使用者 **認證&授權** 管理員.
/// - 一個 AuthMgr 可以有多個認證代理員(AuthAgent), 可提供多種認證方式.
///   - 盡量使用 SASL 規範.
///   - AgentName = fon9_kCSTR_AuthAgent_Prefix + Mechanism Name
///     - 若為 SASL 規範的機制, 則將其中的 '-' 改成 '_'
///     - 例如: "AA_PLAIN"; "AA_SCRAM_SHA_256";
/// - 因此 AuthMgr 必須:
///   - 包含一個 RoleMgr
///   - Policy 的管理.
/// - 所以 AuthMgr::Agents_ 包含了: AuthAgents, RoleMgr, Policies, OnlineUserMgr...
class fon9_API AuthMgr : public seed::NamedSeed {
   fon9_NON_COPY_NON_MOVE(AuthMgr);
   using base = seed::NamedSeed;

public:
   /// 擁有此 AuthMgr 的管理員.
   const seed::MaTreeSP MaRoot_;
   /// 包含 UserMgrs, Role, Policies...
   const seed::MaTreeSP Agents_;
   /// 負責儲存 Agents 所需的資料.
   const InnDbfSP       Storage_;
   const RoleMgrSP      RoleMgr_;

   AuthMgr(seed::MaTreeSP ma, std::string name, InnDbfSP storage);
   virtual seed::TreeSP GetSapling() override;

   #define fon9_kCSTR_AuthAgent_Prefix    "AA_"
   /// 建立一個處理認證協商的物件.
   /// - "AA_PLAIN" 或 "PLAIN" = SASL "PLAIN"
   /// - "AA_SCRAM_SHA_256" 或 "SCRAM_SHA_256" = SASL "SCRAM-SHA-256"
   /// - SCRAM_ARGON2?
   AuthSessionSP CreateAuthSession(StrView mechanismName, FnOnAuthVerifyCB cb);

   /// 取得 SASL 機制列表: 使用 SASL 的命名慣例.
   std::string GetSaslMechList(char chSpl = ' ') const;

   template <class PolicyConfig, class PolicyAgent = typename PolicyConfig::PolicyAgent>
   bool GetPolicy(StrView agentName, const AuthResult& authr, PolicyConfig& res) const {
      if (auto agent = this->Agents_->Get<PolicyAgent>(agentName))
         return agent->GetPolicy(authr, res);
      return false;
   }
   template <class PolicyAgent, class PolicyConfig = typename PolicyAgent::PolicyConfig>
   bool GetPolicy(StrView agentName, const AuthResult& authr, PolicyConfig& res) const {
      if (auto agent = this->Agents_->Get<PolicyAgent>(agentName))
         return agent->GetPolicy(authr, res);
      return false;
   }

   /// 在 ma 上面新增一個 AuthMgr.
   /// \retval nullptr   name 已存在.
   /// \retval !=nullptr 新增到 ma 的 AuthMgr 物件.
   static AuthMgrSP Plant(seed::MaTreeSP ma, InnDbfSP storage, std::string name) {
      return ma->Plant<AuthMgr>("AuthMgr.Plant", std::move(name), std::move(storage));
   }
};

} } // namespaces
#endif//__fon9_auth_AuthMgr_hpp__
