#pragma once

#include "Models.h"

namespace fluxus {

struct W2FrontendStatistics {
    int total = 0;
    std::map<std::string,int> statusCounts;
    std::map<int,int> denomDispensed;
    std::map<std::string,int> errorReasons, hardwareReasons, jamReasons, emptyReasons, timeoutReasons, couponReasons;
    std::int64_t totalRequestedCents = 0, totalDispensedCents = 0;
    std::int64_t linesProcessed = 0, linesMatched = 0;
    int suspiciousTotal = 0, cashEventsCount = 0;
    std::map<std::string,int> cashEventsByType;
};

class W2FrontendAnalyzer {
public:
    std::vector<std::shared_ptr<W2WithdrawSession>> sessions;
    std::map<std::string,std::map<std::string,std::string>> dispensers;
    std::int64_t linesProcessed = 0, linesMatched = 0;
    std::vector<W2CashEvent> cashEvents;

    std::shared_ptr<W2WithdrawSession> ensureSession(const std::string& trx);
    void restoreSessions(const std::vector<std::shared_ptr<W2WithdrawSession>>& value);
    void processLine(const std::string& line);
    void buildSessions();
    W2FrontendStatistics getStats() const;
    const W2CashEvent* lastCashEvent(const DateTime& before, const std::string& type = "") const;
private:
    std::map<std::string,std::shared_ptr<W2WithdrawSession>> sessionsByTrx_;
    std::map<std::string,std::string> activeTrxByUser_;
    std::map<std::int64_t,std::string> transactionToTrx_;
    std::set<std::string> seenCashEvents_;
    std::string activeTrx_;
    std::string resolveTrx(const std::string& line);
    void parseCashEvent(const std::string& line, const DateTime& dt);
};

} // namespace fluxus
