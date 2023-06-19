// \file fon9/framework/Fon9Co_main.cpp
// fon9 console: 直接轉呼叫 fon9::Fon9CoRun();
// \author fonwinz@gmail.com
#include "fon9/framework/Framework.hpp"

int main(int argc, const char* argv[]) {
   return fon9::Fon9CoRun(argc, argv, nullptr);
}
