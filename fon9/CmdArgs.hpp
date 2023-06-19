/// \file fon9/CmdArgs.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_CmdArgs_hpp__
#define __fon9_CmdArgs_hpp__
#include "fon9/StrView.hpp"

namespace fon9 {

struct CmdArgDef {
   StrView     Name_;
   StrView     DefaultValue_;
   StrView     ArgShort_;
   StrView     ArgLong_;
   const char* EnvName_;
   StrView     Title_;
   StrView     Description_;
};

/// \ingroup Misc
/// 從 argv or getenv() 取出環境設定, 搜尋順序:
/// - \ref GetCmdArg(int argc, const char* argv[], const StrView& argShort, const StrView& argLong)
/// - 如果 argv 沒找到, 則找環境變數: getenv(def.EnvName_);
/// - 如果 getenv(def.EnvName_) 也沒有, 就用預設值: def.DefaultValue_;
fon9_API StrView GetCmdArg(int argc, const char* argv[], const CmdArgDef& def);

/// \ingroup Misc
/// 從 argv 取出環境設定, 搜尋順序:
/// - argv: 從最後一個往前找: "-" + def.ArgShort_ 或 "--" + def.ArgLong_; 例如:
///   - def.ArgShort_=="c": "/c cfgpath" or "/c=cfgpath" or "-c cfgpath" or "-c=cfgpath"
///   - def.ArgLong_=="cfg": "--cfg=cfgpath" or "--cfg cfgpath"
/// - 如果 argv 沒找到, 則 StrView{nullptr};
fon9_API StrView GetCmdArg(int argc, const char* argv[], const StrView& argShort, const StrView& argLong);

/// \ingroup Misc
/// 設定執行時的參數.
/// 若有設定過, 則 GetCmdArg() 的 argv 為 nullptr 時, 使用此處的設定.
/// 預設在 Framework::Initialize() 會呼叫此處.
fon9_API void SetCmdArg(int argc, const char* argv[]);

/// \ingroup Misc
/// 設定執行時的參數.
/// 返回 SetCmdArg() 提供的 argv;
fon9_API const char** GetCmdArgv();

/// \ingroup Misc
/// 設定執行時的參數.
/// 返回 SetCmdArg() 提供的 argc;
fon9_API int GetCmdArgc();

} // namespaces
#endif//__fon9_CmdArgs_hpp__
