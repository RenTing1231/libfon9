// \file f9tws/ExgMdFmt13Handler.cpp
// \author fonwinz@gmail.com
#include "f9tws/ExgMdFmt13Handler.hpp"
#include "f9tws/ExgMdFmt6Handler.hpp"
#include "f9tws/ExgMdFmt13.hpp"

namespace f9tws {

ExgMdFmt13Handler::~ExgMdFmt13Handler() {
}
//--------------------------------------------------------------------------//
#define kBSCount     fon9::fmkt::SymbBSData::kBSCount
inline void AssignOddLotBS(fon9::fmkt::PriQty (&dst)[kBSCount], const fon9::fmkt::Pri pri) {
   dst[0].Pri_ = pri;
   dst[0].Qty_ = 0;
   memset(static_cast<void*>(&dst[1]), 0, sizeof(dst[1]) * (kBSCount - 1));
}
static void PackOddLotBS(fon9::RevBuffer& rbuf, fon9::fmkt::RtBSType bsType, const fon9::fmkt::Pri pri) {
   fon9::RevPutBitv(rbuf, fon9_BitvV_Number0);
   fon9::ToBitv(rbuf, pri);
   *rbuf.AllocPacket<uint8_t>() = fon9::cast_to_underlying(bsType);
}
//--------------------------------------------------------------------------//
void ExgMdFmt13Handler::PkContOnReceived(const void* pkptr, unsigned pksz, SeqT seq) {
   namespace f9fmkt = fon9::fmkt;
   const auto& mdfmt = *static_cast<const ExgMdFmt13v3*>(pkptr);
   this->CheckLogLost(&mdfmt, seq);
   if (mdfmt.IsLastDealSent())
      return;
   const auto  dealTime = mdfmt.Time_.ToDayTime();
   auto&       symbs  = *TwsMdSys(*this).SymbsOdd_;
   auto        symblk = symbs.SymbMap_.Lock();
   auto        symb   = fon9::static_pointer_cast<ExgMdSymb>(symbs.FetchSymb(symblk, ToStrView(mdfmt.StkNo_)));
   // dealTime < 143000 表示為試算揭示.
   // dealTime < 143000 不揭示試算成交價，成交價格及成交股數傳送 0。
   // dealTime = 143000 即時傳送成交價格及股數；如沒有成交，成交價格及股數傳送 0。
   const bool  isCalc = (dealTime < fon9::TimeInterval_HHMMSS(14, 30, 00));
   CheckPublishSymbSessionSt(symbs, *symb,
                             f9fmkt::TwsBaseFlag::MatchingMethod_AggregateAuction,
                             (isCalc ? f9fmkt_TradingSessionSt_Open : f9fmkt_TradingSessionSt_Closed),
                             f9fmkt_TradingSessionId_OddLot);

   if (symb->Deal_.Data_.InfoTime_ == dealTime) // 不處理重複資料(序號判斷應該已經過濾掉了,這裡是做個保險).
      return;
   const f9fmkt::Qty dealQty = (isCalc ? 0 : fon9::PackBcdTo<fon9::fmkt::Qty>(mdfmt.TradingVolume_));
   if (dealQty > 0) {
      symb->Deal_.Data_.Flags_ = f9sv_DealFlag_DealTimeChanged;
      symb->Deal_.Data_.DealTime_ = symb->Deal_.Data_.InfoTime_ = dealTime;
      symb->Deal_.Data_.Deal_.Pri_.Assign<4>(fon9::PackBcdTo<uint64_t>(mdfmt.TradingPriceV4_));
      symb->Deal_.Data_.Deal_.Qty_ = dealQty;
      symb->Deal_.Data_.TotalQty_ += dealQty;
      symb->CheckOHL(symb->Deal_.Data_.Deal_.Pri_, dealTime);
      const auto lmtFlags = static_cast<f9sv_DealLmtFlag>(mdfmt.LmtMask_ & (0x80 | 0x40));
      if (symb->Deal_.Data_.LmtFlags_ != lmtFlags) {
         symb->Deal_.Data_.LmtFlags_ = lmtFlags;
         symb->Deal_.Data_.Flags_ |= f9sv_DealFlag_LmtFlagsChanged;
      }
   }
   const f9fmkt::Pri priBuy(fon9::PackBcdTo<uint64_t>(mdfmt.BuyPriceV4_), 4);
   const f9fmkt::Pri priSell(fon9::PackBcdTo<uint64_t>(mdfmt.SellPriceV4_), 4);
   const bool hasBS = (priSell != symb->BS_.Data_.Sells_[0].Pri_ || symb->BS_.Data_.Sells_[0].Qty_ != 0
                       || priBuy != symb->BS_.Data_.Buys_[0].Pri_ || symb->BS_.Data_.Buys_[0].Qty_ != 0);

   // 盤後零股交易, 使用零股盤中資料表.
   // symb->BS_.Data_.MarketSeq_ 序號必須 > 盤中序號, 否則 client 可能會認為是舊資料.
   // 交易所序號使用 PackBcd<8>, 所以加上 10^8;
   seq += 100000000;

   fon9::RevBufferList  rts{pksz};
   f9sv_RtsPackType     rtsPackType;
   f9sv_MdRtsKind       pkKind;
   if (hasBS) {
      symb->BS_.Data_.Flags_ = (isCalc ? f9sv_BSFlag_Calculated : f9sv_BSFlag{});
      symb->BS_.Data_.Flags_ |= (f9sv_BSFlag_OrderBuy | f9sv_BSFlag_OrderSell);
      AssignOddLotBS(symb->BS_.Data_.Sells_, priSell);
      AssignOddLotBS(symb->BS_.Data_.Buys_, priBuy);
      const auto bsLmtFlags = static_cast<f9sv_BSLmtFlag>(mdfmt.LmtMask_ & (0x20 | 0x10 | 0x08 | 0x04));
      if (symb->BS_.Data_.LmtFlags_ != bsLmtFlags) {
         *rts.AllocPacket<uint8_t>() = fon9::cast_to_underlying(symb->BS_.Data_.LmtFlags_ = bsLmtFlags);
         *rts.AllocPacket<uint8_t>() = fon9::cast_to_underlying(f9fmkt::RtBSSnapshotSpc::LmtFlags);
      }
      if (dealQty > 0) {
         rtsPackType = f9sv_RtsPackType_DealBS;
         pkKind = f9sv_MdRtsKind_Deal | f9sv_MdRtsKind_BS;
         PackOddLotBS(rts, f9fmkt::RtBSType::OrderSell, priSell);
         PackOddLotBS(rts, f9fmkt::RtBSType::OrderBuy,  priBuy);
         goto __ASSIGN_DEAL_TO_RTS;
      }
      else {
         rtsPackType = isCalc ? f9sv_RtsPackType_CalculatedBS : f9sv_RtsPackType_SnapshotBS;
         pkKind = f9sv_MdRtsKind_BS;
         PackOddLotBS(rts, f9fmkt::RtBSType::OrderSell, priSell);
         PackOddLotBS(rts, f9fmkt::RtBSType::OrderBuy,  priBuy);
      }
   }
   else if (dealQty > 0) {
      rtsPackType = f9sv_RtsPackType_DealPack;
      pkKind = f9sv_MdRtsKind_Deal;
   __ASSIGN_DEAL_TO_RTS:;
      fon9::ToBitv(rts, symb->Deal_.Data_.Deal_.Qty_);
      fon9::ToBitv(rts, symb->Deal_.Data_.Deal_.Pri_);
      *rts.AllocPacket<uint8_t>() = 0; // = 1筆成交.
      if (IsEnumContains(symb->Deal_.Data_.Flags_, f9sv_DealFlag_LmtFlagsChanged))
         *rts.AllocPacket<uint8_t>() = fon9::cast_to_underlying(symb->Deal_.Data_.LmtFlags_);
      if (IsEnumContains(symb->Deal_.Data_.Flags_, f9sv_DealFlag_TotalQtyLost))
         fon9::ToBitv(rts, symb->Deal_.Data_.TotalQty_ - symb->Deal_.Data_.Deal_.Qty_);
      if (IsEnumContains(symb->Deal_.Data_.Flags_, f9sv_DealFlag_DealTimeChanged))
         fon9::RevPutBitv(rts, fon9_BitvV_NumberNull);
      *rts.AllocPacket<uint8_t>() = fon9::cast_to_underlying(symb->Deal_.Data_.Flags_);
      symb->Deal_.Data_.MarketSeq_ = static_cast<f9fmkt::MarketDataSeq>(seq);
   }
   else { // none Deal, none BS?
      return;
   }

   f9fmkt::PackMktSeq(rts, symb->BS_.Data_.MarketSeq_, symbs.CtrlFlags_, static_cast<f9fmkt::MarketDataSeq>(seq));
   symb->MdRtStream_.Publish(ToStrView(symb->SymbId_), rtsPackType, pkKind, dealTime, std::move(rts));
}

} // namespaces
