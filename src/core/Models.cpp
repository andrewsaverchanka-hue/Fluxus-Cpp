#include "Models.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace fluxus {
namespace {

constexpr std::int64_t daysFromCivil(int y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<int>(doe) - 719468;
}

std::string two(int value) {
    char buffer[3];
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return buffer;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool contains(const std::string& value, const std::string& part) {
    return value.find(part) != std::string::npos;
}

} // namespace

std::int64_t DateTime::unix() const {
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 + minute * 60 + second;
}
std::string DateTime::date() const { return two(day) + "/" + two(month) + "/" + std::to_string(year); }
std::string DateTime::time() const { return two(hour) + ":" + two(minute) + ":" + two(second); }
std::string DateTime::iso() const { return std::to_string(year) + "-" + two(month) + "-" + two(day) + "T" + time(); }

std::optional<DateTime> DateTime::fromIso(const std::string& value) {
    DateTime result;
    char t = 0;
    if (std::sscanf(value.c_str(), "%d-%d-%d%c%d:%d:%d", &result.year, &result.month, &result.day, &t, &result.hour, &result.minute, &result.second) == 7)
        return result;
    return std::nullopt;
}

DateTime DateTime::now() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
}

std::optional<DateTime> eventDate(const Event& event, const std::string& key) {
    if (!event.is_object() || !event.contains(key) || !event[key].is_string()) return std::nullopt;
    return DateTime::fromIso(event[key].get<std::string>());
}
std::string eventString(const Event& event, const std::string& key, const std::string& fallback) {
    if (!event.is_object() || !event.contains(key) || event[key].is_null()) return fallback;
    if (event[key].is_string()) return event[key].get<std::string>();
    if (event[key].is_number_integer()) return std::to_string(event[key].get<std::int64_t>());
    if (event[key].is_boolean()) return event[key].get<bool>() ? "true" : "false";
    return fallback;
}
std::int64_t eventInt(const Event& event, const std::string& key, std::int64_t fallback) {
    if (!event.is_object() || !event.contains(key) || event[key].is_null()) return fallback;
    if (event[key].is_number_integer()) return event[key].get<std::int64_t>();
    if (event[key].is_number_unsigned()) return static_cast<std::int64_t>(event[key].get<std::uint64_t>());
    if (event[key].is_number_float()) return static_cast<std::int64_t>(event[key].get<double>());
    if (event[key].is_string()) {
        try { return std::stoll(event[key].get<std::string>()); } catch (...) { return fallback; }
    }
    return fallback;
}
bool eventBool(const Event& event, const std::string& key, bool fallback) {
    if (!event.is_object() || !event.contains(key) || event[key].is_null()) return fallback;
    if (event[key].is_boolean()) return event[key].get<bool>();
    if (event[key].is_number()) return eventInt(event, key) != 0;
    if (event[key].is_string()) return lower(eventString(event, key)) == "true";
    return fallback;
}
bool eventHas(const Event& event, const std::string& key) {
    return event.is_object() && event.contains(key) && !event[key].is_null();
}
Event makeEvent(std::initializer_list<Json::value_type> values) { return Event(values); }

int denominationValue(int raw) {
    static const std::unordered_map<int, int> values{{100,1},{500,5},{1000,10},{2000,20},{5000,50},{10000,100}};
    const auto it = values.find(raw);
    return it == values.end() ? raw : it->second;
}

std::string classifyDispenseError(const std::string& detail) {
    const auto value = lower(detail);
    if (contains(value, "jam")) return "FAILED_JAM";
    if (contains(value, "bill-end") || contains(value, "bill end") || contains(value, "empty")) return "FAILED_EMPTY";
    if (contains(value, "timeout")) return "FAILED_TIMEOUT";
    return "FAILED_HARDWARE";
}

std::pair<std::string, std::string> classifyLcdmStatus(int code) {
    static const std::unordered_map<int, std::pair<std::string, std::string>> codes{
        {0x30,{"Good","OK"}},{0x31,{"Normal stop","OK"}},{0x32,{"Pickup error","FAILED_HARDWARE"}},
        {0x33,{"JAM at CHK1,2 Sensor","FAILED_JAM"}},{0x34,{"Overflow bill","FAILED_HARDWARE"}},
        {0x35,{"JAM at EXIT or EJT Sensor","FAILED_JAM"}},{0x36,{"JAM at DIV Sensor","FAILED_JAM"}},
        {0x37,{"Undefined command","FAILED_HARDWARE"}},{0x38,{"Upper Bill-End","FAILED_EMPTY"}},
        {0x3A,{"Counting Error (CHK3,4 - DIV)","FAILED_HARDWARE"}},{0x3B,{"Note request error","FAILED_HARDWARE"}},
        {0x3C,{"Counting Error (DIV - EJT)","FAILED_HARDWARE"}},{0x3D,{"Counting Error (EJT - EXIT)","FAILED_HARDWARE"}},
        {0x3F,{"Reject Tray not recognized","FAILED_HARDWARE"}},{0x40,{"Lower Bill-End","FAILED_EMPTY"}},
        {0x41,{"Motor Stop","FAILED_HARDWARE"}},{0x42,{"JAM at DIV Sensor","FAILED_JAM"}},
        {0x43,{"Timeout (DIV -> EJT)","FAILED_TIMEOUT"}},{0x44,{"Over Reject","FAILED_HARDWARE"}},
        {0x45,{"Upper Cassette not recognized","FAILED_HARDWARE"}},{0x46,{"Lower Cassette not recognized","FAILED_HARDWARE"}},
        {0x47,{"Dispensing timeout","FAILED_TIMEOUT"}},{0x48,{"JAM at EJT Sensor","FAILED_JAM"}},
        {0x49,{"Diverter solenoid error","FAILED_HARDWARE"}},{0x4A,{"SOL Sensor error","FAILED_HARDWARE"}},
        {0x4C,{"JAM at CHK3,4 Sensor","FAILED_JAM"}},{0x4E,{"Purge error (Jam at Div)","FAILED_JAM"}}
    };
    const auto it = codes.find(code);
    if (it != codes.end()) return it->second;
    char buffer[32]; std::snprintf(buffer, sizeof(buffer), "Unknown 0x%02X", code);
    return {buffer, "FAILED_HARDWARE"};
}

std::pair<std::string, std::string> classifyLcdm4000(int code, const std::string& description) {
    if (code == 0) return {"No error", "OK"};
    if (code == 1) return {"Bill Pick Up Error", "FAILED_HARDWARE"};
    if (!description.empty()) return {description, classifyDispenseError(description)};
    return {"Unknown code " + std::to_string(code), "FAILED_HARDWARE"};
}

std::string w2StatusDisplay(const std::string& status) {
    static const std::unordered_map<std::string, std::string> names{
        {"OK","✅ ВЫДАНО"},{"PARTIAL","⚠️ ЧАСТИЧНАЯ"},{"FAILED_JAM","⛔ ОШИБКА JAM"},
        {"FAILED_EMPTY","⛔ ОШИБКА EMPTY"},{"FAILED_TIMEOUT","⛔ ОШИБКА TIMEOUT"},
        {"FAILED_HARDWARE","⛔ ОШИБКА HARDWARE"},{"FAILED_COUPON","❌ КУПОН ОТКЛОНЁН"},
        {"INCOMPLETE","⏳ НЕЗАВЕРШЕНА"}
    };
    const auto it = names.find(status);
    return it == names.end() ? status : it->second;
}

BanknoteSession::BanknoteSession(int id, const Event& first) : sessionId(id), events{first} {
    startTimestamp = eventInt(first, "timestamp");
    firstDate = eventString(first, "date", "?");
    firstTime = eventString(first, "time", "?");
    uuid = eventString(first, "banknote_uuid", eventString(first, "banknote_id", "N/A"));
    if (eventHas(first, "banknote_raw")) denomination = static_cast<int>(eventInt(first, "banknote_raw"));
    if (eventHas(first, "currency")) currency = eventString(first, "currency");
    deviceModel = eventString(first, "device_model", "Unknown");
    deviceSerial = eventString(first, "device_serial", "N/A");
    updateStatus(first);
}
void BanknoteSession::addEvent(const Event& event) {
    events.push_back(event);
    if (!denomination && eventHas(event, "banknote_raw")) denomination = static_cast<int>(eventInt(event, "banknote_raw"));
    if (!currency && eventHas(event, "currency")) currency = eventString(event, "currency");
    if (deviceModel == "Unknown") deviceModel = eventString(event, "device_model", deviceModel);
    if (deviceSerial == "N/A") deviceSerial = eventString(event, "device_serial", deviceSerial);
    updateStatus(event);
}
void BanknoteSession::updateStatus(const Event& event) {
    if (eventBool(event, "is_bnv")) return;
    const auto type = eventString(event, "event_type");
    if (contains(type, "STACKED") || contains(type, "CREDIT")) { status = "STACKED"; closed = true; }
    else if (contains(type, "REJECTED")) { status = "REJECTED"; closed = true; }
    else if (status != "ERROR") {
        if (contains(type, "STACKING")) status = "STACKING";
        else if (contains(type, "INSERTED") && status == "INCOMPLETE") status = "INSERTED";
    }
}
void BanknoteSession::markError(const std::string& reason) {
    if (closed) return;
    status = "ERROR"; errorReason = reason;
}
double BanknoteSession::displayDenomination() const { return denomination ? denominationValue(*denomination) : 0; }

W2CashEvent::W2CashEvent(std::string type, std::optional<DateTime> dt) : eventType(std::move(type)), dateTime(dt) {
    timestamp = dt ? static_cast<double>(dt->unix()) : 0;
}
std::string W2CashEvent::dateString() const { return dateTime ? dateTime->date() : "?"; }
std::string W2CashEvent::timeString() const { return dateTime ? dateTime->time() : "?"; }
std::string W2CashEvent::typeDisplay() const {
    if (eventType == Collection) return "Инкассация";
    if (eventType == Withdraw) return "Изъятие (Withdraw)";
    if (eventType == CashIn) return "Пополнение (Cash in)";
    return eventType;
}
bool W2CashEvent::isOutflow() const { return eventType == Collection || eventType == Withdraw; }
bool W2CashEvent::isInflow() const { return eventType == CashIn; }

void W2LowLevelSession::addEvent(const std::string& kind, const DateTime& dt, const std::string& raw, const Event& payload) {
    Event event = {{"kind",kind},{"dt",dt.iso()},{"raw",raw}};
    if (payload.is_object()) event.update(payload);
    events.push_back(std::move(event));
    if (!startDateTime || dt < *startDateTime) { startDateTime = dt; startDateString = dt.date(); startTimeString = dt.time(); }
    if (!endDateTime || dt > *endDateTime) endDateTime = dt;
}
int W2LowLevelSession::totalRequested() const {
    int result = 0; for (const auto& command : commands) result += static_cast<int>(eventInt(command, "amount")); return result;
}
int W2LowLevelSession::totalDispensed() const {
    int result = 0; for (const auto& item : results) result += static_cast<int>(eventInt(item, "dispensed")); return result;
}
int W2LowLevelSession::totalRejected() const {
    int result = 0; for (const auto& item : results) result += static_cast<int>(eventInt(item, "rejected")); return result;
}
void W2LowLevelSession::finalizeStatus() {
    if (results.empty()) { if (!commands.empty()) status = "INCOMPLETE"; return; }
    for (const auto& result : results) {
        const auto code = eventInt(result, "status_code", -1);
        const auto error = eventString(result, "error");
        if (error.empty() && (code < 0 || code == 0x30 || code == 0x31)) continue;
        if (code >= 0) {
            const auto info = classifyLcdmStatus(static_cast<int>(code));
            status = info.second; errorDetail = info.first; lcdmStatusCode = static_cast<int>(code);
        } else {
            errorDetail = error; status = classifyDispenseError(error);
            if (eventHas(result, "device_code")) lcdmDeviceCode = static_cast<int>(eventInt(result, "device_code"));
        }
        return;
    }
    const auto requested = totalRequested(); const auto dispensed = totalDispensed();
    if (requested > 0 && dispensed >= requested) status = "OK";
    else if (dispensed > 0) status = "PARTIAL";
    else status = "INCOMPLETE";
}
double W2LowLevelSession::durationSeconds() const {
    return startDateTime && endDateTime ? static_cast<double>(endDateTime->unix() - startDateTime->unix()) : 0;
}

void W2WithdrawSession::addEvent(const Event& event) {
    events.push_back(event);
    const auto dt = eventDate(event, "dt");
    if (!dt) return;
    if (!startDateTime || *dt < *startDateTime) {
        startDateTime = dt; startDateString = eventString(event, "date", dt->date()); startTimeString = eventString(event, "time", dt->time());
    }
    if (!endDateTime || *dt > *endDateTime) endDateTime = dt;
}
void W2WithdrawSession::finalizeStatus() {
    if (!payoutCouponStatus.empty() && payoutCouponStatus != "ok") { status = "FAILED_COUPON"; return; }
    if (dispensedAmount > 0 && dispensedAmount >= requestedAmount) { status = "OK"; return; }
    if (dispensedAmount > 0 && dispensedAmount < requestedAmount) { status = "PARTIAL"; return; }
    for (const auto& event : dispensedEvents) {
        const auto detail = eventString(event, "statusDetail");
        if (!detail.empty()) { status = classifyDispenseError(detail); errorDetail = detail; return; }
    }
    for (const auto& event : events) {
        const auto kind = eventString(event, "kind");
        if (kind == "cannotContinue" || kind == "errorOccurs") { status = "FAILED_HARDWARE"; return; }
    }
    status = "INCOMPLETE";
}
double W2WithdrawSession::durationSeconds() const {
    return startDateTime && endDateTime ? static_cast<double>(endDateTime->unix() - startDateTime->unix()) : 0;
}
bool W2WithdrawSession::hasSuspiciousMatch() const {
    return std::any_of(suspicionFlags.begin(), suspicionFlags.end(), [](const auto& flags) { return !flags.empty(); });
}
int W2WithdrawSession::suspiciousCount() const {
    return static_cast<int>(std::count_if(suspicionFlags.begin(), suspicionFlags.end(), [](const auto& flags) { return !flags.empty(); }));
}
std::vector<std::string> W2WithdrawSession::allSuspicionFlags() const {
    std::vector<std::string> result;
    for (const auto& flags : suspicionFlags) result.insert(result.end(), flags.begin(), flags.end());
    return result;
}

} // namespace fluxus
