#pragma once

#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fluxus {

using Json = nlohmann::json;
using Event = Json;
using Events = std::vector<Event>;

struct DateTime {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    auto operator<=>(const DateTime&) const = default;
    std::int64_t unix() const;
    std::string date() const;
    std::string time() const;
    std::string iso() const;
    static std::optional<DateTime> fromIso(const std::string& value);
    static DateTime now();
};

std::optional<DateTime> eventDate(const Event& event, const std::string& key);
std::string eventString(const Event& event, const std::string& key, const std::string& fallback = "");
std::int64_t eventInt(const Event& event, const std::string& key, std::int64_t fallback = 0);
bool eventBool(const Event& event, const std::string& key, bool fallback = false);
bool eventHas(const Event& event, const std::string& key);
Event makeEvent(std::initializer_list<Json::value_type> values);

int denominationValue(int raw);
std::string classifyDispenseError(const std::string& detail);
std::pair<std::string, std::string> classifyLcdmStatus(int code);
std::pair<std::string, std::string> classifyLcdm4000(int code, const std::string& description = "");
std::string w2StatusDisplay(const std::string& status);

struct BanknoteSession {
    int sessionId = 0;
    Events events;
    std::int64_t startTimestamp = 0;
    std::string firstDate = "?";
    std::string firstTime = "?";
    std::string uuid = "N/A";
    std::string status = "INCOMPLETE";
    std::optional<int> denomination;
    std::optional<std::string> currency;
    std::string deviceModel = "Unknown";
    std::string deviceSerial = "N/A";
    bool closed = false;
    std::string errorReason;

    BanknoteSession() = default;
    BanknoteSession(int id, const Event& first);
    void addEvent(const Event& event);
    void markError(const std::string& reason);
    double displayDenomination() const;
private:
    void updateStatus(const Event& event);
};

struct W2CashEvent {
    inline static const std::string Collection = "collection";
    inline static const std::string Withdraw = "withdraw_report";
    inline static const std::string CashIn = "cash_in";

    std::string eventType;
    std::optional<DateTime> dateTime;
    double timestamp = 0;
    std::map<int, int> boxesDelta;
    std::map<int, int> rejectDelta;
    std::map<int, int> rawByNominal;
    std::int64_t totalCents = 0;
    std::string rawDetails;

    W2CashEvent() = default;
    W2CashEvent(std::string type, std::optional<DateTime> dt);
    std::string dateString() const;
    std::string timeString() const;
    std::string typeDisplay() const;
    bool isOutflow() const;
    bool isInflow() const;
};

struct W2LowLevelSession {
    std::string model;
    std::optional<int> requestId;
    std::optional<int> jobId;
    std::string idx;
    std::string serialPath;
    std::optional<DateTime> startDateTime;
    std::optional<DateTime> endDateTime;
    std::string startDateString = "?";
    std::string startTimeString = "?";
    Events commands;
    Events results;
    std::string status = "INCOMPLETE";
    std::string errorDetail;
    std::optional<int> lcdmStatusCode;
    std::optional<int> lcdmDeviceCode;
    Events events;
    bool closed = false;

    void addEvent(const std::string& kind, const DateTime& dt, const std::string& raw, const Event& payload = Event::object());
    int totalRequested() const;
    int totalDispensed() const;
    int totalRejected() const;
    void finalizeStatus();
    double durationSeconds() const;
};

struct W2WithdrawSession {
    std::string trx;
    Events events;
    std::optional<DateTime> startDateTime;
    std::optional<DateTime> endDateTime;
    std::string startDateString = "?";
    std::string startTimeString = "?";
    std::string coupon;
    std::string userLogin;
    std::string groupUuid;
    std::optional<std::int64_t> balanceId;
    std::optional<std::int64_t> transactionId;
    std::int64_t requestedAmount = 0;
    std::int64_t dispensedAmount = 0;
    Events commands;
    Events dispensedEvents;
    Events boxes;
    std::vector<std::vector<int>> cassetteSnapshot;
    std::string status = "INCOMPLETE";
    std::string errorDetail;
    int retryCount = 0;
    std::string payoutCouponStatus;
    std::string recoveryCoupon;
    bool receiptPrinted = false;
    std::string dispenserDeviceId;
    std::string dispenserModel;
    std::string dispenserVendor;
    std::vector<std::shared_ptr<W2LowLevelSession>> lowLevelSessions;
    std::vector<std::vector<std::string>> suspicionFlags;
    bool cassetteMismatch = false;
    std::map<int, std::array<int, 2>> cassetteMismatchDetails;
    bool cassetteMismatchCollective = false;
    int cassetteMismatchGroupSize = 1;

    explicit W2WithdrawSession(std::string id = "") : trx(std::move(id)) {}
    void addEvent(const Event& event);
    void finalizeStatus();
    double durationSeconds() const;
    bool hasSuspiciousMatch() const;
    int suspiciousCount() const;
    std::vector<std::string> allSuspicionFlags() const;
};

struct CollectionRecord {
    DateTime dateTime;
    std::string dateString;
    std::string timeString;
    std::map<int, int> notes;
    std::string raw;
};

struct HealthRecord {
    std::string kind;
    Event state = Event::object();
};

struct DeviceConnection {
    std::string id;
    std::string host;
    int port = 22;
    std::string username = "root";
    std::string password;
    std::string comment;
};

struct RemoteLogFile {
    std::string path;
    std::string name;
    std::int64_t size = 0;
    std::int64_t modified = 0;
};

struct SystemInfo {
    std::string memTotal;
    std::string memAvailable;
    std::string cores;
    std::string load;
    std::string temperature;
    std::string uptime;
    std::string build;
    std::vector<std::string> errors;
};

} // namespace fluxus
