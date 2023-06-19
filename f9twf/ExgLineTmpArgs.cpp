// \file f9twf/ExgLineTmpArgs.cpp
// \author fonwinz@gmail.com
#include "f9twf/ExgLineTmpArgs.hpp"
#include "fon9/buffer/RevBufferList.hpp"
#include "fon9/PassKey.hpp"

namespace f9twf {

std::string ExgLineTmpArgs::Verify() const {
   if (TmpGetValueU(this->SessionFcmId_) == 0)
      return "Unknown FcmId";
   if (TmpGetValueU(this->SessionId_) == 0)
      return "Unknown SessionId";
   if (this->PassNum_ > 9999 && this->PassKey_.empty1st())
      return "Unknown Pass";
   if (!this->FcInterval_.IsNullOrZero() && this->FcInterval_.GetOrigValue() < 0)
      return "Bad FcInterval";
   return std::string{};
}
fon9::ConfigParser::Result ExgLineTmpArgs::OnTagValue(fon9::StrView tag, fon9::StrView& value) {
   if (fon9::iequals(tag, "ApCode"))
      this->ApCode_ = static_cast<TmpApCode>(fon9::StrTo(&value, fon9::underlying_type_t<TmpApCode>{}));
   else if (fon9::iequals(tag, "SessionId"))
      TmpPutValue(this->SessionId_, fon9::StrTo(&value, TmpSessionId_t{}));
   else if (fon9::iequals(tag, "FcmId"))
      TmpPutValue(this->SessionFcmId_, fon9::StrTo(&value, TmpFcmId_t{}));
   else if (fon9::iequals(tag, "Pass"))
      this->PassNum_ = fon9::StrTo(&value, this->PassNum_);
   else if (fon9::iequals(tag, "PassKey")) {
      this->PassKey_.AssignFrom(value);
      return fon9::ConfigParser::Result::Success;
   }
   else if (fon9::iequals(tag, "IsUseSymNum")) {
      this->IsUseSymNum_ = (value.Get1st() == 'Y');
      return fon9::ConfigParser::Result::Success;
   }
   else if (fon9::iequals(tag, "FcInterval"))
      this->FcInterval_ = fon9::StrTo(&value, this->FcInterval_);
   else if (fon9::iequals(tag, "LineFc"))
      this->LineFc_.OnValue(value);
   else
      return fon9::ConfigParser::Result::EUnknownTag;
   return value.empty() ? fon9::ConfigParser::Result::Success : fon9::ConfigParser::Result::EInvalidValue;
}

f9twf_API std::string ExgLineTmpArgsParser(ExgLineTmpArgs& args, fon9::StrView cfg) {
   return fon9::ParseFullConfig(args, cfg);
}
//--------------------------------------------------------------------------//
uint16_t ExgLineTmpArgs::GetPassNum() const {
   return static_cast<uint16_t>(PassKeyToPassNum(ToStrView(this->PassKey_), this->PassNum_));
}

} // namespaces
