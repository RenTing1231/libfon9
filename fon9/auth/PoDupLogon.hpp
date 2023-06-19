/// \file fon9/auth/PoDupLogon.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_auth_PoDupLogon_hpp__
#define __fon9_auth_PoDupLogon_hpp__
#include "fon9/auth/AuthMgr.hpp"

namespace fon9 { namespace auth {

struct PoDupLogonOnline;

/// \ingroup auth
/// 被 PoDupLogonAgent 管理的線上登入;
class fon9_API PoDupLogonClient {
   fon9_NON_COPY_NON_MOVE(PoDupLogonClient);

   virtual void PoDupLogonClient_ForceLogout(StrView reason) = 0;
   virtual unsigned PoDupLogonClient_AddRef() const = 0;
   virtual unsigned PoDupLogonClient_Release() const = 0;
   friend unsigned intrusive_ptr_add_ref(const PoDupLogonClient* p) {
      return p->PoDupLogonClient_AddRef();
   }
   friend unsigned intrusive_ptr_release(const PoDupLogonClient* p) {
      return p->PoDupLogonClient_Release();
   }

   friend struct PoDupLogonOnline;
   PoDupLogonOnline* Online_{};

protected:
   PoDupLogonClient() = default;
   virtual ~PoDupLogonClient();

public:
   PoDupLogonOnline* Online() const {
      return this->Online_;
   }
};
using PoDupLogonClientSP = intrusive_ptr<PoDupLogonClient>;

enum class PoDupLogonAct : char {
   RejectNew,
   RejectOld = 'o',
};
static inline bool Is_PoDupLogonAct_RejectNew(PoDupLogonAct v) {
   return v != PoDupLogonAct::RejectOld;
}
static inline bool Is_PoDupLogonAct_RejectOld(PoDupLogonAct v) {
   return v == PoDupLogonAct::RejectOld;
}

/// \ingroup auth
/// 重複登入處理政策管理員.
/// - 設定重複登入時的處理原則.
/// - 由各「登入處理」主動要求上線檢查.
/// - 管理線上使用者(DetailTree = Online User Mgr):
///    該使用者的登入明細表;
///    可支援 SeedCommand: reject;
class fon9_API PoDupLogonAgent : public PolicyAgent {
   fon9_NON_COPY_NON_MOVE(PoDupLogonAgent);
   using base = PolicyAgent;

public:
   PoDupLogonAgent(seed::MaTree* authMgrAgents, std::string name);

   #define fon9_kCSTR_PoDupLogonAgent_Name    "PoDupLogon"
   /// 在 authMgr.Agents_ 上面新增一個 PoDupLogonAgent.
   /// \retval nullptr   name 已存在.
   /// \retval !=nullptr 新增成功的 PoDupLogonAgent 物件, 返回前會先執行 LinkStorage().
   static intrusive_ptr<PoDupLogonAgent> Plant(AuthMgr& authMgr, std::string name = fon9_kCSTR_PoDupLogonAgent_Name) {
      auto res = authMgr.Agents_->Plant<PoDupLogonAgent>("PoDupLogonAgent.Plant", std::move(name));
      if (res)
         res->LinkStorage(authMgr.Storage_, 128);
      return res;
   }

   /// 若有重複登入, 則依照處理原則決定: 接受此次新登入要求, 或關閉舊的線上登入;
   /// - 必須由使用者主動呼叫:
   ///   例: FIX 密碼檢查通過後,
   ///       呼叫 PoDupLogonAgent::CheckLogon(); 檢查是否允許登入;
   /// - 重複登入處理原則的設定 Key = UserId.ProtocolId; 或 UserId;
   ///   - 優先使用加上 ProtocolId 的設定, 例: "fonwin.FIX"; "fonwin.Rc";
   ///   - 如果找不到有 ProtocolId 的設定, 才使用 UserId 的設定,
   ///     例: "fonwin"; 此時所有 ProtocolId 共用登入次數;
   ///   - 若找不到設定, 則不納入管理, 且不限制登入次數.
   ///
   /// \retval true
   ///   - 允許登入, 若有該設定, 則將 cli 加入 Online user;
   ///   - 若有超過最多上線數量, 則已強制登出最早上線者;
   ///   - rcode 內容不變;
   /// \retval false
   ///   - 超過允許的最多上線數量, 呼叫端必須拒絕此次連線;
   ///   - 有設定「綁定來源」但來源不符預期;
   ///   - 此時 rcode 會填入適當的錯誤碼及訊息;
   bool CheckLogon(const AuthResult& authr, const StrView& protocolId, const StrView& userFrom,
                   PoDupLogonClientSP cli, auth::AuthR& rcode);
   /// 若沒有找到 policy 設定, 則 cfg = PoDupLogonAct::RejectOld;
   bool CheckLogonR(const AuthResult& authr, const StrView& protocolId, const StrView& userFrom,
                    PoDupLogonClientSP cli, auth::AuthR& rcode, PoDupLogonAct& cfg);
   PoDupLogonAct GetDupLogonAct(const StrView& userId, const StrView& protocolId) const;

   /// 當線上使用者登出時, 釋放登入次數;
   void OnLogonClientClosed(PoDupLogonClient& cli);
};
using PoDupLogonAgentSP = intrusive_ptr<PoDupLogonAgent>;

} } // namespaces
#endif//__fon9_auth_PoDupLogon_hpp__
