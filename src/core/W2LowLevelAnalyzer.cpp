#include "W2LowLevelAnalyzer.h"
#include "LogUtils.h"

#include <algorithm>
#include <iomanip>
#include <regex>
#include <sstream>

namespace fluxus {
namespace {
const std::regex HandleHeader(R"rx(Handle Dispense Request:\s+reqID=(\d+)\s+commands=(\d+))rx");
const std::regex HandleCmd(R"rx(Handle Dispense Request:\s+reqID=(\d+)\s+cmdIndex=(\d+)\s+bankIndex=(\d+)\s+amount=(\d+))rx");
const std::regex WorkerStart(R"rx(Dispense worker:\s+job start\s+reqID=(\d+)\s+jobID=(\d+))rx");
const std::regex CmdStart(R"rx(Dispense:\s+start\s+reqID=(\d+)\s+jobID=(\d+)\s+cmdIndex=(\d+)\s+bankIndex=(\d+)\s+amount=(\d+))rx");
const std::regex RawHex(R"rx(Dispense:\s+raw response\s+.*?hex=([0-9a-fA-F]+))rx");
const std::regex Sensors2000(R"rx(Dispense:\s+upper CHK=(\d+)\s+EXIT=(\d+);\s+lower CHK=(\d+)\s+EXIT=(\d+))rx");
const std::regex Status2000(R"rx(Dispense:\s+status=0x([0-9a-fA-F]+)\s+\(([^)]+)\))rx");
const std::regex CmdOk(R"rx(CommandDispense:\s+chk=(\d+)\s+disp=(\d+)\s+rej=(\d+))rx");
const std::regex CmdErr(R"rx(Dispense:\s+error\s+reqID=(\d+)\s+jobID=(\d+)\s+cmdIndex=(\d+)\s+bankIndex=(\d+)\s+amount=(\d+)\s+chkSensor=(\d+)\s+dispensed=(\d+)\s+rejected=(\d+))rx");
const std::regex ErrText(R"rx(error="([^"]+)")rx");
const std::regex Settled(R"rx(Dispense:\s+settled\s+reqID=(\d+)\s+jobID=(\d+)\s+cmdIndex=(\d+)\s+bankIndex=(\d+)\s+amount=(\d+)\s+attempts=(\d+))rx");
const std::regex EventOk(R"rx(Dispense:\s+event OK\s+reqID=(\d+)\s+jobID=(\d+)\s+cmdIndex=(\d+)\s+bankIndex=(\d+)\s+amount=(\d+)\s+requested=(\d+)\s+chk=(\d+)\s+dispensed=(\d+)\s+rejected=(\d+))rx");
const std::regex JobDone(R"rx(Dispense worker:\s+job done\s+reqID=(\d+)\s+jobID=(\d+))rx");
const std::regex SeqStart(R"rx(LCDM4000 CCTalk:\s+Dispense sequence: start command.*?bankIndex=(\d+).*?(?:notesAmount|amount)=(\d+))rx");
const std::regex WritePayload(R"rx(LCDM4000 CCTalk:\s+Processor:\s+Write:\s+Dispense.*?payload=([0-9a-fA-F]+))rx");
const std::regex Err4000(R"rx(ERR.*?LCDM4000 CCTalk:\s+Command:\s+Dispense\s+error="error code:\s+(\d+)\s+-\s+([^"]+)")rx");
const std::regex Idx(R"rx(idx=(\d+))rx");
const std::regex NoteResult(R"rx(LCDM4000 CCTalk:\s+Dispense:\s+note result.*?bankIndex=(\d+).*?chkSensorTotal=(\d+).*?deviceCode=(\d+))rx");
const std::regex NoteDescription(R"rx(deviceDescription="([^"]+)")rx");
const std::regex NoteCounts(R"rx(dispensedTotal=(\d+).*?noteDispensed=(\d+).*?noteN=(\d+).*?noteRejected=(\d+))rx");
const std::regex Stop4000(R"rx(WRN.*?LCDM4000 CCTalk:\s+Dispense:\s+stop after device status.*?bankIndex=(\d+).*?deviceCode=(\d+))rx");
const std::regex Finished4000(R"rx(LCDM4000 CCTalk:\s+Dispense:\s+finished.*?bankIndex=(\d+).*?chkSensorTotal=(\d+).*?dispensedTotal=(\d+).*?notesAmount=(\d+).*?rejectedTotal=(\d+))rx");
const std::regex StatusSensors(R"rx(LCDM4000 CCTalk:\s+Command:\s+Status\s+SensorDVTL=(\w+)\s+SensorDVTR=(\w+)\s+SensorEJT=(\w+)\s+SensorEXIT=(\w+)\s+SensorRJT=(\w+))rx");
const std::regex StatusCassette(R"rx(LCDM4000 CCTalk:\s+Command:\s+Status\s+Cassette(\d+)Exists=(\w+)\s+Cassette\d+Length=(\d+)\s+Cassette\d+Opacity=(\d+)\s+Cassette\d+Status=(\d+))rx");
const std::regex SerialPath(R"rx(serialPath=(\S+))rx");
int number(const std::smatch& match,int index){return std::stoi(match[index]);}
std::optional<std::string> group(const std::regex& pattern,const std::string& line){std::smatch m;if(std::regex_search(line,m,pattern))return m[1].str();return std::nullopt;}
std::string pathOf(const std::string& line){return group(SerialPath,line).value_or("");}
void set(Event& target,std::initializer_list<Json::value_type> values){
    Event patch=Event::object();
    for(const auto& value:values)if(value.is_array()&&value.size()==2)patch[value[0].get<std::string>()]=value[1];
    target.update(patch);
}
std::set<int> banks(const Events& commands){std::set<int> result;for(const auto&command:commands)if(eventHas(command,"bankIndex"))result.insert(static_cast<int>(eventInt(command,"bankIndex")));return result;}
DateTime effectiveEnd(const W2WithdrawSession& session){std::optional<DateTime> result;
    for(const auto&event:session.events){const auto kind=eventString(event,"kind");if(kind=="dispensedEvent"||kind=="cannotContinue"||kind=="errorOccurs"){
        if(const auto dt=eventDate(event,"dt");dt&&(!result||*dt>*result))result=dt;}}
    return result.value_or(session.endDateTime.value_or(*session.startDateTime));}
std::vector<std::string> suspicion(const W2WithdrawSession& frontend,const W2LowLevelSession& low,const std::optional<DateTime>& previousEnd){
    std::vector<std::string> flags;
    if(frontend.startDateTime&&low.startDateTime){const auto gap=low.startDateTime->unix()-frontend.startDateTime->unix();if(std::abs(gap)>30)flags.push_back("gap "+std::string(gap>0?"+":"-")+std::to_string(std::abs(gap))+"s");}
    if(previousEnd&&low.startDateTime){const auto gap=low.startDateTime->unix()-previousEnd->unix();if(gap>60)flags.push_back("retry pause "+std::to_string(gap)+"s");}
    const auto fe=banks(frontend.commands),ll=banks(low.commands);if(!fe.empty()&&!ll.empty()){std::vector<int> diff;std::set_difference(ll.begin(),ll.end(),fe.begin(),fe.end(),std::back_inserter(diff));
        if(!diff.empty()){std::string value="другой банк (bank ";for(std::size_t i=0;i<diff.size();++i){if(i)value+=",";value+=std::to_string(diff[i]);}value+=" vs [";for(auto it=fe.begin();it!=fe.end();++it){if(it!=fe.begin())value+=", ";value+=std::to_string(*it);}flags.push_back(value+"])" );}}
    int total=0;for(const auto&command:frontend.commands)total+=static_cast<int>(eventInt(command,"notesAmount"));const int dispensed=low.totalDispensed();
    if(total>0&&dispensed>total)flags.push_back("выдано больше ("+std::to_string(dispensed)+">"+std::to_string(total)+")");return flags;}
}

std::shared_ptr<W2LowLevelSession> W2LowLevelAnalyzer::get2000(int request,const std::string& line){auto&session=active2000_[request];if(!session){session=std::make_shared<W2LowLevelSession>();session->model="LCDM2000";session->requestId=request;session->serialPath=pathOf(line);sessions.push_back(session);if(!session->serialPath.empty()&&!dispensers.count(session->serialPath))dispensers[session->serialPath]={{"model","LCDM2000"}};}return session;}
std::shared_ptr<W2LowLevelSession> W2LowLevelAnalyzer::get4000(int bank,const std::string& line,const std::string& idx){auto&session=active4000_[bank];if(session&&session->closed)session.reset();if(!session){session=std::make_shared<W2LowLevelSession>();session->model="LCDM4000";session->serialPath=pathOf(line);session->idx=idx;sessions.push_back(session);if(!session->serialPath.empty()&&!dispensers.count(session->serialPath))dispensers[session->serialPath]={{"model","LCDM4000"}};}else if(!idx.empty()&&session->idx.empty())session->idx=idx;return session;}
std::shared_ptr<W2LowLevelSession> W2LowLevelAnalyzer::find2000(const std::string& path){std::shared_ptr<W2LowLevelSession> result;for(const auto&[id,session]:active2000_)if((path.empty()||session->serialPath==path)&&(!result||result->startDateTime<session->startDateTime))result=session;return result;}
std::shared_ptr<W2LowLevelSession> W2LowLevelAnalyzer::find4000(const std::string& path){std::shared_ptr<W2LowLevelSession> result;for(const auto&[id,session]:active4000_)if((path.empty()||session->serialPath==path)&&(!result||result->startDateTime<session->startDateTime))result=session;return result;}
Event& W2LowLevelAnalyzer::openResult(W2LowLevelSession& session){if(session.results.empty()||eventBool(session.results.back(),"closed"))session.results.push_back({{"closed",false}});return session.results.back();}

void W2LowLevelAnalyzer::processLine(const std::string& line){
    ++linesProcessed;const auto clean=stripAnsi(line);const auto dt=extractDevicesDateTime(clean);if(!dt)return;std::smatch m;
    if(std::regex_search(clean,m,HandleHeader)){const auto session=get2000(number(m,1),clean);session->addEvent("handleHeader",*dt,truncate(clean,200),{{"commands_count",number(m,2)}});++linesMatched;return;}
    if(std::regex_search(clean,m,HandleCmd)){const auto req=number(m,1),cmd=number(m,2),bank=number(m,3),amount=number(m,4);const auto session=get2000(req,clean);
        session->commands.push_back({{"cmdIndex",cmd},{"bankIndex",bank},{"amount",amount}});session->addEvent("handleCmd",*dt,truncate(clean,200),{{"cmdIndex",cmd},{"bankIndex",bank},{"amount",amount}});++linesMatched;return;}
    if(std::regex_search(clean,m,WorkerStart)){const auto req=number(m,1),job=number(m,2);const auto session=get2000(req,clean);session->jobId=job;session->addEvent("workerStart",*dt,truncate(clean,200),{{"jobID",job}});++linesMatched;return;}
    if(std::regex_search(clean,m,CmdStart)){const auto req=number(m,1),job=number(m,2),cmd=number(m,3),bank=number(m,4),amount=number(m,5);const auto session=get2000(req,clean);session->jobId=job;
        session->addEvent("cmdStart",*dt,truncate(clean,200),{{"cmdIndex",cmd},{"bankIndex",bank},{"amount",amount}});++linesMatched;return;}
    if(std::regex_search(clean,m,RawHex)){if(const auto session=find2000(pathOf(clean))){session->addEvent("rawHex",*dt,truncate(clean,200),{{"hex",m[1].str()}});++linesMatched;}return;}
    if(std::regex_search(clean,m,Sensors2000)){if(const auto session=find2000(pathOf(clean))){session->addEvent("sensors",*dt,truncate(clean,200),{{"upper_chk",number(m,1)},{"upper_exit",number(m,2)},{"lower_chk",number(m,3)},{"lower_exit",number(m,4)}});++linesMatched;}return;}
    if(std::regex_search(clean,m,Status2000)){if(const auto session=find2000(pathOf(clean))){const int code=std::stoi(m[1].str(),nullptr,16);const auto text=m[2].str();session->addEvent("statusCode",*dt,truncate(clean,200),{{"status_code",code},{"status_text",text}});auto&result=openResult(*session);result["status_code"]=code;result["status_text"]=text;++linesMatched;}return;}
    if(std::regex_search(clean,m,CmdOk)){if(const auto session=find2000(pathOf(clean))){const int chk=number(m,1),disp=number(m,2),rej=number(m,3);auto&result=openResult(*session);
        result["chk_sensor"]=eventInt(result,"chk_sensor")+chk;result["dispensed"]=eventInt(result,"dispensed")+disp;result["rejected"]=eventInt(result,"rejected")+rej;
        session->addEvent("cmdOk",*dt,truncate(clean,200),{{"chk",chk},{"dispensed",disp},{"rejected",rej},{"total_dispensed",eventInt(result,"dispensed")}});++linesMatched;}return;}
    if(std::regex_search(clean,m,CmdErr)){const int req=number(m,1),job=number(m,2),cmd=number(m,3),bank=number(m,4),amount=number(m,5),chk=number(m,6),disp=number(m,7),rej=number(m,8);
        const auto session=get2000(req,clean);session->jobId=job;const auto error=group(ErrText,clean).value_or("");auto&result=openResult(*session);
        set(result,{{"cmdIndex",cmd},{"bankIndex",bank},{"requested",amount},{"chk_sensor",chk},{"dispensed",disp},{"rejected",rej},{"error",error},{"closed",true}});
        session->addEvent("cmdErr",*dt,truncate(clean,200),{{"cmdIndex",cmd},{"bankIndex",bank},{"chk",chk},{"dispensed",disp},{"rejected",rej},{"error",error}});session->closed=true;active2000_.erase(req);++linesMatched;return;}
    if(std::regex_search(clean,m,Settled)){const auto req=number(m,1);if(active2000_.count(req)){active2000_[req]->addEvent("settled",*dt,truncate(clean,200),{{"cmdIndex",number(m,3)},{"bankIndex",number(m,4)},{"amount",number(m,5)},{"attempts",number(m,6)}});++linesMatched;}return;}
    if(std::regex_search(clean,m,EventOk)){const auto req=number(m,1);if(active2000_.count(req)){const auto session=active2000_[req];const int cmd=number(m,3),bank=number(m,4),requested=number(m,6),chk=number(m,7),disp=number(m,8),rej=number(m,9);auto&result=openResult(*session);
        set(result,{{"cmdIndex",cmd},{"bankIndex",bank},{"requested",requested},{"chk_sensor",chk},{"dispensed",disp},{"rejected",rej},{"closed",true}});
        session->addEvent("eventOk",*dt,truncate(clean,200),{{"cmdIndex",cmd},{"bankIndex",bank},{"requested",requested},{"chk",chk},{"dispensed",disp},{"rejected",rej}});
        const auto closed=static_cast<int>(std::count_if(session->results.begin(),session->results.end(),[](const auto&r){return eventBool(r,"closed");}));if(closed>=static_cast<int>(session->commands.size())){session->closed=true;active2000_.erase(req);}++linesMatched;}return;}
    if(std::regex_search(clean,m,JobDone)){const auto req=number(m,1);if(active2000_.count(req)){const auto session=active2000_[req];session->addEvent("jobDone",*dt,truncate(clean,200),{{"jobID",number(m,2)}});session->closed=true;active2000_.erase(req);++linesMatched;}return;}
    if(std::regex_search(clean,m,SeqStart)){const int bank=number(m,1),amount=number(m,2);const auto session=get4000(bank,clean);session->commands.push_back({{"cmdIndex",session->commands.size()},{"bankIndex",bank},{"amount",amount}});session->addEvent("seqStart",*dt,truncate(clean,200),{{"bankIndex",bank},{"amount",amount}});++linesMatched;return;}
    if(std::regex_search(clean,m,WritePayload)){if(const auto session=find4000(pathOf(clean))){session->addEvent("writePayload",*dt,truncate(clean,200),{{"payload",m[1].str()}});++linesMatched;}return;}
    if(std::regex_search(clean,m,Err4000)){if(const auto session=find4000(pathOf(clean))){const int code=number(m,1);const auto error=m[2].str();const auto idx=group(Idx,clean).value_or("");if(!idx.empty()&&session->idx.empty())session->idx=idx;auto&result=openResult(*session);set(result,{{"device_code",code},{"error",error}});session->addEvent("lcdm4000Err",*dt,truncate(clean,200),{{"device_code",code},{"error",error},{"idx",idx}});++linesMatched;}return;}
    if(std::regex_search(clean,m,NoteResult)){const int bank=number(m,1);if(active4000_.count(bank)){const auto session=active4000_[bank];const int chk=number(m,2),code=number(m,3);const auto description=group(NoteDescription,clean).value_or("");std::smatch counts;const bool has=std::regex_search(clean,counts,NoteCounts);const int disp=has?number(counts,1):0,noteN=has?number(counts,3):0,rej=has?number(counts,4):0;auto&result=openResult(*session);set(result,{{"bankIndex",bank},{"chk_sensor",chk},{"device_code",code},{"description",description},{"dispensed",disp},{"rejected",rej},{"requested",noteN}});session->addEvent("noteResult",*dt,truncate(clean,200),{{"bankIndex",bank},{"device_code",code},{"description",description},{"dispensed",disp},{"rejected",rej}});++linesMatched;}return;}
    if(std::regex_search(clean,m,Stop4000)){const int bank=number(m,1);if(active4000_.count(bank)){const auto session=active4000_[bank];const int code=number(m,2);auto&result=openResult(*session);set(result,{{"device_code",code},{"stopped",true},{"closed",true}});if(eventString(result,"error").empty())result["error"]=classifyLcdm4000(code).first;session->addEvent("lcdm4000Stop",*dt,truncate(clean,200),{{"bankIndex",bank},{"device_code",code}});++linesMatched;}return;}
    if(std::regex_search(clean,m,Finished4000)){const int bank=number(m,1);if(active4000_.count(bank)){const auto session=active4000_[bank];const int chk=number(m,2),disp=number(m,3),amount=number(m,4),rej=number(m,5);auto&result=openResult(*session);set(result,{{"bankIndex",bank},{"chk_sensor",chk},{"dispensed",disp},{"rejected",rej},{"requested",amount},{"closed",true}});session->addEvent("lcdm4000Finished",*dt,truncate(clean,200),{{"bankIndex",bank},{"chk_sensor",chk},{"dispensed",disp},{"requested",amount},{"rejected",rej}});session->closed=true;active4000_.erase(bank);++linesMatched;}return;}
    if(std::regex_search(clean,m,StatusSensors)){std::array<std::string,5> values{m[1].str(),m[2].str(),m[3].str(),m[4].str(),m[5].str()};Event state={{"dt",dt->iso()},{"DVTL",values[0]},{"DVTR",values[1]},{"EJT",values[2]},{"EXIT",values[3]},{"RJT",values[4]}};lcdmHealth.push_back({"sensors",state});if(!lastSensorState_||*lastSensorState_!=values){lcdmHealthChanges.push_back({"sensors",state});lastSensorState_=values;}++linesMatched;return;}
    if(std::regex_search(clean,m,StatusCassette)){const int num=number(m,1);const auto exists=m[2].str();const int length=number(m,3),opacity=number(m,4),status=number(m,5);Event state={{"dt",dt->iso()},{"num",num},{"exists",exists},{"length",length},{"opacity",opacity},{"status",status}};lcdmHealth.push_back({"cassette",state});const auto tuple=std::make_tuple(exists,length,opacity,status);if(!lastCassetteState_.count(num)||lastCassetteState_[num]!=tuple){lcdmHealthChanges.push_back({"cassette",state});lastCassetteState_[num]=tuple;}++linesMatched;}
}

void W2LowLevelAnalyzer::buildSessions(){for(auto&session:sessions)session->finalizeStatus();std::sort(sessions.begin(),sessions.end(),[](const auto&a,const auto&b){return a->startDateTime<b->startDateTime;});}
void W2LowLevelAnalyzer::correlateWithFrontend(const std::vector<std::shared_ptr<W2WithdrawSession>>& frontendSessions,int before,int after,int buffer){
    std::vector<std::shared_ptr<W2WithdrawSession>> frontend;for(const auto&s:frontendSessions)if(s->startDateTime)frontend.push_back(s);std::sort(frontend.begin(),frontend.end(),[](const auto&a,const auto&b){return a->startDateTime<b->startDateTime;});std::set<const W2LowLevelSession*>used;
    for(std::size_t i=0;i<frontend.size();++i){const auto&session=frontend[i];const auto start=session->startDateTime->unix();const auto feBanks=banks(session->commands);auto upper=effectiveEnd(*session).unix()+after;
        for(std::size_t j=i+1;j<frontend.size();++j){const auto&next=frontend[j];const bool same=session->dispenserDeviceId.empty()||next->dispenserDeviceId.empty()||session->dispenserDeviceId==next->dispenserDeviceId;if(same&&next->startDateTime){upper=std::min(upper,next->startDateTime->unix()-buffer);break;}}
        if(upper<start)continue;std::vector<std::shared_ptr<W2LowLevelSession>> candidates;
        for(const auto&ll:sessions){if(used.count(ll.get())||!ll->startDateTime)continue;const auto time=ll->startDateTime->unix();if(time<start-before||time>upper)continue;const auto llBanks=banks(ll->commands);bool overlap=feBanks.empty()||llBanks.empty();for(int bank:llBanks)if(feBanks.count(bank))overlap=true;if(overlap)candidates.push_back(ll);}
        std::sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a->startDateTime<b->startDateTime;});std::optional<DateTime>previousEnd;
        for(const auto&ll:candidates){session->lowLevelSessions.push_back(ll);session->suspicionFlags.push_back(suspicion(*session,*ll,previousEnd));used.insert(ll.get());previousEnd=ll->endDateTime?ll->endDateTime:ll->startDateTime;}}
}
W2LowLevelStatistics W2LowLevelAnalyzer::getStats()const{W2LowLevelStatistics stats;stats.total=static_cast<int>(sessions.size());stats.healthRecords=static_cast<int>(lcdmHealth.size());stats.healthChanges=static_cast<int>(lcdmHealthChanges.size());stats.linesProcessed=linesProcessed;stats.linesMatched=linesMatched;
    for(const auto&session:sessions){++stats.byModel[session->model.empty()?"unknown":session->model];++stats.statusCounts[session->status];std::string code;if(session->lcdmStatusCode){std::ostringstream text;text<<"0x"<<std::uppercase<<std::hex<<std::setw(2)<<std::setfill('0')<<*session->lcdmStatusCode<<' '<<classifyLcdmStatus(*session->lcdmStatusCode).first;code=text.str();}else if(session->lcdmDeviceCode)code="code="+std::to_string(*session->lcdmDeviceCode)+" "+classifyLcdm4000(*session->lcdmDeviceCode,session->errorDetail).first;if(!code.empty())++stats.lcdmCodesCount[code];}return stats;}

} // namespace fluxus
