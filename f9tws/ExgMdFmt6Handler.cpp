// \file f9tws/ExgMdFmt6Handler.cpp
// \author fonwinz@gmail.com
#include "f9tws/ExgMdFmt6Handler.hpp"
#include "f9tws/ExgMdFmt6.hpp"

namespace f9tws {

ExgMdFmt6Handler::~ExgMdFmt6Handler() {
}
//--------------------------------------------------------------------------//
void ExgMdFmt6Handler::PkContOnReceived(const void* pkptr, unsigned pksz, SeqT seq) {
   FmtRt_PkContOnReceived<f9fmkt_TradingSessionId_Normal>(
      *this, *TwsMdSys(*this).Symbs_,
      *static_cast<const ExgMdFmt6v4*>(pkptr), pksz, seq);
}
//--------------------------------------------------------------------------//
bool CheckPublishSymbSessionSt(ExgMdSymbs& symbs, ExgMdSymb& symb,
                               const fon9::fmkt::TwsBaseFlag matchingMethod,
                               const f9fmkt_TradingSessionSt tradingSessionSt,
                               const f9fmkt_TradingSessionId tradingSessionId) {
   if (fon9_LIKELY(fon9::fmkt::GetMatchingMethod(symb.TwsFlags_) == matchingMethod
                   && symb.TradingSessionSt_ == tradingSessionSt
                   && symb.TradingSessionId_ == tradingSessionId))
      return false;
   fon9::RevBufferList rts{64};
   const auto* tabBase = symbs.LayoutSP_->GetTab(0);
   assert(tabBase->Name_ == fon9_kCSTR_TabName_Base);
   fon9::seed::SimpleRawRd  rd{symb};
   if (symb.TradingSessionId_ != tradingSessionId) {
      symb.TradingSessionId_ = tradingSessionId;
      fon9::fmkt::MdRtsPackFieldValue(rts, *tabBase->Fields_.Get("Session"), rd);
   }
   if (fon9::fmkt::GetMatchingMethod(symb.TwsFlags_) != matchingMethod) {
      SetMatchingMethod(&symb.TwsFlags_, matchingMethod);
      fon9::fmkt::MdRtsPackFieldValue(rts, *tabBase->Fields_.Get("TwsFlags"), rd);
   }
   if (symb.TradingSessionSt_ != tradingSessionSt) {
      symb.TradingSessionSt_ = tradingSessionSt;
      fon9::fmkt::MdRtsPackFieldValue(rts, *tabBase->Fields_.Get("SessionSt"), rd);
   }
   symb.MdRtStream_.Publish(ToStrView(symb.SymbId_),
                            f9sv_RtsPackType_FieldValue_AndInfoTime,
                            f9sv_MdRtsKind_TradingSession,
                            symb.Deal_.Data_.DealTime(), std::move(rts));
   return true;
}
//--------------------------------------------------------------------------//

} // namespaces
