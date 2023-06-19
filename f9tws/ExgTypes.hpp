// \file f9tws/ExgTypes.hpp
// \author fonwinz@gmail.com
#ifndef __f9tws_ExgTypes_hpp__
#define __f9tws_ExgTypes_hpp__
#include "f9tws/Config.h"
#include "fon9/CharAry.hpp"

namespace f9tws {

using StkNo = fon9::CharAryP<6, 4, char, ' '>;
using BrkId = fon9::CharAryF<4>;
using OrdNo = fon9::CharAryF<5>;

/// TWSE FIX 的 ClOrdID 固定使用 12 bytes, 若不足要補空白.
using ClOrdID = fon9::CharAryF<12, char, ' '>;

//--------------------------------------------------------------------------//

enum class TwsOType : char {
   /// 現股.
   Gn = '0',
   /// 代辦融資.
   CrAgent = '1',
   /// 代辦融券.
   DbAgent = '2',
   /// 自辦融資.
   CrSelf = '3',
   /// 自辦融券.
   DbSelf = '4',
   /// 借券賣出.
   SBL5 = '5',
   /// 避險套利借券賣出.
   SBL6 = '6',

   /// 信用當沖.
   DayTradeCD = 'c',
   /// 現股當沖.
   DayTradeGn = 'd',
};
static inline bool IsTwsOTypeDb(TwsOType ot) {
   return ot == TwsOType::DbSelf || ot == TwsOType::DbAgent;
}
static inline bool IsTwsOTypeCr(TwsOType ot) {
   return ot == TwsOType::CrSelf || ot == TwsOType::CrAgent;
}
static inline bool IsTwsOTypeSBL(TwsOType ot) {
   return ot == TwsOType::SBL5 || ot == TwsOType::SBL6;
}

enum class TwsApCode : char {
   /// '0' TWSE:普通股交易子系統 / OTC:等價交易子系統.
   Regular = '0',
   /// '7' 盤後定價交易子系統.
   FixedPrice = '7',
   /// '2' 盤後零股交易子系統.
   OddLot2 = '2',
   /// 'C' 盤中零股交易子系統.
   OddLotC = 'C',

   /// '3' 成交回報子系統
   Match3 = '3',
   /// 'A' 成交回報子系統(擴充格式)
   MatchA = 'A',

   /// '4' 標借交易子系統.
   LendingAuct = '4',
   /// '5' 拍賣交易子系統.
   Auction = '5',

   /// '6' TWSE:標購交易子系統
   TenderOfferTWSE = '6',

   /// '6' OTC:自營商營業處所議價子系統.
   NegotiationOTC = '6',
   /// '8' OTC:標購交易子系統.
   TenderOfferOTC = '8',

   /// '1' 單筆訊息與檔案傳輸子系統.
   FileTx = '1',
};

enum class TwsExchangeCode : char {
   /// 0-一般委託.
   Regular = '0',
   /// 1-鉅額.
   Block = '1',
   /// 2-零股.
   OddLot = '2',
   /// 3-外國股票委託價格超過確認上/下界之確認.
   FrOver = '3',
};

enum class TwsPriType : char {
   /// 市價 (僅適用於逐筆交易時段).
   Market = '1',
   /// 限價. 
   Limit = '2',
};

enum class TwsTimeInForce : char {
   /// 當日有效.
   ROD = '0',
   /// 立即成交否則取消 (僅適用於逐筆交易時段).
   IOC = '3',
   /// 立即全部成交否則取消 (僅適用於逐筆交易時段).
   FOK = '4',
};

} // namespaces
#endif//__f9tws_ExgTypes_hpp__
