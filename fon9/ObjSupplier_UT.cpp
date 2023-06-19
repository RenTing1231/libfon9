// \file fon9/ObjSupplier_UT.cpp
// \author fonwinz@gmail.com
#include "fon9/ObjSupplier.hpp"
#include "fon9/TestTools.hpp"
#include "fon9/CmdArgs.hpp"

std::atomic<uint64_t>   gObjCtorCount{0};
std::atomic<uint64_t>   gObjDtorCount{0};

struct ObjCountChecker {
   ~ObjCountChecker() {
      std::cout << "\n[TEST ] Object ctor/dtor count"
         "|CtorCount=" << gObjCtorCount <<
         "|DtorCount=" << gObjDtorCount;
      if (gObjCtorCount == gObjDtorCount)
         std::cout << "\r[OK   ]\n" << std::endl;
      else {
         std::cout << "\r[ERROR]\n" << std::endl;
         abort();
      }
   }
};
// 要等 thread_local 物件解構之後, 才輪到 static 物件,
// 此時檢查 ctor/dtor 次數是否一致.
static ObjCountChecker  ObjCountChecker_;

struct ObjectT {
   char Dummy_[200];

   ObjectT() {
      ++gObjCtorCount;
   }
   virtual void FreeThis() {
      delete this;
   }

protected:
   virtual ~ObjectT() {
      ++gObjDtorCount;
   }
};

const unsigned kObjectCountInCarrier = 10000;
using ObjSupplier = fon9::ObjSupplier<ObjectT, kObjectCountInCarrier>;

//--------------------------------------------------------------------------//

int main(int argc, const char* argv[]) {
   fon9::AutoPrintTestInfo utinfo{"ObjSupplier"};
   fon9::GetDefaultTimerThread();
   std::this_thread::sleep_for(std::chrono::milliseconds{20});

   unsigned allocCount = fon9::StrTo(fon9::GetCmdArg(argc, argv, "a", "alloccount"), 0u);
   if (allocCount <= 0)
      allocCount = kObjectCountInCarrier * 2;

   std::cout << "Objects In Carrier = " << kObjectCountInCarrier << std::endl;
   std::cout << "  Test Alloc Count = " << allocCount << std::endl;
   auto supplier = ObjSupplier::Make();
   using ObjectV = std::vector<ObjectT*>;
   ObjectV  objs(allocCount * 2);
   auto     obji = objs.begin();
   fon9::StopWatch stopWatch;

   stopWatch.ResetTimer();
   for (unsigned L = allocCount; L > 0; --L)
      *obji++ = supplier->Alloc();
   stopWatch.PrintResult("Supplier", allocCount);

   stopWatch.ResetTimer();
   for (unsigned L = allocCount; L > 0; --L)
      *obji++ = new ObjectT{};
   stopWatch.PrintResult("::new   ", allocCount);

   for (auto x : objs)
      x->FreeThis();
}
