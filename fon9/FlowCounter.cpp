/// \file fon9/FlowCounter.cpp
/// \author fonwinz@gmail.com
#include "fon9/FlowCounter.hpp"

namespace fon9 {

ConfigParser::Result FlowCounterArgs::OnTagValue(StrView tag, StrView& value) {
   if (iequals(tag, "Fc"))
      return this->OnValue(value);
   return ConfigParser::Result::EUnknownTag;
}
ConfigParser::Result FlowCounterArgs::OnValue(StrView& value) {
   this->FcCount_ = StrTo(&value, this->FcCount_);
   if (StrTrimHead(&value).Get1st() == '/') {
      value.SetBegin(value.begin() + 1);
      this->FcTimeMS_ = StrTo(&value, this->FcTimeMS_);
   }
   return value.empty() ? ConfigParser::Result::Success : ConfigParser::Result::EInvalidValue;
}

} // namespace
