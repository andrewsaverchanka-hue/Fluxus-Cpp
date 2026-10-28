#pragma once

#include "LimaAnalyzer.h"
#include "W2FrontendAnalyzer.h"
#include "W2LowLevelAnalyzer.h"
#include <functional>

namespace fluxus {

struct AnalysisResult {
    LimaAnalyzer lima;
    W2FrontendAnalyzer w2Frontend;
    W2LowLevelAnalyzer w2LowLevel;
    std::vector<std::string> sourceFiles;
    DateTime savedAt = DateTime::now();
    int version = 2;
};
using StreamInput = std::tuple<std::string,std::string,std::string>;
using AnalysisProgress = std::function<void(int completed,int total,const std::string& file,int parallel)>;

class AnalysisEngine {
public:
    static AnalysisResult analyzeDevice(const std::vector<std::string>& files,const std::function<void(const std::string&)>& fileProgress = {});
    static AnalysisResult analyze(const std::vector<std::string>& files,const AnalysisProgress& progress = {},int maxParallelDevices = 0);
    static AnalysisResult analyzeStreams(const std::vector<StreamInput>& streams);
    static void finalize(AnalysisResult& result);
    static AnalysisResult combine(std::vector<AnalysisResult> results);
};

} // namespace fluxus
