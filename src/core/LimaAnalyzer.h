#pragma once

#include "Models.h"
#include <set>

namespace fluxus {

using DenomStats = std::map<std::string, std::map<double, int>>;
using DeviceStats = std::map<std::string, std::map<std::string, DenomStats>>;

class LimaAnalyzer {
public:
    DenomStats stats;
    DeviceStats statsByDevice;
    std::map<std::string, std::map<std::string, std::string>> deviceModels;
    std::int64_t totalCount = 0;
    double totalSum = 0;
    std::int64_t linesFound = 0;
    std::int64_t linesParsed = 0;
    Events allEvents;
    std::vector<BanknoteSession> sessions;
    std::vector<CollectionRecord> collections;
    Events bnvEvents;
    std::map<std::string, int> bnvEventsCount;
    std::map<std::string, std::map<std::string, int>> bnvEventsByType;

    void processLine(const std::string& line);
    void buildSessions();
    struct BetweenCollections {
        std::optional<DenomStats> stats;
        std::int64_t count = 0;
        double sum = 0;
        std::optional<CollectionRecord> first;
        std::optional<CollectionRecord> last;
    };
    BetweenCollections betweenCollections() const;
private:
    std::set<std::string> collectionKeys_;
    void processCollection(const std::string& clean, const DateTime& dt);
    std::optional<Event> processBnv(const std::string& clean, const std::string& original);
};

} // namespace fluxus
