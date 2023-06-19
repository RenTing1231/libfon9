// \file fon9/fmkt/FmktTypes.h
// \author fonwinz@gmail.com
#ifndef __fon9_fmkt_FmktTypes_h__
#define __fon9_fmkt_FmktTypes_h__
#include "fon9/sys/Config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// \ingroup fmkt
/// 回報項目種類(下單要求種類).
/// 用來標註 TradingRxItem 的種類.
fon9_ENUM(f9fmkt_RxKind, char) {
   f9fmkt_RxKind_Unknown = 0,
   f9fmkt_RxKind_RequestNew    = 'N',
   f9fmkt_RxKind_RequestDelete = 'D',
   f9fmkt_RxKind_RequestChgQty = 'C',
   f9fmkt_RxKind_RequestChgPri = 'P',
   f9fmkt_RxKind_RequestQuery  = 'Q',
   /// 修改條件單的觸發條件.
   f9fmkt_RxKind_RequestChgCond = 'c',
   /// 重新嘗試某筆下單要求.
   /// 例: 某下單要求被風控阻擋, 之後風控額度開放 or 主管確認可放行, 則透過此類要求重新執行下單步驟.
   f9fmkt_RxKind_RequestRerun = 'R',
   /// 成交回報.
   f9fmkt_RxKind_Filled = 'f',
   /// 委託異動.
   f9fmkt_RxKind_Order = 'o',
   /// 系統事件.
   f9fmkt_RxKind_Event = 'e',
};

/// \ingroup fmkt
/// 商品的交易市場.
/// - 在系統設計時決定市場別, 通常用來區分「可直接連線」的不同交易市場.
/// - 這裡沒有將所有可能的 market 都定義出來, 當有實際需求時可自行依下述方式增加.
/// - 底下以台灣市場為例:
///   - 如果系統有交易外匯, 則可增加:
///     `constexpr fon9::fmkt::TradingMarket  f9fmkt_TradingMarket_Forex = static_cast<fon9::fmkt::TradingMarket>('x');`
///   - 如果系統可直接連上 SGX, 則可增加:
///     `constexpr fon9::fmkt::TradingMarket  f9fmkt_TradingMarket_SGX = static_cast<fon9::fmkt::TradingMarket>('g');`
///   - 台灣大部分券商的對外(國外)期權, 可視為「一個」市場:
///     `constexpr fon9::fmkt::TradingMarket  f9fmkt_TradingMarket_FF = static_cast<fon9::fmkt::TradingMarket>('f');`
fon9_ENUM(f9fmkt_TradingMarket, char) {
   f9fmkt_TradingMarket_Unknown = '\0',
   /// 台灣上市(集中交易市場)
   f9fmkt_TradingMarket_TwSEC = 'T',
   /// 台灣上櫃(櫃檯買賣中心)
   f9fmkt_TradingMarket_TwOTC = 'O',
   /// 台灣興櫃.
   f9fmkt_TradingMarket_TwEMG = 'E',
   /// 台灣期交所-期貨市場.
   f9fmkt_TradingMarket_TwFUT = 'f',
   /// 台灣期交所-選擇權市場.
   f9fmkt_TradingMarket_TwOPT = 'o',

   /// 提供給陣列使用, 例如:
   /// using MarketAry = std::array<MarketRec, f9fmkt_TradingMarket_MaxIndex + 1u>;
   f9fmkt_TradingMarket_MaxIndex = 'z'
};
/// 返回值必定在 [0..f9fmkt_TradingMarket_MaxIndex] 之間.
static inline unsigned char f9fmkt_TradingMarket_ToIndex(f9fmkt_TradingMarket v) {
   return (unsigned char)(v) > (unsigned char)f9fmkt_TradingMarket_MaxIndex
      ? (unsigned char)(0) : (unsigned char)(v);
}
static inline f9fmkt_TradingMarket IndexTo_f9fmkt_TradingMarket(unsigned idx) {
   return idx > (unsigned char)f9fmkt_TradingMarket_MaxIndex
      ? f9fmkt_TradingMarket_Unknown : (f9fmkt_TradingMarket)(idx);
}

/// \ingroup fmkt
/// 交易時段代碼.
/// - FIX 4.x TradingSessionID(#336): 並沒有明確的定義, 由雙邊自訂.
/// - FIX 5.0 TradingSessionID(#336): http://fixwiki.org/fixwiki/TradingSessionID
///   '1'=Day; '2'=HalfDay; '3'=Morning; '4'=Afternoon; '5'=Evening; '6'=AfterHours; '7'=Holiday;
/// - 台灣期交所 FIX 的 Trading Session Status Message 定義 TradingSessionID=流程群組代碼.
/// - 台灣證交所 FIX 沒有 Trading Session Status Message,
///   但要求提供 TargetSubID(#57)=[TMP協定的 AP-CODE]:
///   Trading session: Required for New / Replace / Cancel / Query / Execution
///   '0'=Regular Trading; '7'=FixedPrice; '2'=OddLot.
fon9_ENUM(f9fmkt_TradingSessionId, char) {
   f9fmkt_TradingSessionId_Unknown = 0,
   /// 一般(日盤).
   f9fmkt_TradingSessionId_Normal = 'N',
   /// 零股.
   f9fmkt_TradingSessionId_OddLot = 'O',
   /// 盤後定價.
   f9fmkt_TradingSessionId_FixedPrice = 'F',
   /// 盤後交易(夜盤).
   f9fmkt_TradingSessionId_AfterHour = 'A',

   /// 提供給陣列使用, 例如:
   /// using SessionAry = std::array<SessionRec, f9fmkt_TradingSessionId_MaxIndex + 1u>;
   f9fmkt_TradingSessionId_MaxIndex = 'z',
   /// 回報時, 若回報格式沒有提供 SessionId, 則可先填入此值,
   /// 若 [回報處理程序] 有支援, 則: 根據現有委託書填入 SessionId.
   f9fmkt_TradingSessionId_RptAutoSessionId = f9fmkt_TradingSessionId_MaxIndex
};
/// 返回值必定在 [0..f9fmkt_TradingSessionId_MaxIndex] 之間.
static inline unsigned char f9fmkt_TradingSessionId_ToIndex(f9fmkt_TradingSessionId v) {
   return (unsigned char)(v) > (unsigned char)f9fmkt_TradingSessionId_MaxIndex
      ? (unsigned char)(0) : (unsigned char)(v);
}
static inline f9fmkt_TradingSessionId IndexTo_f9fmkt_TradingSessionId(unsigned idx) {
   return idx > (unsigned char)f9fmkt_TradingSessionId_MaxIndex
      ? f9fmkt_TradingSessionId_Unknown : (f9fmkt_TradingSessionId)(idx);
}

/// 交易時段狀態.
/// (value & 0x0f) = FIX Tag#340(TradSesStatus:State of the trading session).
/// 這裡列出可能的狀態, 但實務上不一定會依序, 或確實更新.
/// - 例: PreClose 狀態, 雖然「台灣證交所」有此狀態, 但系統不一定會提供.
/// - 必定會異動的情況是:
///   - 系統換日清盤(Clear).
///   - 收到交易所送出的異動訊息.
fon9_ENUM(f9fmkt_TradingSessionSt, uint8_t) {
   f9fmkt_TradingSessionSt_Unknown         = 0x00,
   f9fmkt_TradingSessionSt_RequestRejected = 0x06,

   /// 清盤(FIX 無此狀態).
   f9fmkt_TradingSessionSt_Clear           = 0x10,

   /// 開盤前可收單.
   f9fmkt_TradingSessionSt_PreOpen         = 0x34,
   /// 不可刪單階段, 通常在開盤前 n 分鐘, 例: 台灣期交所開盤前2分鐘不可刪單.
   f9fmkt_TradingSessionSt_NonCancelPeriod = 0x44,

   /// 延後開盤: 例如台灣證交所有提供「試算後延後開盤」.
   f9fmkt_TradingSessionSt_DelayOpen       = 0x72,
   /// 一般盤中.
   f9fmkt_TradingSessionSt_Open            = 0x82,
   /// 異常重新開盤, 例如: 期交所發生異常, 期交所會用 SeqReset 訊息通知, 此時 fon9 會用此狀態通知.
   f9fmkt_TradingSessionSt_OpenSeqReset    = 0x92,

   /// 暫停交易, 可能會恢復, 恢復後的狀態為 f9fmkt_TradingSessionSt_Open.
   f9fmkt_TradingSessionSt_Halted          = 0xb1,
   /// 收盤前, 仍可收單, 但交易方式可能與盤中不同.
   f9fmkt_TradingSessionSt_PreClose        = 0xe5,

   /// 延後收盤: 例如台灣證交所有提供「試算後延後收盤」.
   f9fmkt_TradingSessionSt_DelayClose      = 0xe3,
   /// 該時段收盤, 或該時段不再恢復交易.
   f9fmkt_TradingSessionSt_Closed          = 0xf3,
};
static inline char f9fmkt_TradingSessionSt_ToFixTradSesStatus(f9fmkt_TradingSessionSt st) {
   return (char)((st & 0x0f) + '0');
}

fon9_ENUM(f9fmkt_PriType, char) {
   f9fmkt_PriType_Unknown = 0,
   /// 限價.
   f9fmkt_PriType_Limit = 'L',
   /// 市價.
   f9fmkt_PriType_Market = 'M',
   /// 一定範圍內市價.
   f9fmkt_PriType_Mwp = 'm',
};

/// \ingroup fmkt
/// 時間限制.
fon9_ENUM(f9fmkt_TimeInForce, char) {
   /// 當日有效. ROD, GFD.
   f9fmkt_TimeInForce_ROD = 0,
   /// 立即成交否則取消.
   f9fmkt_TimeInForce_IOC = 'I',
   /// 立即全部成交否則取消.
   f9fmkt_TimeInForce_FOK = 'F',
   /// 報價單,一定期間後交易所自動刪除.
   f9fmkt_TimeInForce_QuoteAutoCancel = '8',
};

/// \ingroup fmkt
/// 買賣別.
fon9_ENUM(f9fmkt_Side, char) {
   f9fmkt_Side_Unknown = 0,
   f9fmkt_Side_Buy = 'B',
   f9fmkt_Side_Sell = 'S',
};

/// \ingroup fmkt
/// 下單要求狀態.
/// 這裡的值有順序性, Request 的狀態值只會往後增加.
fon9_ENUM(f9fmkt_TradingRequestSt, uint8_t) {
   /// 建構時的初始值.
   f9fmkt_TradingRequestSt_Initialing = 0,

   /// 條件單初步檢查成功, 正在等候條件成立.
   /// 條件成立後, 會繼續走 [風控、送單] 流程, 狀態依序變化.
   f9fmkt_TradingRequestSt_WaitingCond = 0x0a,
   f9fmkt_TradingRequestSt_WaitingCondAtOther = 0x0b,

   /// - 當檢查方式需要他方協助, 無法立即得到結果, 則在檢查前設定此狀態.
   /// - 如果檢查方式可立即完成:「失敗拒絕下單 or 檢查通過繼續下一步驟」, 則檢查前可以不用設定此狀態.
   f9fmkt_TradingRequestSt_Checking = 0x1c,
   /// 下單要求排隊中.
   f9fmkt_TradingRequestSt_Queuing = 0x20,
   /// 下單要求在另一台主機排隊.
   f9fmkt_TradingRequestSt_QueuingAtOther = 0x21,

   /// 請求遠端支援送出.
   /// 若遭遠端退回, 則有可能會回到 f9fmkt_TradingRequestSt_Queuing;
   f9fmkt_TradingRequestSt_AskToRemote = 0x2a,
   /// 遠端支援退回, 遠端可能在收到下單要求時, 無可用線路, 或流量管制.
   /// 退回原因需參考 ErrCode;
   f9fmkt_TradingRequestSt_BackFromRemote = 0x2b,

   /// 下單要求送出給 [券商系統], 由券商系統處理下單要求.
   f9fmkt_TradingRequestSt_SendingToBroker = 0x2d,
   /// 下單要求送出給 [券商系統] 後, 券商系統告知在其內部排隊中, 尚未送出給交易所.
   /// 券商系統送給交易所, 會變成 f9fmkt_TradingRequestSt_Sent 狀態.
   f9fmkt_TradingRequestSt_BrokerQueuing = 0x2e,

   /// 在呼叫 io.Send() 之前設定的狀態.
   /// 您可以自行決定要在 io.Send() 之前 or 之後 or both, 設定送出狀態.
   f9fmkt_TradingRequestSt_Sending = 0x30,
   /// 在呼叫 io.Send() 之後設定的狀態.
   /// 如果要在送出後設定狀態: 則要小心考慮在 io.Send() 之後, 設定 Sent 之前, 就已收到送出的結果.
   f9fmkt_TradingRequestSt_Sent = 0x38,

   /// 小於等於 f9fmkt_TradingRequestSt_LastRunStep 表示:
   /// [下單要求流程]處理過程的狀態變化, [下單要求流程]仍在處理中.
   /// 與母單無關, 母單使用 f9fmkt_TradingRequestSt_ParentDone 來判斷.
   f9fmkt_TradingRequestSt_LastRunStep = 0x9f,

   /// 交易所部分回報: 有些下單要求交易所會有多次回報.
   /// 針對「某筆下單要求」交易所「最後一次回報」前的狀態.
   /// 此部分回報為成功.
   f9fmkt_TradingRequestSt_PartExchangeAccepted = 0xa2,

   // 保留:
   // f9fmkt_OrderSt_ParentPartFilled = 0xa3,

   /// 母單已完畢(全部子單已送出,尚未有失敗), 部分尚未收到回報.
   /// 若子單全部成功, 則會變成 f9fmkt_TradingRequestSt_ExchangeAccepted;
   f9fmkt_TradingRequestSt_ParentDone = 0xa4,

   /// 如果確定交易所刪單會分成多次傳送,
   /// 則首次收到的交易所刪單回報, 使用此狀態.
   /// - 例: 報價單 Bid/Offer 分2筆回報, 首次的 Bid 回報使用此狀態;
   ///       Offer 使用 f9fmkt_TradingRequestSt_ExchangeCanceled.
   f9fmkt_TradingRequestSt_PartExchangeCanceled = 0xa7,
   /// 交易所部分回報: 有些下單要求交易所會有多次回報.
   /// 針對「某筆下單要求」交易所「最後一次回報」前的狀態.
   /// 此部分回報為失敗.
   f9fmkt_TradingRequestSt_PartExchangeRejected = 0xae,

   /// >=f9fmkt_TradingRequestSt_Done 表示: 下單要求流程已結束, 下單要求失敗, 或已收到回報結果.
   f9fmkt_TradingRequestSt_Done = 0xda,

   // 不明原因結束, 無法確定此要求是否成功.
   // 例如: 要求送出後斷線.
   // ** 移除此狀態 **
   // ** 若送出後沒收到回報, 不論重啟或斷線, 都不改變最後狀態.
   // ** 保留最後狀態, 維運人員較容易研判問題.
   //f9fmkt_TradingRequestSt_Broken = 0xdb,

   /// 排隊中的要求被取消, 此筆要求沒送出.
   /// 可能因程式結束後重啟, 或新單排隊中但收到刪單要求.
   f9fmkt_TradingRequestSt_QueuingCanceled = 0xe2,
   /// 下單要求因「線路狀況」拒絕. 尚未送給交易所.
   /// 例如: 無可用線路.
   f9fmkt_TradingRequestSt_LineRejected = 0xe3,
   /// 下單要求因「委託書號」拒絕. 尚未送給交易所.
   /// 例如: 已無可用櫃號; 自編委託書號重複.
   f9fmkt_TradingRequestSt_OrdNoRejected = 0xe4,
   /// 下單要求因「內部其他原因」拒絕. 尚未送給交易所.
   f9fmkt_TradingRequestSt_InternalRejected = 0xe9,
   /// 下單到券商系統, 被券商系統拒絕.
   f9fmkt_TradingRequestSt_BrokerRejected = 0xeb,
   /// 下單要求因「風控檢查」拒絕. 尚未送給交易所.
   f9fmkt_TradingRequestSt_CheckingRejected = 0xec,
   /// 下單要求被「交易所」拒絕.
   f9fmkt_TradingRequestSt_ExchangeRejected = 0xee,
   /// 刪改要求因「刪改時已無剩餘量,但曾經有此委託」而被「交易所」拒絕刪改.
   /// 請檢查: 是否已全部成交? 或是否已經被刪(交易所刪單、其他系統刪減...)?
   f9fmkt_TradingRequestSt_ExchangeNoLeavesQty  = 0xef,

   // 新單要求: 尚未收到新單回報, 但先收到成交回報.
   // 如果有「新單未成功」時「暫時保留成交」則無此狀態.
   // ** 移除此狀態 **
   // ** 如果可以用「收到的回報(成交、刪、改、查)」確認新單成功數量,
   // ** 則直接在該回報更新前, 更新「新單要求」的最後狀態=f9fmkt_TradingRequestSt_ExchangeAccepted
   // ** 然後再更新「收到的回報(成交、刪、改、查)」.
   //f9fmkt_TradingRequestSt_FilledBeforeNew = 0xf1,

   /// 已收到交易所的確認回報: 包含新單回報、刪改回報、查詢回報...
   f9fmkt_TradingRequestSt_ExchangeAccepted = 0xf2,
   /// 成交回報要求, 直接使用此狀態.
   f9fmkt_TradingRequestSt_Filled = 0xf3,

   // 保留:
   // f9fmkt_OrderSt_FullFilled = 0xf4,
   // f9fmkt_OrderSt_AsCanceled = 0xf5,
   // = f6,

   /// 如果確定「交易所主動」刪單會分成多次傳送,
   /// 則首次收到的交易所刪單回報, 使用此狀態.
   /// - 例: 期交所狀態 47 => 48;
   ///       47(因價格穩定措施刪單)「交易所主動刪單」回報使用 f9fmkt_TradingRequestSt_ExchangeCanceling;
   ///       48(退單時之上限或下限價格) 使用 f9fmkt_TradingRequestSt_ExchangeCanceled.
   /// - 證交所: f9fmkt_TradingRequestSt_ExchangeCanceling 表示:
   ///   證券進入價格穩定措施或尾盤集合競價時段，交易所主動取消留存委託簿之「市價」委託單資料並回報.
   ///   但因證券沒有分次回報, 所以 OrderSt 仍使用 f9fmkt_OrderSt_ExchangeCanceled;
   f9fmkt_TradingRequestSt_ExchangeCanceling = 0xf7,
   /// 交易所刪單 IOC, FOK... 若有多次回覆, 則最後一次刪單回覆, 使用此狀態.
   f9fmkt_TradingRequestSt_ExchangeCanceled  = 0xfa,
   /// f9fmkt_TradingRequestSt_ExchangeCanceled 之後, 又收到必須處理的交易所刪單.
   f9fmkt_TradingRequestSt_ExchangeCanceled2 = 0xfb,
   f9fmkt_TradingRequestSt_ExchangeCanceled3 = 0xfc,

   // 保留: f9fmkt_OrderSt_UserCanceled = 0xfd,

   /// 下單要求流程已結束, 但後續又有變動.
   /// 類似 FIX 的 ExecType=D(Restated).
   f9fmkt_TradingRequestSt_Restated = 0xfe,

   // 保留: f9fmkt_OrderSt_DoneForDay = 0xff,
   // 這裡的狀態碼, 必須與 f9fmkt_OrderSt 一起考慮.
   // 因為新單的 RequestSt 改變時, 通常會拿新單的 RequestSt 直接更新 OrderSt.
};
static inline int f9fmkt_TradingRequestSt_IsFinishedRejected(f9fmkt_TradingRequestSt st) {
   return (unsigned char)(st & 0xf0) == (unsigned char)(0xe0);
}
static inline int f9fmkt_TradingRequestSt_IsAnyRejected(f9fmkt_TradingRequestSt st) {
   return f9fmkt_TradingRequestSt_IsFinishedRejected(st)
      || st == f9fmkt_TradingRequestSt_PartExchangeRejected;
}
static inline int f9fmkt_TradingRequestSt_IsPart(f9fmkt_TradingRequestSt st) {
   return (unsigned char)(st & 0xf0) == (unsigned char)(0xa0);
}

/// \ingroup fmkt
/// OrderSt 分成 4 階段:
/// - 新單還在系統裡未送出.
///   - Checking:   風控檢查中.
///   - Queuing:    排隊中.
/// - 新單已送出但尚未收到回報.
///   - Sending:    傳送中.
///   - Sent:       已送出, 但尚未收到交易所回報.
///   - Broken:     狀態不明: 送出後,未收到回報,但是斷線了.
/// - 委託已成立:
///   - Accepted:   交易所已收單.
///   - PartFilled: 部分成交.
/// - 委託已結束:
///   - 進入此階段後, 狀態就不會再變動.
///   - 當本機發現交易所的 WorkQty 變成 0 時.
///     - 刪減後的 AfterQty=0: 使用者刪減、交易所刪減(IOC、FOK、超過限額...)
///     - 收到成交回報後的 LeavesQty=0
///     - 交易所收盤.
///     - 要注意的是: 「成交回報、刪減回報」可能會亂序!
///       - 假設某筆委託交易所異動順序如下:   
///         新10, 減1(Bf=10,Af=9), 成2, 減2(Bf=7,Af=5), 成2, 刪(Bf=3,Af=0)
///       - 若分別在不同的主機上操作, 最多可能有 6*5*4*3*2*1=720 種順序組合!!
///   - Rejected:   委託被拒絕.
///     - BrokerRejected:   券商拒絕.
///     - ExchangeRejected: 交易所拒絕.
///   - Canceled:   刪單成功(已刪單狀態), 刪減後 AfterQty=0
///     - 透過刪減回報的「BeforeQty - AfterQty」來調整 LeavesQty.
///     - 但此時本機的 LeavesQty 不一定為 0, 因為可能有「在途成交」或「其他未收到的減量」.
///     - 若有在途成交, 則收完成交後, **不會變成全部成交**
///     - UserCanceled:     使用者刪單, 或減量成功後的 AfterQty=0;
///     - ExchangeCanceled: 交易所刪單(IOC、FOK...).
///   - FullFilled: 全部成交.
///   - DoneForDay: 已收盤.
fon9_ENUM(f9fmkt_OrderSt, uint8_t) {
   f9fmkt_OrderSt_Initialing = 0,

   /// 此次委託異動的原因是為了記錄: 被保留的亂序回報.
   /// - 委託異動 = 被保留的回報, 例: BeforeQty, AfterQty, ExgTime, ErrCode... 都會寫到「委託異動」.
   /// - 等適當時機再繼續處理, 例:
   ///   - 收到新單回報.
   ///   - Order.LeavesQty==保留的.BeforeQty.
   f9fmkt_OrderSt_ReportPending = 1,
   /// 收到交易所的回報, 但此回報已經臭酸了.
   /// - 可能的情況, 例:
   ///   - 查單 => 刪單 => 刪單回報成功 => 查單回報剩餘量3(Stale)
   ///   - 改價A => 改價B => 改價B成功 => 改價A成功(Stale);
   /// - 若無法用數量來判斷, 則使用交易所時間來判斷.
   /// - 若連交易所時間都相同, 則使用收到的先後順序來處理, 所以仍有誤判的可能.
   /// - 此時「委託異動」內容只有底下欄位會變動, 只是為了記錄該筆要求已處理完畢:
   ///   - OrderSt   = f9fmkt_OrderSt_ReportStale
   ///   - RequestSt = report st.
   ///   - ErrCode   = report err code.
   ///   - 其他委託內容(例: ExgTime, Qty, Pri...)都不會變動.
   f9fmkt_OrderSt_ReportStale = 2,

   f9fmkt_OrderSt_NewWaitingCond          = f9fmkt_TradingRequestSt_WaitingCond,
   f9fmkt_OrderSt_NewWaitingCondAtOther   = f9fmkt_TradingRequestSt_WaitingCondAtOther,
      
   /// f9fmkt_OrderSt_IniAccepted 用於 OmsOrder::Initiator() 及 OmsOrder::EndUpdate();
   /// >= f9fmkt_OrderSt_IniAccepted 則表示此單已被 f9oms 接受.
   /// 後續異動必須更新 LastOrderSt_;
   f9fmkt_OrderSt_IniAccepted             = f9fmkt_OrderSt_NewWaitingCond,

   f9fmkt_OrderSt_NewStarting             = 0x10,
   f9fmkt_OrderSt_NewChecking             = f9fmkt_TradingRequestSt_Checking,
   f9fmkt_OrderSt_NewQueuing              = f9fmkt_TradingRequestSt_Queuing,
   f9fmkt_OrderSt_NewQueuingAtOther       = f9fmkt_TradingRequestSt_QueuingAtOther,

   f9fmkt_OrderSt_NewAskToRemote          = f9fmkt_TradingRequestSt_AskToRemote,

   f9fmkt_OrderSt_NewSendingToBroker      = f9fmkt_TradingRequestSt_SendingToBroker,
   f9fmkt_OrderSt_NewBrokerQueuing        = f9fmkt_TradingRequestSt_BrokerQueuing,

   f9fmkt_OrderSt_NewSending              = f9fmkt_TradingRequestSt_Sending,
   f9fmkt_OrderSt_NewSent                 = f9fmkt_TradingRequestSt_Sent,

   f9fmkt_OrderSt_NewPartExchangeAccepted = f9fmkt_TradingRequestSt_PartExchangeAccepted,
   /// 母單尚未完畢, 已有成交.
   /// 若母單已完畢, 則會用 f9fmkt_OrderSt_PartFilled or f9fmkt_OrderSt_FullFilled;
   f9fmkt_OrderSt_ParentPartFilled        = 0xa3,
   /// 母單已完畢(全部子單已送出,尚未有失敗), 部分尚未收到回報.
   /// 若子單全部成功, 則會變成 f9fmkt_OrderSt_ExchangeAccepted;
   /// 之後除了刪減, 不再接受其他修改。
   f9fmkt_OrderSt_ParentDone              = f9fmkt_TradingRequestSt_ParentDone,
   // = f9fmkt_TradingRequestSt_PartExchangeCanceled = 0xa7,
   f9fmkt_OrderSt_NewPartExchangeRejected = f9fmkt_TradingRequestSt_PartExchangeRejected,

   f9fmkt_OrderSt_NewDone                 = f9fmkt_TradingRequestSt_Done,
 //f9fmkt_OrderSt_NewBroken               = f9fmkt_TradingRequestSt_Broken,

   f9fmkt_OrderSt_NewQueuingCanceled      = f9fmkt_TradingRequestSt_QueuingCanceled,
   f9fmkt_OrderSt_NewLineRejected         = f9fmkt_TradingRequestSt_LineRejected,
   f9fmkt_OrderSt_NewOrdNoRejected        = f9fmkt_TradingRequestSt_OrdNoRejected,
   f9fmkt_OrderSt_NewInternalRejected     = f9fmkt_TradingRequestSt_InternalRejected,
   f9fmkt_OrderSt_NewBrokerRejected       = f9fmkt_TradingRequestSt_BrokerRejected,
   f9fmkt_OrderSt_NewCheckingRejected     = f9fmkt_TradingRequestSt_CheckingRejected,
   f9fmkt_OrderSt_NewExchangeRejected     = f9fmkt_TradingRequestSt_ExchangeRejected,

   f9fmkt_OrderSt_ExchangeAccepted = f9fmkt_TradingRequestSt_ExchangeAccepted,
   f9fmkt_OrderSt_PartFilled       = f9fmkt_TradingRequestSt_Filled,
   f9fmkt_OrderSt_FullFilled       = 0xf4,
   /// 部分成交之後(其實是全部成交), 才收到成交前的減量回報.
   /// - 例: 新單10:Accepted, 成交3:PartFilled, 減量7(Bf=10,Af=3):AsCanceled
   f9fmkt_OrderSt_AsCanceled = 0xf5,
   // 保留: = 0xf6,

   /// 若在 OmsErrCodeAct 有設定 St=ExchangeCanceled; 或 ExchangeCanceling;
   /// 則在 OmsRequestRunnerInCore::Update(); 會設定 UpdateOrderSt_ = St;
   /// 此時就會使用到這些狀態.
   f9fmkt_OrderSt_Canceling         = f9fmkt_TradingRequestSt_ExchangeCanceling,
   f9fmkt_OrderSt_ExchangeCanceled  = f9fmkt_TradingRequestSt_ExchangeCanceled,
   f9fmkt_OrderSt_ExchangeCanceled2 = f9fmkt_TradingRequestSt_ExchangeCanceled2,
   f9fmkt_OrderSt_ExchangeCanceled3 = f9fmkt_TradingRequestSt_ExchangeCanceled3,
   /// 一般用於使用者的刪單要求成功刪單完成.
   f9fmkt_OrderSt_UserCanceled      = 0xfd,

   // 保留: = f9fmkt_TradingRequestSt_Restated = 0xfe,

   f9fmkt_OrderSt_DoneForDay = 0xff,
   // 這裡的狀態碼, 必須與 f9fmkt_TradingRequestSt 一起考慮.
   // 因為新單的 RequestSt 改變時, 通常會拿新單的 RequestSt 直接更新 OrderSt.
};
static inline int f9fmkt_OrderSt_IsFinishedRejected(f9fmkt_OrderSt st) {
   return f9fmkt_TradingRequestSt_IsFinishedRejected((f9fmkt_TradingRequestSt)st);
}
static inline int f9fmkt_OrderSt_IsAnyRejected(f9fmkt_OrderSt st) {
   return f9fmkt_TradingRequestSt_IsAnyRejected((f9fmkt_TradingRequestSt)st);
}
static inline int f9fmkt_OrderSt_IsCanceled(f9fmkt_OrderSt st) {
   return f9fmkt_OrderSt_AsCanceled <= st && st <= f9fmkt_OrderSt_UserCanceled;
}

/// \ingroup fmkt
/// 複式單組合方式.
fon9_ENUM(f9fmkt_SymbCombSide, uint8_t) {
   /// 不是複式單, 沒有第2隻腳.
   f9fmkt_SymbCombSide_None = 0,
   /// Leg1.Side = Leg2.Side = 下單要求的買賣別.
   f9fmkt_SymbCombSide_SameSide = 1,
   /// Leg1.Side = 下單要求的買賣別;
   /// Leg2.Side = (Leg1.Side==Buy ? Sell : Buy);
   f9fmkt_SymbCombSide_SideIsLeg1 = 2,
   /// Leg2.Side = 下單要求的買賣別;
   /// Leg1.Side = (Leg2.Side==Buy ? Sell : Buy);
   f9fmkt_SymbCombSide_SideIsLeg2 = 3,
};

/// \ingroup fmkt
/// 條件單觸發後的執行價.
fon9_ENUM(f9fmkt_ExecPriSel, char) {
   f9fmkt_ExecPriSel_Unknown = 0,
   /// 觸發下單當時的 買進價.
   f9fmkt_ExecPriSel_Bid = 'B',
   /// 觸發下單當時的 賣出價.
   f9fmkt_ExecPriSel_Ask = 'A',
   /// 觸發下單當時的 成交價.
   f9fmkt_ExecPriSel_LastPrice = 'P',
};

#ifdef __cplusplus
}
#endif
#endif//__fon9_fmkt_FmktTypes_h__
