// \file f9tws/ExgTradingLineFixFactory.cpp
// \author fonwinz@gmail.com
#include "f9tws/ExgTradingLineFixFactory.hpp"
#include "fon9/fix/FixBusinessReject.hpp"

namespace f9tws {

ExgTradingLineFixFactory::ExgTradingLineFixFactory(std::string fixLogPathFmt, Named&& name)
   : base(std::move(fixLogPathFmt), std::move(name)) {
   ExgTradingLineFix::InitFixConfig(this->FixConfig_);
   f9fix::InitRecvRejectMessage(this->FixConfig_);
}

fon9::io::SessionSP ExgTradingLineFixFactory::CreateTradingLine(ExgTradingLineMgr& lineMgr,
                                                                const fon9::IoConfigItem& cfg,
                                                                std::string& errReason) {
   ExgTradingLineFixArgs args;
   args.Market_ = lineMgr.Market_;
   errReason = ExgTradingLineFixArgsParser(args, ToStrView(cfg.SessionArgs_));
   if (!errReason.empty())
      return fon9::io::SessionSP{};

   std::string fixLogPath;
   errReason = this->MakeLogPath(fixLogPath);
   if (!errReason.empty())
      return fon9::io::SessionSP{};

   fon9::fix::IoFixSenderSP fixSender;
   errReason = MakeExgTradingLineFixSender(args, &fixLogPath, fixSender);
   if (!errReason.empty())
      return fon9::io::SessionSP{};

   return this->CreateTradingLineFix(lineMgr, args, std::move(fixSender));
}

} // namespaces
