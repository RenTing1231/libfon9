// \file fon9/fmkt/SymbTwsBase.hpp
// \author fonwinz@gmail.com
#ifndef __fon9_fmkt_SymbTwsBase_hpp__
#define __fon9_fmkt_SymbTwsBase_hpp__
#include "fon9/seed/Tab.hpp"

namespace fon9 { namespace fmkt {

enum class TwsBaseFlag : uint8_t {
   MatchingMethodMask = 0x03,
   /// 逐筆撮合.
   MatchingMethod_ContinuousMarket = 0x01,
   /// 集合競價.
   /// - T30.MatchInterval > 0 時, 會設定此旗標.
   /// - 行情: Fmt1.MatchingCycleSeconds > 0; 設定 AggregateAuction;
   ///         Fmt1.MatchingCycleSeconds <= 0; 設定 ContinuousMarket;
   /// - 行情: Fmt6.StatusMask_(Bit 4 撮合方式註記 0:AggregateAuction; 1:ContinuousMarket)
   MatchingMethod_AggregateAuction = 0x02,

   /// 變更交易股票(全額交割:下單時需先收足款券)
   AlteredTradingMethod = 0x04,

   /// 豁免平盤下融券賣出.
   AllowDb_BelowPriRef = 0x10,
   /// 豁免平盤下借券賣出.
   AllowSBL_BelowPriRef = 0x20,
};
fon9_ENABLE_ENUM_BITWISE_OP(TwsBaseFlag);
/// 傳回:
/// TwsBaseFlag::MatchingMethod_ContinuousMarket = 逐筆撮合;
/// TwsBaseFlag::MatchingMethod_AggregateAuction = 集合競價;
/// TwsBaseFlag{} 尚未提供此資訊;
static inline TwsBaseFlag GetMatchingMethod(TwsBaseFlag f) {
   return f & TwsBaseFlag::MatchingMethodMask;
}
static inline void SetMatchingMethod(TwsBaseFlag* pVal, TwsBaseFlag vNew) {
   assert(vNew == TwsBaseFlag::MatchingMethod_ContinuousMarket || vNew == TwsBaseFlag::MatchingMethod_AggregateAuction);
   *pVal = (*pVal - TwsBaseFlag::MatchingMethodMask) | vNew;
}
static inline bool IsMatchingMethod_ContinuousMarket(TwsBaseFlag f) {
   return GetMatchingMethod(f) == TwsBaseFlag::MatchingMethod_ContinuousMarket;
}
static inline bool IsMatchingMethod_AggregateAuction(TwsBaseFlag f) {
   return GetMatchingMethod(f) == TwsBaseFlag::MatchingMethod_AggregateAuction;
}

enum class StkCTGCD : char {
   /// 一般板.
   Normal = '\0',
   /// 上市創新板.
   Innovation = '3',
   /// 上櫃戰略板.
   Pioneer = '3'
};

/// 股票異常代碼.
enum class StkAnomalyCode : uint8_t {
   Normal = 0,
   /// 注意.
   Attention = 0x01,
   /// 處置.
   Disposition = 0x02,
   /// 再次處置.
   FurtherDisposition = 0x04,
   /// 彈性處置.
   FlexibleDisposition = 0x08,
};
fon9_ENABLE_ENUM_BITWISE_OP(StkAnomalyCode);
static inline bool IsAnyAnomalyDisposition(StkAnomalyCode v) {
   return IsEnumContainsAny(v, StkAnomalyCode::Disposition | StkAnomalyCode::FurtherDisposition | StkAnomalyCode::FlexibleDisposition);
}

/// \ingroup fmkt
/// 台灣證券預設的商品基本資料.
/// - ShUnit
struct fon9_API SymbTwsBase {
   /// 台灣證券「一張」的單位數(股數)
   uint32_t       ShUnit_{0};
   TwsBaseFlag    TwsFlags_{};
   StkCTGCD       StkCTGCD_{};
   StkAnomalyCode StkAnomalyCode_{};
   char           Padding___[1];

   void SymbTwsBaseDailyClear() {
   }
   void SymbTwsBaseSessionClear() {
   }

   template <class Derived>
   static void AddFields(seed::Fields& flds) {
      AddFields(fon9_OffsetOfBase(Derived, SymbTwsBase), flds);
   }
   static void AddFields(int ofsadj, seed::Fields& flds);
};

/// 取得台灣證券的整股交易單位, 若沒設定, 則 1張 = 1000股.
inline uint32_t GetSymbTwsShUnit(const SymbTwsBase* symb) {
   if (symb && 0 < symb->ShUnit_)
      return symb->ShUnit_;
   return 1000;
}

} } // namespaces
#endif//__fon9_fmkt_SymbTwsBase_hpp__
