// \file f9tws/ExgLineArgs.cpp
// \author fonwinz@gmail.com
#include "f9tws/ExgLineArgs.hpp"
#include "fon9/PassKey.hpp"

namespace f9tws {

void ExgLineArgs::Clear() {
   this->BrkId_.Clear(' ');
   this->SocketId_.Clear(' ');
   this->PassKey_.Clear();
   this->PassNum_ = static_cast<uint16_t>(-1);
   this->HbInterval_ = 0;
}

template <class CharAryT>
static fon9::ConfigParser::Result CopyToCharAry(CharAryT& dst, fon9::StrView& value) {
   if (value.size() != sizeof(dst))
      return fon9::ConfigParser::Result::EInvalidValue;
   memcpy(dst.Chars_, value.begin(), sizeof(dst));
   return fon9::ConfigParser::Result::Success;
}

fon9::ConfigParser::Result ExgLineArgs::OnTagValue(fon9::StrView tag, fon9::StrView& value) {
   if (fon9::iequals(tag, "BrkId"))
      return CopyToCharAry(this->BrkId_, value);
   else if (fon9::iequals(tag, "SocketId") || fon9::iequals(tag, "PvcId"))
      return CopyToCharAry(this->SocketId_, value);
   else if (fon9::iequals(tag, "Pass"))
      this->PassNum_ = fon9::StrTo(&value, this->PassNum_);
   else if (fon9::iequals(tag, "PassKey")) {
      this->PassKey_.AssignFrom(value);
      return fon9::ConfigParser::Result::Success;
   }
   else if (fon9::iequals(tag, "HbInt"))
      this->HbInterval_ = fon9::StrTo(&value, this->HbInterval_);
   else {
      return fon9::ConfigParser::Result::EUnknownTag;
   }
   return value.empty() ? fon9::ConfigParser::Result::Success : fon9::ConfigParser::Result::EInvalidValue;
}

std::string ExgLineArgs::Verify() const {
   if (fon9::isspace(this->BrkId_.Chars_[0]))
      return "Unknown BrkId";
   if (fon9::isspace(this->SocketId_.Chars_[0]))
      return "Unknown SocketId/PvcId";
   if (this->PassNum_ > 9999 && this->PassKey_.empty1st())
      return "Unknown Pass";
   return std::string{};
}
//--------------------------------------------------------------------------//
uint16_t ExgLineArgs::GetPassNum() const {
   return static_cast<uint16_t>(PassKeyToPassNum(ToStrView(this->PassKey_), this->PassNum_));
}

} // namespaces
