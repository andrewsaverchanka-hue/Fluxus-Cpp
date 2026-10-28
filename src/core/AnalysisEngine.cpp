#include "AnalysisEngine.h"
#include "LogUtils.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <sstream>
#include <thread>

namespace fluxus {

AnalysisResult AnalysisEngine::analyzeDevice(const std::vector<std::string>& files,const std::function<void(const std::string&)>& fileProgress) {
    AnalysisResult result;result.sourceFiles=files;
    for(const auto&path:files){if(fileProgress)fileProgress(path);auto type=detectLogType(path);if(type=="unknown"){
        const auto name=std::filesystem::path(path).filename().string();type=name.find("skills-battle")!=std::string::npos?"frontend":"devices";}
        LineReader reader(path);if(!reader.isOpen())throw std::runtime_error("Не удалось открыть лог: "+path);std::string line;
        if(type=="frontend")while(reader.readLine(line))result.w2Frontend.processLine(line);
        else while(reader.readLine(line)){result.lima.processLine(line);result.w2LowLevel.processLine(line);}}
    finalize(result);return result;
}

AnalysisResult AnalysisEngine::analyze(const std::vector<std::string>& files,const AnalysisProgress& progress,int maxParallelDevices) {
    if(files.empty())return {};
    std::map<std::string,std::vector<std::string>> groups;for(const auto&path:files)groups[std::filesystem::path(path).parent_path().string()].push_back(path);
    std::vector<std::pair<std::string,std::vector<std::string>>> work(groups.begin(),groups.end());
    std::vector<std::optional<AnalysisResult>> results(work.size());std::atomic<std::size_t>next{0};std::atomic<int>completed{0};
    int parallel=maxParallelDevices>0?maxParallelDevices:std::min<int>(std::max(1u,std::thread::hardware_concurrency()),4);parallel=std::max(1,std::min<int>(parallel,work.size()));
    std::atomic<bool>failed{false};std::exception_ptr error;std::mutex errorMutex;
    auto worker=[&]{while(!failed){const auto index=next.fetch_add(1);if(index>=work.size())break;try{
        const auto&entry=work[index];results[index]=analyzeDevice(entry.second,[&](const std::string&file){if(progress)progress(completed.load(),static_cast<int>(files.size()),file,parallel);});
        completed+=static_cast<int>(entry.second.size());if(progress)progress(completed.load(),static_cast<int>(files.size()),entry.second.back(),parallel);
    }catch(...){failed=true;std::lock_guard lock(errorMutex);if(!error)error=std::current_exception();}}};
    std::vector<std::thread>threads;for(int i=0;i<parallel;++i)threads.emplace_back(worker);for(auto&thread:threads)thread.join();if(error)std::rethrow_exception(error);
    std::vector<AnalysisResult>ready;for(auto&result:results)if(result)ready.push_back(std::move(*result));return combine(std::move(ready));
}

AnalysisResult AnalysisEngine::analyzeStreams(const std::vector<StreamInput>& streams) {
    AnalysisResult result;std::string line;
    for(const auto&[name,text,type]:streams){result.sourceFiles.push_back(name);std::istringstream input(text);
        if(type=="frontend")while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();result.w2Frontend.processLine(line);}
        else while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();result.lima.processLine(line);result.w2LowLevel.processLine(line);}}
    finalize(result);return result;
}

void AnalysisEngine::finalize(AnalysisResult& result) {
    result.lima.buildSessions();result.w2Frontend.buildSessions();result.w2LowLevel.buildSessions();result.w2LowLevel.correlateWithFrontend(result.w2Frontend.sessions);
}

AnalysisResult AnalysisEngine::combine(std::vector<AnalysisResult> results) {
    if(results.empty())return {};if(results.size()==1)return std::move(results[0]);AnalysisResult combined;
    for(auto&result:results){combined.sourceFiles.insert(combined.sourceFiles.end(),result.sourceFiles.begin(),result.sourceFiles.end());
        int offset=static_cast<int>(combined.lima.sessions.size());for(auto&session:result.lima.sessions){session.sessionId+=offset;combined.lima.sessions.push_back(std::move(session));}
        combined.lima.allEvents.insert(combined.lima.allEvents.end(),result.lima.allEvents.begin(),result.lima.allEvents.end());
        combined.lima.bnvEvents.insert(combined.lima.bnvEvents.end(),result.lima.bnvEvents.begin(),result.lima.bnvEvents.end());
        combined.lima.collections.insert(combined.lima.collections.end(),result.lima.collections.begin(),result.lima.collections.end());
        combined.lima.totalCount+=result.lima.totalCount;combined.lima.totalSum+=result.lima.totalSum;combined.lima.linesFound+=result.lima.linesFound;combined.lima.linesParsed+=result.lima.linesParsed;
        for(const auto&[currency,denominations]:result.lima.stats)for(const auto&[denomination,count]:denominations)combined.lima.stats[currency][denomination]+=count;
        for(const auto&[model,serials]:result.lima.statsByDevice)for(const auto&[serial,currencies]:serials)for(const auto&[currency,denominations]:currencies)for(const auto&[denomination,count]:denominations)combined.lima.statsByDevice[model][serial][currency][denomination]+=count;
        for(const auto&[serial,device]:result.lima.deviceModels)combined.lima.deviceModels[serial]=device;
        for(const auto&[name,count]:result.lima.bnvEventsCount)combined.lima.bnvEventsCount[name]+=count;
        for(const auto&[validator,events]:result.lima.bnvEventsByType)for(const auto&[name,count]:events)combined.lima.bnvEventsByType[validator][name]+=count;
        combined.w2Frontend.sessions.insert(combined.w2Frontend.sessions.end(),result.w2Frontend.sessions.begin(),result.w2Frontend.sessions.end());
        combined.w2Frontend.cashEvents.insert(combined.w2Frontend.cashEvents.end(),result.w2Frontend.cashEvents.begin(),result.w2Frontend.cashEvents.end());
        combined.w2Frontend.linesProcessed+=result.w2Frontend.linesProcessed;combined.w2Frontend.linesMatched+=result.w2Frontend.linesMatched;
        for(const auto&[id,device]:result.w2Frontend.dispensers)combined.w2Frontend.dispensers[id]=device;
        combined.w2LowLevel.sessions.insert(combined.w2LowLevel.sessions.end(),result.w2LowLevel.sessions.begin(),result.w2LowLevel.sessions.end());
        combined.w2LowLevel.lcdmHealth.insert(combined.w2LowLevel.lcdmHealth.end(),result.w2LowLevel.lcdmHealth.begin(),result.w2LowLevel.lcdmHealth.end());
        combined.w2LowLevel.lcdmHealthChanges.insert(combined.w2LowLevel.lcdmHealthChanges.end(),result.w2LowLevel.lcdmHealthChanges.begin(),result.w2LowLevel.lcdmHealthChanges.end());
        combined.w2LowLevel.linesProcessed+=result.w2LowLevel.linesProcessed;combined.w2LowLevel.linesMatched+=result.w2LowLevel.linesMatched;
        for(const auto&[id,device]:result.w2LowLevel.dispensers)combined.w2LowLevel.dispensers[id]=device;}
    combined.w2Frontend.restoreSessions(combined.w2Frontend.sessions);return combined;
}

} // namespace fluxus
