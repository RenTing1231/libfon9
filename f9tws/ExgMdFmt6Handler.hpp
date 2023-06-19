// \file f9tws/ExgMdFmt6Handler.hpp
// \author fonwinz@gmail.com
#ifndef __f9tws_ExgMdFmt6Handler_hpp__
#define __f9tws_ExgMdFmt6Handler_hpp__
#include "f9tws/ExgMdSystem.hpp"

namespace f9tws {

class f9tws_API ExgMdFmt6Handler : public ExgMdHandlerPkCont {
   fon9_NON_COPY_NON_MOVE(ExgMdFmt6Handler);
   void PkContOnReceived(const void* pk, unsigned pksz, SeqT seq) override;
public:
   using ExgMdHandlerPkCont::ExgMdHandlerPkCont;
   virtual ~ExgMdFmt6Handler();
};

bool CheckPublishSymbSessionSt(ExgMdSymbs& symbs, ExgMdSymb& symb,
                               const fon9::fmkt::TwsBaseFlag matchingMethod,
                               const f9fmkt_TradingSessionSt tradingSessionSt,
                               const f9fmkt_TradingSessionId tradingSessionId);
   
/// - kTradingSessionId == f9fmkt_TradingSessionId_Normal; MdFmt = Fmt6;  symbs = *handler.MdSys_.Symbs_;
/// - kTradingSessionId == f9fmkt_TradingSessionId_OddLot; MdFmt = Fmt23; symbs = *handler.MdSys_.SymbsOdd_;
template <f9fmkt_TradingSessionId kTradingSessionId, class MdFmt>
inline void FmtRt_PkContOnReceived(ExgMdHandlerPkCont& handler, ExgMdSymbs& symbs,
                                   const MdFmt& mdfmt, unsigned pksz, ExgMdHandlerPkCont::SeqT seq) {
   namespace f9fmkt = fon9::fmkt;
   handler.CheckLogLost(&mdfmt, seq);
   if (mdfmt.IsLastDealSent())
      return;
   const auto  dealTime = mdfmt.Time_.ToDayTime();
   auto        symblk = symbs.SymbMap_.Lock();
   auto        symb = fon9::static_pointer_cast<ExgMdSymb>(symbs.FetchSymb(symblk, ToStrView(mdfmt.StkNo_)));

   // #define DEBUG_FMTRT
#ifdef DEBUG_FMTRT
   const bool kDebugPrint = (/*ToStrView(symb->SymbId_) == "2885"//"2330"
                             ||*/ (mdfmt.StatusMask_ & 0x60) != 0
                             || (mdfmt.LmtMask_ & 0x03) != 0
                             );
   if (kDebugPrint) {
      printf("\n[%s]%06u/%02x.%02x.%02x/Total=%u/",
             symb->SymbId_.begin(),
             fon9::PackBcdTo<unsigned>(mdfmt.Time_.HHMMSS_),
             mdfmt.ItemMask_, mdfmt.LmtMask_, mdfmt.StatusMask_,
             fon9::PackBcdTo<unsigned>(mdfmt.TotalQty_));
   }
#endif
   const bool  isCalc = ((mdfmt.StatusMask_ & 0x80) != 0);
   const bool  hasDeal = ((mdfmt.ItemMask_ & 0x80) != 0);
   const auto* mdPQs = mdfmt.PQs_;
   if (hasDeal) {
      symb->Deal_.Data_.Flags_ = (isCalc ? f9sv_DealFlag_Calculated : f9sv_DealFlag{});
      if (symb->Deal_.Data_.InfoTime_ != dealTime) {
         symb->Deal_.Data_.DealTime_ = symb->Deal_.Data_.InfoTime_ = dealTime;
         symb->Deal_.Data_.Flags_ |= f9sv_DealFlag_DealTimeChanged;
      }
   #ifdef DEBUG_FMTRT
      if (kDebugPrint) {
         printf("Deal=%u*%u/",
                static_cast<unsigned>(fon9::PackBcdTo<uint64_t>(mdPQs->PriV4_)),
                fon9::PackBcdTo<unsigned>(mdPQs->Qty_));
      }
   #endif
      mdPQs->AssignTo(symb->Deal_.Data_.Deal_);
      ++mdPQs;
      // mdfmt.TotalQty_ 包含此次成交, 試算階段 DealQty = TotalQty, 所以不更新 TotalQty.
      if (!isCalc) {
         // 若有「暫緩撮合」, 則: DealQty=0, 並提供「瞬間價格趨勢」.
         // 此時不應改變 High/Low, 但仍應「發行 & 儲存」此訊息,
         // 訂閱者如果有回補成交明細, 則會收到此訊息(DealQty=0), 處理時要注意。
         if (symb->Deal_.Data_.Deal_.Qty_ > 0)
            symb->CheckOHL(symb->Deal_.Data_.Deal_.Pri_, dealTime);
         const f9fmkt::Qty pkTotalQty = fon9::PackBcdTo<f9fmkt::Qty>(mdfmt.TotalQty_);
         if (symb->Deal_.Data_.TotalQty_ + symb->Deal_.Data_.Deal_.Qty_ != pkTotalQty)
            symb->Deal_.Data_.Flags_ |= f9sv_DealFlag_TotalQtyLost;
         symb->Deal_.Data_.TotalQty_ = pkTotalQty;
      }
      // - Bit 1-0 瞬間價格趨勢 00：一般揭示; 11：（保留）
      //   - 01：暫緩撮合且瞬間趨跌; 10：暫緩撮合且瞬間趨漲;
      //     - 此時 DealPri=上次成交價; DealQty=0; TotalQty=不變;
      //     - 然後該股票的 mdfmt 就是大約2分鐘的「試算」揭示, 且沒有再提供「瞬間價格趨勢」.
      static_assert(f9sv_DealLmtFlag_HeldMatchTrendFall == 0x01, "");
      static_assert(f9sv_DealLmtFlag_HeldMatchTrendRise == 0x02, "");
      static_assert(f9sv_DealLmtFlag_UpLmt == 0x80, "");
      static_assert(f9sv_DealLmtFlag_DnLmt == 0x40, "");
      const auto lmtFlags = static_cast<f9sv_DealLmtFlag>(mdfmt.LmtMask_ & (0x80 | 0x40 | 0x01 | 0x02));
      if (symb->Deal_.Data_.LmtFlags_ != lmtFlags) {
         symb->Deal_.Data_.LmtFlags_ = lmtFlags;
         symb->Deal_.Data_.Flags_ |= f9sv_DealFlag_LmtFlagsChanged;
      }
   }
#ifdef DEBUG_FMTRT
   if (kDebugPrint)
      printf("%02x/", symb->Deal_.Data_.Flags_);
#endif

   f9fmkt_TradingSessionSt tradingSessionSt = symb->TradingSessionSt_;
   if (const auto stm = mdfmt.StatusMask_ & (0x40 | 0x20 | 0x08 | 0x04)) {
      switch (stm) {
      case 0x40: tradingSessionSt = f9fmkt_TradingSessionSt_DelayOpen;  break;
      case 0x20: tradingSessionSt = f9fmkt_TradingSessionSt_DelayClose; break;
      case 0x08: tradingSessionSt = f9fmkt_TradingSessionSt_Open;       break;
      case 0x04: tradingSessionSt = f9fmkt_TradingSessionSt_Closed;     break;
      }
   }
   const f9fmkt::TwsBaseFlag matchingMethod = ((mdfmt.StatusMask_ & 0x10)
                                               ? f9fmkt::TwsBaseFlag::MatchingMethod_ContinuousMarket
                                               : f9fmkt::TwsBaseFlag::MatchingMethod_AggregateAuction);
   CheckPublishSymbSessionSt(symbs, *symb, matchingMethod, tradingSessionSt, kTradingSessionId);

   fon9::RevBufferList  rts{pksz};
   f9sv_RtsPackType     rtsPackType;
   f9sv_MdRtsKind       pkKind;
   if (mdfmt.HasBS()) {
      symb->BS_.Data_.InfoTime_ = dealTime;
      symb->BS_.Data_.Flags_ = (isCalc ? f9sv_BSFlag_Calculated : f9sv_BSFlag{});
      symb->BS_.Data_.Flags_ |= (f9sv_BSFlag_OrderBuy | f9sv_BSFlag_OrderSell);
      // AssignBS() 在 ExgMdFmt6.hpp
      mdPQs = AssignBS(symb->BS_.Data_.Buys_, mdPQs, static_cast<unsigned>((mdfmt.ItemMask_ & 0x70) >> 4));
      mdPQs = AssignBS(symb->BS_.Data_.Sells_, mdPQs, static_cast<unsigned>((mdfmt.ItemMask_ & 0x0e) >> 1));

      static_assert(f9sv_BSLmtFlag_UpLmtBuy == 0x20, "");
      static_assert(f9sv_BSLmtFlag_DnLmtBuy == 0x10, "");
      static_assert(f9sv_BSLmtFlag_UpLmtSell == 0x08, "");
      static_assert(f9sv_BSLmtFlag_DnLmtSell == 0x04, "");
      const auto bsLmtFlags = static_cast<f9sv_BSLmtFlag>(mdfmt.LmtMask_ & (0x20 | 0x10 | 0x08 | 0x04));
      if (symb->BS_.Data_.LmtFlags_ != bsLmtFlags) {
         *rts.AllocPacket<uint8_t>() = fon9::cast_to_underlying(symb->BS_.Data_.LmtFlags_ = bsLmtFlags);
         *rts.AllocPacket<uint8_t>() = fon9::cast_to_underlying(f9fmkt::RtBSSnapshotSpc::LmtFlags);
      }
      symb->BS_.Data_.MarketSeq_ = static_cast<f9fmkt::MarketDataSeq>(seq);
      if (hasDeal) {
         rtsPackType = f9sv_RtsPackType_DealBS;
         pkKind = f9sv_MdRtsKind_Deal | f9sv_MdRtsKind_BS;
         MdRtsPackSnapshotBS(rts, symb->BS_.Data_);
         goto __ASSIGN_DEAL_TO_RTS;
      }
      else {
         rtsPackType = isCalc ? f9sv_RtsPackType_CalculatedBS : f9sv_RtsPackType_SnapshotBS;
         pkKind = f9sv_MdRtsKind_BS;
         MdRtsPackSnapshotBS(rts, symb->BS_.Data_);
      }
   }
   else if (hasDeal) {
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
   f9fmkt::PackMktSeq(rts, symbs.CtrlFlags_, static_cast<f9fmkt::MarketDataSeq>(seq));
   symb->MdRtStream_.Publish(ToStrView(symb->SymbId_), rtsPackType, pkKind, dealTime, std::move(rts));
}

} // namespaces
#endif//__f9tws_ExgMdFmt6Handler_hpp__
