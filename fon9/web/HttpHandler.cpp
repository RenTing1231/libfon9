/// \file fon9/web/HttpHandler.cpp
/// \author fonwinz@gmail.com
#include "fon9/web/HttpHandler.hpp"
#include "fon9/web/UrlCodec.hpp"
#include "fon9/web/HttpDate.hpp"
#include "fon9/web/HtmlEncoder.hpp"
#include "fon9/seed/FieldMaker.hpp"
#include "fon9/FilePath.hpp"

namespace fon9 { namespace web {

void HttpRequest::RemoveFullMessage() {
   this->Message_.RemoveFullMessage();
   this->ClearFields();
}
void HttpRequest::ClearAll() {
   this->Message_.ClearAll();
   this->ClearFields();
}
void HttpRequest::ClearFields() {
   this->MessageSt_ = HttpMessageSt::Incomplete;
   this->HttpHandler_.reset();
   this->Method_.clear();
   this->TargetOrig_.clear();
}
void HttpRequest::ParseStartLine() {
   assert(this->Message_.IsHeaderReady());
   assert(!this->IsHeaderReady());
   StrView  startLine = this->Message_.StartLine();
   this->Method_.assign(StrFetchNoTrim(startLine, ' '));

   assert(this->TargetOrig_.empty());
   UrlDecodeAppend(this->TargetOrig_, StrFetchNoTrim(startLine, ' '));
   this->TargetRemain_ = ToStrView(this->TargetOrig_);

   this->Version_.assign(startLine);
}

//--------------------------------------------------------------------------//

HttpHandler::~HttpHandler() {
}
io::RecvBufferSize HttpHandler::SendErrorPrefix(io::Device& dev, HttpRequest& req, StrView httpStatus, RevBufferList&& currbuf) {
   if (req.IsMethod("HEAD") || currbuf.cfront() == nullptr) {
      currbuf.MoveOut();
      RevPrint(currbuf, fon9_kCSTR_HTTPCRLN);
   }
   else {
      RevPrint(currbuf,
               "<!DOCTYPE html>" "<html>"
               "<head>"
               "<meta charset='utf-8'/>"
               "<title>fon9:error</title>"
               "<style>.error{color:red;background:lightgray}</style>"
               "</head>");
      size_t sz = CalcDataSize(currbuf.cfront());
      RevPrint(currbuf,
               "Content-Type: text/html; charset=utf-8" fon9_kCSTR_HTTPCRLN
               "Content-Length: ", sz,                  fon9_kCSTR_HTTPCRLN
               fon9_kCSTR_HTTPCRLN);
   }
   RevPrint(currbuf, fon9_kCSTR_HTTP11 " ", httpStatus, fon9_kCSTR_HTTPCRLN
            "Date: ", FmtHttpDate{UtcNow()}, fon9_kCSTR_HTTPCRLN);
   dev.Send(currbuf.MoveOut());
   return io::RecvBufferSize::Default;
}

//--------------------------------------------------------------------------//

HttpDispatcher::~HttpDispatcher() {
}

seed::LayoutSP HttpDispatcher::MakeDefaultLayout() {
   seed::Fields fields = NamedSeed::MakeDefaultFields();
   //fields.Add(fon9_MakeField(HttpHandler, Value_, "Config"));
   return new seed::Layout1(fon9_MakeField2(HttpHandler, Name),
                            new seed::Tab{Named{"Handler"}, std::move(fields)});
}
seed::TreeSP HttpDispatcher::GetSapling() {
   return this->Sapling_;
}

io::RecvBufferSize HttpDispatcher::OnHttpRequest(io::Device& dev, HttpRequest& req) {
   assert(req.IsHeaderReady());
   req.TargetRemain_ = FilePath::RemovePathHead(req.TargetRemain_);
   req.TargetCurr_   = StrFetchNoTrim(req.TargetRemain_, '/');
   if (auto handler = this->Get(req.TargetCurr_))
      return handler->OnHttpRequest(dev, req);
   return this->OnHttpHandlerNotFound(dev, req);
}

io::RecvBufferSize HttpDispatcher::OnHttpHandlerNotFound(io::Device& dev, HttpRequest& req) {
   RevBufferList rbuf{128};
   if (!req.IsMethod("HEAD")) {
      if (req.TargetCurr_.empty()) {
         RevPrint(rbuf, "</body></html>");
         RevEncodeHtml(rbuf, ToStrView(req.TargetOrig_));
         RevPrint(rbuf, "<body>Handler not support: ");
      }
      else {
         // 如果 req.TargetOrig_ == "../XXXX"; 例: 首行輸入的是 "GET ../ma/fon9ma.html HTTP/1.1";
         // 則 req.TargetCurr_.begin() == req.TargetOrig_.begin();
         // 雖然透過 browser 來的 target 通常會有 '/' 開頭; 但攻擊 or 弱點掃描工具, 不會如此;
         const char* pcurr = req.TargetCurr_.begin();
         if (pcurr > req.TargetOrig_.begin())
            --pcurr;
         RevPrint(rbuf, "</body></html>");
         RevEncodeHtml(rbuf, StrView{req.TargetCurr_.end(), req.TargetOrig_.end()});
         RevPrint(rbuf, "</b>");
         RevEncodeHtml(rbuf, StrView{pcurr, req.TargetCurr_.end()});
         RevPrint(rbuf, "<b class='error'>");
         RevEncodeHtml(rbuf, StrView{req.TargetOrig_.begin(), pcurr});
         RevPrint(rbuf, "<body>Handler not found: ");
      }
   }
   fon9_LOG_WARN("HttpHandlerNotFound|dev=", ToPtr(&dev), "|method=", req.Method_, "|target=", req.TargetOrig_);
   return this->SendErrorPrefix(dev, req, fon9_kCSTR_HTTP_404_NotFound, std::move(rbuf));
}

} } // namespaces
