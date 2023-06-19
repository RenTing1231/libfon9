/// \file fon9/CmdArgs.cpp
/// \author fonwinz@gmail.com
#define _CRT_SECURE_NO_WARNINGS  // Windows: getenv()
#include "fon9/CmdArgs.hpp"

namespace fon9 {

static int           gArgc;
static const char**  gArgv;
void SetCmdArg(int argc, const char* argv[]) {
   gArgc = argc;
   gArgv = argv;
}
const char** GetCmdArgv() {
   return gArgv;
}
int GetCmdArgc() {
   return gArgc;
}

StrView GetCmdArg(int argc, const char* argv[], const StrView& argShort, const StrView& argLong) {
   if (argv == nullptr) {
      argv = gArgv;
      argc = gArgc;
   }
   if (argc > 1 && argv != nullptr && (!argShort.empty() || !argLong.empty())) {
      for (int L = argc; L > 0;) {
         const char*    vstr = argv[--L];
         const StrView* argName;
         const char     chLead = vstr[0];
         switch (chLead) {
         default:
            continue;
         case '-':
            if (vstr[1] == '-') { // --argLong
               argName = &argLong;
               vstr += 2;
               break;
            }
            /* fall through */  // 只有一個 '-': 使用 -argShort
         case '/':
            argName = &argShort;
            ++vstr;
            break;
         }
         if (argName->empty())
            continue;
         StrView           opt = StrView_cstr(vstr);
         const char* const pvalue = opt.Find('=');
         vstr = opt.end();
         if (pvalue)
            opt = StrView{opt.begin(), pvalue};
         if (opt == *argName) {
            if (pvalue)
               return StrView{pvalue + 1, vstr};
            if (L + 1 >= argc)
               return StrView{""};
            vstr = argv[L + 1];
            if (vstr[0] == chLead)
               return StrView{""};
            return StrView_cstr(vstr);
         }
      }
   }
   return StrView{nullptr};
}
StrView GetCmdArg(int argc, const char* argv[], const CmdArgDef& def) {
   StrView retval = GetCmdArg(argc, argv, def.ArgShort_, def.ArgLong_);
   if (!retval.empty())
      return retval;
   if (def.EnvName_ && *def.EnvName_)
      if (const char* envstr = getenv(def.EnvName_))
         return StrView_cstr(envstr);
   return def.DefaultValue_;
}

} // namespaces
