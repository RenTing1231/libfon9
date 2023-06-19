// \file f9twf/ExgLineTmpArgs.hpp
// \author fonwinz@gmail.com
#ifndef __f9twf_ExgLineTmpArgs_hpp__
#define __f9twf_ExgLineTmpArgs_hpp__
#include "f9twf/ExgTmpTypes.hpp"
#include "fon9/ConfigParser.hpp"
#include "fon9/FlowCounter.hpp"

namespace f9twf {

struct f9twf_API ExgLineTmpArgs {
   TmpApCode               ApCode_;
   TmpSessionId            SessionId_;
   TmpFcmId                SessionFcmId_;
   bool                    IsUseSymNum_;
   uint16_t                PassNum_;
   fon9::CharAry<16>       PassKey_;
   fon9::TimeInterval      FcInterval_;
   fon9::FlowCounterArgs   LineFc_;
   char                    Padding____[4];

   void Clear() {
      fon9::ForceZeroNonTrivial(this);
   }
   fon9::ConfigParser::Result OnTagValue(fon9::StrView tag, fon9::StrView& value);
   std::string Verify() const;

   uint16_t GetPassNum() const;

   fon9::TimeInterval GetFcInterval() const {
      return this->FcInterval_.IsNullOrZero()
         ? fon9::TimeInterval_Second(1) : this->FcInterval_;
   }
};

/// - cfg = "FcmId=數字代號|SessionId=|Pass=|ApCode="; 每個欄位都必須提供.
/// - 若有提供 "PassKey=" 則必須自定 FnGetPassNum;
/// - 額外的設定 "IsUseSymNum=Y";  送給期交所的 SymbId, 使用數字格式.
///   預設使用文字格式(根據 SymbolId 的長度決定使用 LongId or ShortId).
/// - retval.empty() 成功, retval = 失敗訊息.
f9twf_API std::string ExgLineTmpArgsParser(ExgLineTmpArgs& args, fon9::StrView cfg);

} // namespaces
#endif//__f9twf_ExgLineTmpArgs_hpp__
