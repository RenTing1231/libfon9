// \file f9tws/ExgMdFmt1Handler.hpp
// \author fonwinz@gmail.com
#ifndef __f9tws_ExgMdFmt1Handler_hpp__
#define __f9tws_ExgMdFmt1Handler_hpp__
#include "f9tws/ExgMdSystem.hpp"

namespace f9tws {

class f9tws_API ExgMdFmt1TwseHandler : public ExgMdHandlerAnySeq {
   fon9_NON_COPY_NON_MOVE(ExgMdFmt1TwseHandler);
   void OnPkReceived(const ExgMdHead& pk, unsigned pksz) override;
public:
   using ExgMdHandlerAnySeq::ExgMdHandlerAnySeq;
   virtual ~ExgMdFmt1TwseHandler();
};

class f9tws_API ExgMdFmt1TpexHandler : public ExgMdHandlerAnySeq {
   fon9_NON_COPY_NON_MOVE(ExgMdFmt1TpexHandler);
   void OnPkReceived(const ExgMdHead& pk, unsigned pksz) override;
public:
   using ExgMdHandlerAnySeq::ExgMdHandlerAnySeq;
   virtual ~ExgMdFmt1TpexHandler();
};

class f9tws_API ExgMdFmt1V9TwseHandler : public ExgMdHandlerAnySeq {
   fon9_NON_COPY_NON_MOVE(ExgMdFmt1V9TwseHandler);
   void OnPkReceived(const ExgMdHead& pk, unsigned pksz) override;
public:
   using ExgMdHandlerAnySeq::ExgMdHandlerAnySeq;
   virtual ~ExgMdFmt1V9TwseHandler();
};

class f9tws_API ExgMdFmt1V9TpexHandler : public ExgMdHandlerAnySeq {
   fon9_NON_COPY_NON_MOVE(ExgMdFmt1V9TpexHandler);
   void OnPkReceived(const ExgMdHead& pk, unsigned pksz) override;
public:
   using ExgMdHandlerAnySeq::ExgMdHandlerAnySeq;
   virtual ~ExgMdFmt1V9TpexHandler();
};
//--------------------------------------------------------------------------//
struct ExgMdBaseInfoParser {
   fon9_NON_COPY_NON_MOVE(ExgMdBaseInfoParser);
   ExgMdSymbs&        Symbs_;
   ExgMdSymbs::Locker SymbLk_;
   ExgMdSymb* const   Symb_;
   const f9fmkt_TradingMarket       PrevTradingMarket_;
   char                             Padding___[3];
   const uint32_t                   PrevShUnit_;
   const fon9::fmkt::SymbRef::Data  PrevRefData_;
   const StkNameUTF8                PrevNameUTF8_;

   ExgMdBaseInfoParser(ExgMdSymbs& symbs, const StkNo& stkNo);

   template <class MdFmt>
   void ParseRef(const MdFmt& pk) {
      this->Symb_->Ref_.Data_.PriRef_.Assign<4>(fon9::PackBcdTo<uint64_t>(pk.PriRefV4_));
      this->Symb_->Ref_.Data_.PriUpLmt_.Assign<4>(fon9::PackBcdTo<uint64_t>(pk.PriUpLmtV4_));
      this->Symb_->Ref_.Data_.PriDnLmt_.Assign<4>(fon9::PackBcdTo<uint64_t>(pk.PriDnLmtV4_));
      SetMatchingMethod(&this->Symb_->TwsFlags_, (fon9::PackBcdTo<uint32_t>(pk.MatchingCycleSeconds_) > 0
                                                  ? fon9::fmkt::TwsBaseFlag::MatchingMethod_AggregateAuction
                                                  : fon9::fmkt::TwsBaseFlag::MatchingMethod_ContinuousMarket));

      switch (fon9::PackBcdTo<uint8_t>(pk.AnomalyCode_)) {
      case 0:
         this->Symb_->StkAnomalyCode_ = fon9::fmkt::StkAnomalyCode::Normal;
         break;
      default:
      case 1:
         this->Symb_->StkAnomalyCode_ = fon9::fmkt::StkAnomalyCode::Attention;
         break;
      case 2:
         this->Symb_->StkAnomalyCode_ = fon9::fmkt::StkAnomalyCode::Disposition;
         break;
      case 3:
         this->Symb_->StkAnomalyCode_ = fon9::fmkt::StkAnomalyCode::Attention | fon9::fmkt::StkAnomalyCode::Disposition;
         break;
      case 4:
         this->Symb_->StkAnomalyCode_ = fon9::fmkt::StkAnomalyCode::FurtherDisposition;
         break;
      case 5:
         this->Symb_->StkAnomalyCode_ = fon9::fmkt::StkAnomalyCode::Attention | fon9::fmkt::StkAnomalyCode::FurtherDisposition;
         break;
      case 6:
         this->Symb_->StkAnomalyCode_ = fon9::fmkt::StkAnomalyCode::FlexibleDisposition;
         break;
      case 7:
         this->Symb_->StkAnomalyCode_ = fon9::fmkt::StkAnomalyCode::Attention | fon9::fmkt::StkAnomalyCode::FlexibleDisposition;
         break;
      }
   }
   template <class MdFmt>
   void ParseShUnit(const MdFmt& pk) {
      this->Symb_->ShUnit_ = fon9::PackBcdTo<uint32_t>(pk.TradingUnit_);
   }
   template <class MdFmt>
   void ParseCTGCD(const MdFmt& pk) {
      (void)pk;
   }

   void Publish(ExgMdSystem& mdsys, const ExgMdHead& pk, unsigned pksz) const;
};

struct ExgMdBaseInfoParserV9 : public ExgMdBaseInfoParser {
   fon9_NON_COPY_NON_MOVE(ExgMdBaseInfoParserV9);

   using ExgMdBaseInfoParser::ExgMdBaseInfoParser;
   template <class MdFmt>
   void ParseCTGCD(const MdFmt& pk) {
      switch (pk.StkCTGCD_) {
      case ' ': case '0':
         this->Symb_->StkCTGCD_ = fon9::fmkt::StkCTGCD::Normal;
         break;
      default:
         this->Symb_->StkCTGCD_ = static_cast<fon9::fmkt::StkCTGCD>(pk.StkCTGCD_);
         break;
      }
      if (pk.IsMarginSale_BelowPriRef_ == 'Y')
         this->Symb_->TwsFlags_ |= fon9::fmkt::TwsBaseFlag::AllowDb_BelowPriRef;
      else
         this->Symb_->TwsFlags_ -= fon9::fmkt::TwsBaseFlag::AllowDb_BelowPriRef;

      if (pk.IsSecuritiesLendingSale_BelowPriRef_ == 'Y')
         this->Symb_->TwsFlags_ |= fon9::fmkt::TwsBaseFlag::AllowSBL_BelowPriRef;
      else
         this->Symb_->TwsFlags_ -= fon9::fmkt::TwsBaseFlag::AllowSBL_BelowPriRef;
   }
};

/// stockEntries = Fmt1.StockEntries_;
inline bool EdgMdFmt_IsStockEntries(ExgMdSystem& mdsys, const char (&stockEntries)[2], unsigned pksz) {
   if (stockEntries[0] == ' ')
      return false;
   // 避免記錄重複的 "AL" or "NE", 需要額外處理是否有記錄過 "AL" or "NE";
   // 為了避免無謂的工作, 在此先將此記錄功能拿掉, 如果有需要再處理.
   // mdsys.BaseInfoPkLog(fmt1, pksz);
   (void)mdsys; (void)pksz;
   return true;
}

template <class Parser, class MdFmt>
static inline void EdgMdParseBaseInfo(f9fmkt_TradingMarket mkt, ExgMdHandler& handler, ExgMdSymbs& symbs, const MdFmt& mdfmt, unsigned pksz) {
   if (fon9_UNLIKELY(EdgMdFmt_IsStockEntries(TwsMdSys(handler), mdfmt.StockEntries_, pksz)))
      return;
   Parser parser{symbs, mdfmt.StkNo_};
   parser.Symb_->TradingMarket_ = mkt;

   parser.ParseRef(mdfmt);
   parser.ParseShUnit(mdfmt);
   parser.ParseCTGCD(mdfmt);

   parser.Publish(TwsMdSys(handler), mdfmt, pksz);
}

} // namespaces
#endif//__f9tws_ExgMdFmt1Handler_hpp__
