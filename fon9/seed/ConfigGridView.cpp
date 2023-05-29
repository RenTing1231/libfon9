﻿// \file fon9/seed/ConfigGridView.cpp
// \author fonwinz@gmail.com
#include "fon9/seed/ConfigGridView.hpp"
#include "fon9/RevPrint.hpp"

namespace fon9 { namespace seed {

fon9_API void RevPrintConfigFieldNames(RevBuffer& rbuf, const Fields& flds, StrView keyFieldName, FieldFlag excludes) {
   size_t fldno = flds.size();
   while (fldno > 0) {
      const Field* fld = flds.Get(--fldno);
      if (excludes == FieldFlag{} || !IsEnumContainsAny(fld->Flags_, excludes))
         RevPrint(rbuf, *fon9_kCSTR_CELLSPL, fld->Name_);
   }
   RevPrint(rbuf, keyFieldName);
}
fon9_API void RevPrintConfigFieldValues(RevBuffer& rbuf, const Fields& flds, const RawRd& rd, FieldFlag excludes) {
   size_t fldno = flds.size();
   while (fldno > 0) {
      const Field* fld = flds.Get(--fldno);
      if (excludes == FieldFlag{} || !IsEnumContainsAny(fld->Flags_, excludes)) {
         fld->CellRevPrint(rd, nullptr, rbuf);
         RevPutChar(rbuf, *fon9_kCSTR_CELLSPL);
      }
   }
}

GridViewToContainer::~GridViewToContainer() {
}
void GridViewToContainer::ParseFieldNames(const Fields& tabflds, StrView& cfgstr) {
   this->Fields_.clear();
   this->Fields_.reserve(tabflds.size());
   StrView ln = StrFetchNoTrim(cfgstr, *fon9_kCSTR_ROWSPL);
   // 為了讓有些使用 editor 工具修改, 造成尾端 "\n" 變成 "\r\n", 所以增加 StrRemoveTailCRLN();
   StrFetchNoTrim(StrRemoveTailCRLF(&ln), *fon9_kCSTR_CELLSPL);// skip key field name.
   while (!ln.empty())
      this->Fields_.push_back(tabflds.Get(StrFetchNoTrim(ln, *fon9_kCSTR_CELLSPL)));
}
void GridViewToContainer::ParseConfigStr(StrView cfgstr) {
   unsigned lineNo = 1;
   while (!cfgstr.empty()) {
      ++lineNo;
      StrView ln = StrFetchNoTrim(cfgstr, *fon9_kCSTR_ROWSPL);
      if (StrRemoveTailCRLF(&ln).empty())
         continue;
      StrView keyText = StrFetchNoTrim(ln, *fon9_kCSTR_CELLSPL);
      this->OnNewRow(keyText, ln);
   }
}
void GridViewToContainer::FillRaw(const RawWr& wr, StrView ln) {
   for (const Field* fld : this->Fields_) {
      StrView fldval = StrFetchNoTrim(ln, *fon9_kCSTR_CELLSPL);
      if (fld)
         fld->StrToCell(wr, fldval);
   }
}

} } // namespaces
