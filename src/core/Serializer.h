#pragma once

#include "AnalysisEngine.h"

namespace fluxus {

class AnalysisSerializer {
public:
    inline static constexpr int CurrentVersion = 2;
    static void save(const std::string& path,const AnalysisResult& result);
    static AnalysisResult load(const std::string& path);
};

} // namespace fluxus
