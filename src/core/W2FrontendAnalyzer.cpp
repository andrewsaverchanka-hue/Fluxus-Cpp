#include "W2FrontendAnalyzer.h"
#include "LogUtils.h"

#include <algorithm>
#include <cstdlib>
#include <regex>

namespace fluxus {
namespace {
const std::regex UuidJson(R"rx("trx"\s*:\s*"([0-9a-f\-]{36})")rx");
const std::regex Coupon(R"rx("coupon"\s*:\s*\{\s*"low"\s*:\s*(-?\d+))rx");
const std::regex GroupUuid(R"rx("groupUuid"\s*:\s*"(\w+)")rx");
const std::regex PayoutStatus(R"rx("payoutCoupon"\s*:\s*\{\s*"status"\s*:\s*"(\w+)")rx");
const std::regex Amount(R"rx("amount"\s*:\s*\{\s*"low"\s*:\s*(-?\d+))rx");
const std::regex TrxApi(R"rx(trx\s+([0-9a-f\-]{36}))rx");
const std::regex Login(R"rx(login:\s*'(\d+)')rx");
const std::regex LoginPlain(R"rx(login:\s*(\d+))rx");
const std::regex DbTrx(R"rx(`trx`\s*=\s*'([0-9a-f\-]{36})')rx");
const std::regex DbSum(R"rx(`sum`\s*=\s*(\d+))rx");
const std::regex DbUser(R"rx(`user_login`\s*=\s*'(\d+)')rx");
const std::regex DbId(R"rx(where id\s*=\s*(\d+))rx");
const std::regex Commands(R"rx(commands\s+(\[.*?\]))rx");
const std::regex Snapshot(R"rx(nominal and current count\s+(\[\[.*?\]\]))rx");
const std::regex Dispensed(R"rx("dispensed"\s*:\s*(\{[^{}]*\}))rx");
const std::regex Model(R"rx("model"\s*:\s*"([^"]+)")rx");
const std::regex Device(R"rx("device"\s*:\s*\{\s*"id"\s*:\s*"([^"]+)")rx");
const std::regex Vendor(R"rx("vendor"\s*:\s*"([^"]+)")rx");
const std::regex BoxValues(R"rx(\((\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\))rx");
const std::regex DbStatus(R"rx(`status`\s*=\s*'(\w+)')rx");
const std::regex ErrorTrx(R"rx(trx[:\s]+([0-9a-f\-]{36}))rx");
const std::regex CouponValue(R"rx("value"\s*:\s*\{\s*"low"\s*:\s*(-?\d+))rx");
const std::regex CompleteTrx(R"rx(transactionId:\s*([0-9a-f\-]{36}))rx");
const std::regex TotalSum(R"rx(totalSum["\s]*(?:,\s*"value"\s*)?["":=\s]+"?\$?(\d+))rx",std::regex::icase);
const std::regex TotalOut(R"rx(totalOut["\s]*(?:,\s*"value"\s*)?["":=\s]+"?\$?(\d+))rx",std::regex::icase);
const std::regex NoteEntry(R"rx(\$(\d+)\s*\*\s*(\d+)\s*=\s*\$?(\d+))rx");
const std::regex BoxEntry(R"rx(Box\s+(\d+)[^\n]*\n+(?:Inserted:\s*\n+)?\$(\d+)\s*\*\s*(\d+)\s*=\s*\$?(\d+))rx",std::regex::icase);

std::optional<std::string> group(const std::regex& pattern,const std::string& line) {
    std::smatch match; if (std::regex_search(line,match,pattern)) return match[1].str(); return std::nullopt;
}
std::int64_t absolute(const std::string& value) { return std::llabs(std::stoll(value)); }
std::string unescape(std::string value) {
    std::string result; result.reserve(value.size());
    for (std::size_t i=0;i<value.size();++i) {
        if (value[i]=='\\' && i+1<value.size() && (value[i+1]=='n' || value[i+1]=='"')) {
            if (value[++i]=='n') result.push_back('\n'); else result.push_back('"');
        } else result.push_back(value[i]);
    }
    return result;
}
std::string trim(const std::string& value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return "";
    const auto last=value.find_last_not_of(" \t\r\n");
    return value.substr(first,last-first+1);
}
Event logEvent(const DateTime& dt,const std::string& kind,const std::string& raw,std::size_t length,const Event& extra=Event::object()) {
    Event result={{"dt",dt.iso()},{"date",dt.date()},{"time",dt.time()},{"kind",kind},{"raw",truncate(trim(raw),length)}};
    if (extra.is_object()) result.update(extra); return result;
}
std::map<int,int> parseNotes(const std::string& text) {
    std::map<int,int> result;
    for (std::sregex_iterator it(text.begin(),text.end(),NoteEntry),end;it!=end;++it)
        result[std::stoi((*it)[1])]+=std::stoi((*it)[2]);
    return result;
}
std::map<int,int> parseBoxes(const std::string& text) {
    std::map<int,int> result;
    for (std::sregex_iterator it(text.begin(),text.end(),BoxEntry),end;it!=end;++it)
        result[std::stoi((*it)[1])]+=std::stoi((*it)[3]);
    return result;
}
std::optional<std::string> payloadAfter(const std::string& line,const std::string& marker) {
    const auto found=line.find(marker); if (found==std::string::npos) return std::nullopt;
    auto pos=line.find(':',found+marker.size()); if (pos==std::string::npos) pos=found+marker.size()-1;
    ++pos; while (pos<line.size() && (std::isspace(static_cast<unsigned char>(line[pos])) || line[pos]=='=')) ++pos;
    bool quoted=false; if (pos<line.size() && line[pos]=='"') {quoted=true;++pos;}
    std::string result;
    for (;pos<line.size();++pos) {
        if (quoted && line[pos]=='"' && (pos==0 || line[pos-1]!='\\')) break;
        result.push_back(line[pos]);
    }
    return unescape(result);
}
}

std::shared_ptr<W2WithdrawSession> W2FrontendAnalyzer::ensureSession(const std::string& trx) {
    auto& session=sessionsByTrx_[trx]; if (!session) session=std::make_shared<W2WithdrawSession>(trx); return session;
}
void W2FrontendAnalyzer::restoreSessions(const std::vector<std::shared_ptr<W2WithdrawSession>>& value) {
    sessions=value; sessionsByTrx_.clear(); for (const auto& session:sessions) sessionsByTrx_[session->trx]=session;
}
std::string W2FrontendAnalyzer::resolveTrx(const std::string& line) {
    if (const auto login=group(Login,line)) { const auto it=activeTrxByUser_.find(*login); if (it!=activeTrxByUser_.end()) return it->second; }
    return activeTrx_;
}

void W2FrontendAnalyzer::processLine(const std::string& line) {
    ++linesProcessed; const auto dt=extractFrontendDateTime(line); if (!dt) return;
    if (line.find("payoutCoupon")!=std::string::npos && (line.find("status")!=std::string::npos || line.find("coupon")!=std::string::npos)) {
        if (const auto trx=group(UuidJson,line)) {
            const auto session=ensureSession(*trx);
            if (const auto value=group(Coupon,line)) session->coupon=std::to_string(absolute(*value));
            if (const auto value=group(GroupUuid,line)) session->groupUuid=*value;
            if (const auto value=group(PayoutStatus,line)) session->payoutCouponStatus=*value;
            if (const auto value=group(Amount,line)) session->requestedAmount=absolute(*value);
            session->addEvent(logEvent(*dt,"payoutCoupon",line,200,{{"status",session->payoutCouponStatus},{"amount",session->requestedAmount}}));
            activeTrx_=*trx; ++linesMatched;
        } return;
    }
    if (line.find("payoutCouponAPI")!=std::string::npos) {
        const auto trx=group(TrxApi,line); const auto login=group(LoginPlain,line);
        if (trx) { const auto session=ensureSession(*trx); if (login) {session->userLogin=*login;activeTrxByUser_[*login]=*trx;}
            session->addEvent(logEvent(*dt,"payoutCouponAPI",line,200));++linesMatched; } return;
    }
    if (line.find("INSERT INTO terminal_balances")!=std::string::npos) {
        if (const auto trx=group(DbTrx,line)) { const auto session=ensureSession(*trx);
            if (const auto value=group(DbSum,line)) session->requestedAmount=std::stoll(*value);
            if (const auto login=group(DbUser,line)) {session->userLogin=*login;activeTrxByUser_[*login]=*trx;}
            session->addEvent(logEvent(*dt,"insertBalance",line,150));++linesMatched; } return;
    }
    if (line.find("UPDATE terminal_balances")!=std::string::npos && line.find("in_progress")!=std::string::npos) {
        const auto id=group(DbId,line);const auto trx=resolveTrx(line);
        if (id && !trx.empty()) {const auto session=ensureSession(trx);session->balanceId=std::stoll(*id);
            session->addEvent(logEvent(*dt,"balanceInProgress",line,150));++linesMatched;} return;
    }
    if (line.find("INSERT INTO terminal_transactions ")!=std::string::npos || line.find("INSERT INTO terminal_transactions SET")!=std::string::npos) {
        if (const auto trx=group(DbTrx,line)) {ensureSession(*trx)->addEvent(logEvent(*dt,"insertTransaction",line,150));++linesMatched;} return;
    }
    if (line.find("DispenserService.dispense")!=std::string::npos) {
        const auto trx=resolveTrx(line);if (trx.empty()) return;const auto session=ensureSession(trx);
        if (const auto value=group(Commands,line)) {
            try {session->commands=Json::parse(*value);if (!session->commands.empty() && session->dispenserDeviceId.empty()) session->dispenserDeviceId=eventString(session->commands[0],"deviceId");}catch(...){}
        }
        if (const auto value=group(Snapshot,line)) {try {session->cassetteSnapshot=Json::parse(*value).get<std::vector<std::vector<int>>>();}catch(...){}}
        session->addEvent(logEvent(*dt,"dispenseCommand",line,300));++linesMatched;return;
    }
    if (line.find("processNewEvent")!=std::string::npos && line.find("\"type\":16")!=std::string::npos) {
        const auto trx=resolveTrx(line);if (trx.empty()) return;const auto session=ensureSession(trx);Event dispensed=Event::object();
        if (const auto value=group(Dispensed,line)) {try{dispensed=Json::parse(*value);}catch(...){}}
        const auto model=group(Model,line),device=group(Device,line),vendor=group(Vendor,line);
        if (device) {session->dispenserDeviceId=*device;if(model)session->dispenserModel=*model;if(vendor)session->dispenserVendor=*vendor;
            if (!dispensers.count(*device)) dispensers[*device]={{"model",session->dispenserModel.empty()?"?":session->dispenserModel},{"vendor",session->dispenserVendor.empty()?"?":session->dispenserVendor}};}
        session->dispensedEvents.push_back(dispensed);if(eventHas(dispensed,"statusDetail"))++session->retryCount;
        session->addEvent(logEvent(*dt,"dispensedEvent",line,300,{{"bankIndex",dispensed.contains("bankIndex")?dispensed["bankIndex"]:Json(nullptr)},
            {"requested",dispensed.contains("notesAmountRequested")?dispensed["notesAmountRequested"]:Json(nullptr)},{"dispensed",dispensed.value("notesAmountDispensed",0)},
            {"statusDetail",dispensed.contains("statusDetail")?dispensed["statusDetail"]:Json(nullptr)}}));++linesMatched;return;
    }
    if (line.find("INSERT INTO terminal_transactions_box")!=std::string::npos) {
        const auto trx=resolveTrx(line);if(trx.empty())return;const auto session=ensureSession(trx);
        for(std::sregex_iterator it(line.begin(),line.end(),BoxValues),end;it!=end;++it){const auto& m=*it;const auto id=std::stoll(m[1]);
            session->transactionId=id;transactionToTrx_[id]=trx;session->boxes.push_back({{"tt_id",id},{"box",std::stoi(m[2])},
                {"nominal",std::stoi(m[3])},{"count",std::stoi(m[4])},{"is_rejected",std::stoi(m[5])}});}
        session->addEvent(logEvent(*dt,"insertBox",line,200));++linesMatched;return;
    }
    if (line.find("UPDATE terminal_transactions")!=std::string::npos && line.find("status")!=std::string::npos &&
        (line.find("ok")!=std::string::npos || line.find("refund")!=std::string::npos)) {
        if(const auto id=group(DbId,line)){const auto transactionId=std::stoll(*id);std::string trx;
            if(transactionToTrx_.count(transactionId))trx=transactionToTrx_[transactionId];else trx=resolveTrx(line);
            if(!trx.empty()){const auto session=ensureSession(trx);session->transactionId=transactionId;
                if(const auto sum=group(DbSum,line))session->dispensedAmount=std::stoll(*sum);
                session->addEvent(logEvent(*dt,"updateTransaction",line,200,{{"db_status",group(DbStatus,line).value_or("")}}));++linesMatched;}}
        return;
    }
    if (line.find("cannotContinue")!=std::string::npos && line.find('{')!=std::string::npos) {
        const auto trx=resolveTrx(line);if(!trx.empty()){ensureSession(trx)->addEvent(logEvent(*dt,"cannotContinue",line,300));++linesMatched;}return;
    }
    if (line.find("Error occurs while withdrawing")!=std::string::npos) {
        if(const auto trx=group(ErrorTrx,line)){ensureSession(*trx)->addEvent(logEvent(*dt,"errorOccurs",line,250));++linesMatched;}return;
    }
    if (line.find("createCoupon")!=std::string::npos && line.find("status")!=std::string::npos) {
        const auto trx=resolveTrx(line);const auto value=group(CouponValue,line);
        if(!trx.empty()&&value){const auto session=ensureSession(trx);session->recoveryCoupon=std::to_string(absolute(*value));
            session->addEvent(logEvent(*dt,"createCoupon",line,250,{{"value",session->recoveryCoupon}}));++linesMatched;}return;
    }
    if (line.find("printSlip")!=std::string::npos && (line.find("BARCODE")!=std::string::npos || line.find("balance")!=std::string::npos)) {
        const auto trx=resolveTrx(line);if(!trx.empty()){const auto session=ensureSession(trx);session->receiptPrinted=true;
            session->addEvent(logEvent(*dt,"printSlip",line,150));++linesMatched;}return;
    }
    if (line.find("Complete transaction")!=std::string::npos) {
        if(const auto trx=group(CompleteTrx,line)){ensureSession(*trx)->addEvent(logEvent(*dt,"completeTransaction",line,200));++linesMatched;}return;
    }
    if (line.find("Redirect to HOME")!=std::string::npos && line.find("withdraw")!=std::string::npos) {
        const auto trx=resolveTrx(line);if(!trx.empty()){const bool failed=line.find("failed withdraw")!=std::string::npos;
            ensureSession(trx)->addEvent(logEvent(*dt,failed?"redirectFailed":"redirectOk",line,150));++linesMatched;}return;
    }
    if(line.find("COLLECTION REPORT")!=std::string::npos||line.find("Withdraw report")!=std::string::npos||line.find("Cash in")!=std::string::npos||
       line.find("takenFromDispenserBoxes")!=std::string::npos||line.find("addedToDispenserBoxes")!=std::string::npos||line.find("outByNotes")!=std::string::npos)
        parseCashEvent(line,*dt);
}

void W2FrontendAnalyzer::parseCashEvent(const std::string& line,const DateTime& dt) {
    std::string type;
    if(line.find("COLLECTION REPORT")!=std::string::npos)type=W2CashEvent::Collection;
    else if(line.find("Withdraw report")!=std::string::npos||line.find("takenFromDispenserBoxes")!=std::string::npos)type=W2CashEvent::Withdraw;
    else if(line.find("Cash in")!=std::string::npos||line.find("addedToDispenserBoxes")!=std::string::npos)type=W2CashEvent::CashIn;
    else if(line.find("outByNotes")!=std::string::npos)type=W2CashEvent::Collection;
    if(type.empty())return;const auto key=type+dt.iso();if(!seenCashEvents_.insert(key).second)return;
    const auto text=unescape(line);W2CashEvent cash(type,dt);
    if(type==W2CashEvent::CashIn){if(const auto payload=payloadAfter(text,"addedToDispenserBoxes")){
        cash.boxesDelta=parseBoxes(unescape(*payload));cash.rawDetails=truncate(*payload,500);}
        if(const auto total=group(TotalSum,text))cash.totalCents=std::stoll(*total)*100;
    }else if(type==W2CashEvent::Withdraw){
        if(const auto payload=payloadAfter(text,"takenFromDispenserBoxes")){for(const auto&[box,count]:parseBoxes(*payload))cash.boxesDelta[box]=-count;cash.rawDetails="Dispenser: "+truncate(*payload,300);}
        if(const auto payload=payloadAfter(text,"takenFromRejectedBox")){cash.rejectDelta=parseNotes(*payload);cash.rawDetails+=(cash.rawDetails.empty()?"":" | ")+std::string("Reject: ")+truncate(*payload,cash.rawDetails.empty()?300:200);}
        if(const auto total=group(TotalSum,text))cash.totalCents=std::stoll(*total)*100;
    }else{
        if(const auto payload=payloadAfter(text,"outByNotes")){cash.rawByNominal=parseNotes(*payload);cash.rawDetails="OUT: "+truncate(*payload,300);}
        if(const auto total=group(TotalOut,text))cash.totalCents=std::stoll(*total)*100;
    }
    if(!cash.boxesDelta.empty()||!cash.rejectDelta.empty()||!cash.rawByNominal.empty()||cash.totalCents>0){cashEvents.push_back(std::move(cash));++linesMatched;}
    else seenCashEvents_.erase(key);
}

void W2FrontendAnalyzer::buildSessions() {
    sessions.clear();for(auto&[trx,session]:sessionsByTrx_){session->finalizeStatus();sessions.push_back(session);}
    std::sort(sessions.begin(),sessions.end(),[](const auto&a,const auto&b){return a->startDateTime<b->startDateTime;});
    std::sort(cashEvents.begin(),cashEvents.end(),[](const auto&a,const auto&b){return a.dateTime<b.dateTime;});
}
const W2CashEvent* W2FrontendAnalyzer::lastCashEvent(const DateTime& before,const std::string& type) const {
    const W2CashEvent* result=nullptr;for(const auto& event:cashEvents)if(event.dateTime&&*event.dateTime<before&&(type.empty()||event.eventType==type)&&(!result||*result->dateTime<*event.dateTime))result=&event;return result;
}
W2FrontendStatistics W2FrontendAnalyzer::getStats() const {
    W2FrontendStatistics stats;stats.total=static_cast<int>(sessions.size());stats.linesProcessed=linesProcessed;stats.linesMatched=linesMatched;stats.cashEventsCount=static_cast<int>(cashEvents.size());
    for(const auto&session:sessions){++stats.statusCounts[session->status];stats.totalRequestedCents+=session->requestedAmount;stats.totalDispensedCents+=session->dispensedAmount;
        if(!session->errorDetail.empty()){++stats.errorReasons[session->errorDetail];std::map<std::string,int>*target=nullptr;
            if(session->status=="FAILED_HARDWARE")target=&stats.hardwareReasons;else if(session->status=="FAILED_JAM")target=&stats.jamReasons;
            else if(session->status=="FAILED_EMPTY")target=&stats.emptyReasons;else if(session->status=="FAILED_TIMEOUT")target=&stats.timeoutReasons;if(target)++(*target)[session->errorDetail];}
        if(session->status=="FAILED_COUPON"&&!session->payoutCouponStatus.empty())++stats.couponReasons[session->payoutCouponStatus];
        for(const auto&box:session->boxes)if(eventInt(box,"is_rejected")==2)stats.denomDispensed[static_cast<int>(eventInt(box,"nominal"))]+=static_cast<int>(eventInt(box,"count"));
        if(session->hasSuspiciousMatch())++stats.suspiciousTotal;}
    for(const auto&event:cashEvents)++stats.cashEventsByType[event.eventType];return stats;
}

} // namespace fluxus
