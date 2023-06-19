// \file f9tws/ExgTradingLineFix.cpp
// \author fonwinz@gmail.com
#include "f9tws/ExgTradingLineFix.hpp"
#include "fon9/fix/FixAdminDef.hpp"
#include "fon9/FilePath.hpp"

namespace f9tws {

// 台灣證交所規定 10 秒.
constexpr uint32_t   kHeartBtInt = 10;

//--------------------------------------------------------------------------//

fon9::ConfigParser::Result ExgTradingLineFixArgs::OnTagValue(fon9::StrView tag, fon9::StrView& value) {
   if (fon9::iequals(tag, "LineFc"))
      return this->LineFc_.OnValue(value);
   fon9::ConfigParser::Result r = this->FcArgs_.OnTagValue(tag, value);
   if (r == fon9::ConfigParser::Result::EUnknownTag)
      return base::OnTagValue(tag, value);
   return r;
}

f9tws_API std::string ExgTradingLineFixArgsParser(ExgTradingLineFixArgs& args, fon9::StrView cfg) {
   return fon9::ParseFullConfig(args, cfg);
}

//--------------------------------------------------------------------------//

static f9fix::CompIDs MakeCompIDs(const ExgTradingLineFixArgs& args) {
   static_assert(f9fmkt_TradingMarket_TwSEC == 'T' && f9fmkt_TradingMarket_TwOTC == 'O',
                "f9fmkt_TradingMarket_TwSEC == 'T' && f9fmkt_TradingMarket_TwOTC == 'O'");
   char  senderCompID[7];
   senderCompID[0] = args.Market_;
   memcpy(senderCompID + 1, args.BrkId_.data(), 4);
   memcpy(senderCompID + 5, args.SocketId_.data(), 2);
   return f9fix::CompIDs(fon9::StrView_all(senderCompID), nullptr,
            args.Market_ == f9fmkt_TradingMarket_TwSEC ? fon9::StrView{"XTAI"} : fon9::StrView{"ROCO"}, nullptr);
}

f9tws_API std::string MakeExgTradingLineFixSender(const ExgTradingLineFixArgs& args, fon9::StrView recPath, f9fix::IoFixSenderSP& out) {
   /// - 上市 retval->Initialize(recPath + "FIX44_XTAI_BrkId_SocketId.log");
   /// - 上櫃 retval->Initialize(recPath + "FIX44_ROCO_BrkId_SocketId.log");
   f9fix::IoFixSenderSP fixSender{new f9fix::IoFixSender{f9fix_BEGIN_HEADER_V44, MakeCompIDs(args)}};
   std::string          fileName = fon9::FilePath::AppendPathTail(recPath);
   fileName += "FIX44_";
   fixSender->CompIDs_.Target_.CompID_.AppendTo(fileName); // "XTAI" or "ROCO"
   fileName.push_back('_');
   fixSender->CompIDs_.Sender_.CompID_.AppendTo(fileName); // ('T' or 'O') + BrkId + SocketId
   fileName.append(".log");
   auto res = fixSender->GetFixRecorder().Initialize(fileName);
   if (res.IsError())
      return fon9::RevPrintTo<std::string>("MakeExgTradingLineFixSender|fn=", fileName, '|', res);
   out = std::move(fixSender);
   return std::string{};
}

//--------------------------------------------------------------------------//

ExgTradingLineFix::ExgTradingLineFix(f9fix::IoFixManager&         mgr,
                                     const f9fix::FixConfig&      fixcfg,
                                     const ExgTradingLineFixArgs& lineargs,
                                     f9fix::IoFixSenderSP&&       fixSender)
   : base(mgr, fixcfg)
   , baseTradingLine(lineargs.LineFc_)
   , RawAppendNo_{static_cast<unsigned>(fon9::UtcNow().GetDecPart() / 1000)}
   , FlowCounter_{lineargs.FcArgs_}
   , LineArgs_(lineargs)
   , FixSender_{std::move(fixSender)} {
}
void ExgTradingLineFix::OnFixSessionConnected() {
   base::OnFixSessionConnected();
   this->FixSender_->OnFixSessionConnected(this->GetDevice());
   f9fix::FixBuilder fixb;
   // 由證券商每次隨機產生一組三位數字。 001 <= APPEND-NO <= 999。此值不能與前五次登入使用相同之值。
   uint32_t app = (this->RawAppendNo_ + 1) % 1000;
   if (app == 0)
      app = 1;
   this->RawAppendNo_ = app;
   // KEY-VALUE = (APPEND-NO * PASSWORD)取千與百二位數字。
   // APPEND-NO + KEY-VALUE: 3 digits + 2 digits
   #define kRawDataLength     5
   app = (app * 100) + (((app * this->LineArgs_.GetPassNum()) / 100) % 100);
   auto pOutRawData = fixb.GetBuffer().AllocPrefix(kRawDataLength);
   fixb.GetBuffer().SetPrefixUsed(fon9::Pic9ToStrRev<kRawDataLength>(pOutRawData, app));
   fon9::RevPrint(fixb.GetBuffer(), f9fix_kFLD_EncryptMethod_None
                  f9fix_SPLTAGEQ(RawDataLength) fon9_CTXTOCSTR(kRawDataLength)
                  f9fix_SPLTAGEQ(RawData));
   this->SendLogon(this->FixSender_,
                   this->LineArgs_.HbInterval_ ? this->LineArgs_.HbInterval_ : kHeartBtInt,
                   std::move(fixb));
}
f9fix::FixSenderSP ExgTradingLineFix::OnFixSessionDisconnected(const fon9::StrView& info) {
   this->FixSender_->OnFixSessionDisconnected();
   return base::OnFixSessionDisconnected(info);
}

} // namespaces
