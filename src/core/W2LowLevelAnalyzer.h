#pragma once

#include "Models.h"

namespace fluxus {

struct W2LowLevelStatistics {
    int total = 0;
    std::map<std::string,int> byModel, statusCounts, lcdmCodesCount;
    int healthRecords = 0, healthChanges = 0;
    std::int64_t linesProcessed = 0, linesMatched = 0;
};

class W2LowLevelAnalyzer {
public:
    std::vector<std::shared_ptr<W2LowLevelSession>> sessions;
    std::vector<HealthRecord> lcdmHealth, lcdmHealthChanges;
    std::map<std::string,std::map<std::string,std::string>> dispensers;
    std::int64_t linesProcessed = 0, linesMatched = 0;

    void processLine(const std::string& line);
    void buildSessions();
    void correlateWithFrontend(const std::vector<std::shared_ptr<W2WithdrawSession>>& frontend,
        int startWindowBeforeSeconds = 10, int endWindowAfterSeconds = 60, int nextSessionBufferSeconds = 2);
    W2LowLevelStatistics getStats() const;
private:
    std::map<int,std::shared_ptr<W2LowLevelSession>> active2000_, active4000_;
    std::optional<std::array<std::string,5>> lastSensorState_;
    std::map<int,std::tuple<std::string,int,int,int>> lastCassetteState_;
    std::shared_ptr<W2LowLevelSession> get2000(int request,const std::string& line);
    std::shared_ptr<W2LowLevelSession> get4000(int bank,const std::string& line,const std::string& idx="");
    std::shared_ptr<W2LowLevelSession> find2000(const std::string& path);
    std::shared_ptr<W2LowLevelSession> find4000(const std::string& path);
    static Event& openResult(W2LowLevelSession& session);
};

} // namespace fluxus
