#include "LimaAnalyzer.h"
#include "LogUtils.h"

#include <algorithm>
#include <regex>
#include <sstream>

namespace fluxus {
namespace {
const std::regex BanknoteNested(R"rx(banknote:\s*\{\s*banknote:\s*(\d+))rx");
const std::regex BanknotePlain(R"rx(banknote:\s*(\d+))rx");
const std::regex CurrencyEscaped(R"rx(currency:\s*\\?"(\w+)\\?")rx");
const std::regex CurrencyPlain(R"rx(currency:\s*(\w+))rx");
const std::regex UuidEscaped(R"rx(banknote_uuid:\s*\\?"([^\\"]+)\\?")rx");
const std::regex UuidPlain(R"rx(banknote_uuid:\s*"([^"]+)")rx");
const std::regex BanknoteId(R"rx(banknote_id:\s*(\d+))rx");
const std::regex ModelEscaped(R"rx(model:\s*\\?"([^\\"]+)\\?")rx");
const std::regex ModelPlain(R"rx(model:\s*"([^"]+)")rx");
const std::regex SerialEscaped(R"rx(serial_number:\s*\\?"([^\\"]+)\\?")rx");
const std::regex SerialPlain(R"rx(serial_number:\s*"([^"]+)")rx");
const std::regex DeviceId(R"rx(device:\s*\{\s*id:\s*\\?"([^\\"]+)\\?")rx");
const std::regex VendorEscaped(R"rx(vendor:\s*\\?"([^\\"]+)\\?")rx");
const std::regex VendorPlain(R"rx(vendor:\s*"([^"]+)")rx");
const std::regex EventType(R"rx(type:\s*(DEVICE_EVENT_TYPE_BILL_ACCEPTOR_\w+))rx");
const std::regex EventTypeFallback(R"rx(event=\s*(DEVICE_EVENT_TYPE_BILL_ACCEPTOR_\w+))rx");
const std::regex DescriptionEscaped(R"rx(description:\s*\\?"([^\\"]+)\\?")rx");
const std::regex DescriptionPlain(R"rx(description:\s*"([^"]+)")rx");
const std::regex Timestamp(R"rx(timestamp_ms:\s*(\d+))rx");
const std::regex NotesQuoted(R"rx(notes\s*=\s*"([^"]*)")rx");
const std::regex NotesPlain(R"rx(notes\s*=\s*([^\s,;]+))rx");
const std::regex NoteStats(R"rx(\$(\d+)\s*x\s*(\d+))rx");
const std::regex BnvQuoted(R"rx(event=\s*"([^"]+)")rx");
const std::regex BnvPlain(R"rx(event=\s*([^\s]+))rx");

const std::set<std::string> BnvNames{
    "Slave Reset","Note Rejecting","Note Stacked","Fraud Attempt","Stacker Full","Note Cleared To Cash Box",
    "Note Stacking","Safe Note Jam","Unsafe Note Jam","Disabled","Note Cleared From Front","Channel Disable",
    "Credit Note","Note Rejected","Initialising","Read Note"
};
const std::set<std::string> BnvErrors{"Safe Note Jam","Unsafe Note Jam","Fraud Attempt","Stacker Full"};

std::optional<std::string> group(const std::regex& pattern, const std::string& input) {
    std::smatch match;
    if (std::regex_search(input, match, pattern)) return match[1].str();
    return std::nullopt;
}
std::optional<std::string> firstGroup(const std::string& input, std::initializer_list<const std::regex*> patterns) {
    for (const auto* pattern : patterns) if (auto value = group(*pattern, input)) return value;
    return std::nullopt;
}
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
std::optional<int> banknoteRaw(const std::string& line) {
    auto value = group(BanknoteNested, line);
    if (!value) value = group(BanknotePlain, line);
    if (!value) return std::nullopt;
    const int raw = std::stoi(*value);
    static const std::set<int> known{100,500,1000,2000,5000,10000};
    if (!known.count(raw) && raw < 100) return std::nullopt;
    return raw;
}
void increment(DenomStats& stats, const std::string& currency, double denomination) { ++stats[currency][denomination]; }
bool finalType(const std::string& type) {
    const auto ends = [&type](const std::string& suffix) { return type.size() >= suffix.size() && type.compare(type.size()-suffix.size(),suffix.size(),suffix)==0; };
    return ends("_STACKED") || ends("_REJECTED") || ends("_CREDIT");
}
}

void LimaAnalyzer::processLine(const std::string& line) {
    const auto clean = stripAnsi(line);
    std::smatch noteMatch;
    const bool isCollection = clean.find("COLLECTION REPORT") != std::string::npos ||
        (clean.find("Print") != std::string::npos && clean.find("notes=") != std::string::npos &&
         clean.find('$') != std::string::npos && std::regex_search(clean, noteMatch, NoteStats));
    if (isCollection) if (const auto dt = extractDevicesDateTime(line)) processCollection(clean, *dt);

    if (line.find("DEVICE_EVENT_TYPE_BILL_ACCEPTOR_BANKNOTE_") != std::string::npos) {
        ++linesFound;
        auto type = group(EventType, line);
        if (!type) type = group(EventTypeFallback, line);
        if (!type) return;
        const auto raw = banknoteRaw(line);
        const auto currency = firstGroup(line, {&CurrencyEscaped,&CurrencyPlain});
        const auto uuid = firstGroup(line, {&UuidEscaped,&UuidPlain});
        const auto id = group(BanknoteId, line);
        auto model = firstGroup(line, {&ModelEscaped,&ModelPlain});
        auto serial = firstGroup(line, {&SerialEscaped,&SerialPlain,&DeviceId});
        auto vendor = firstGroup(line, {&VendorEscaped,&VendorPlain});
        if (model) *model = trim(*model); if (serial) *serial = trim(*serial); if (vendor) *vendor = trim(*vendor);
        if (serial && model) deviceModels[*serial] = {{"model",*model},{"vendor",vendor.value_or("?")}};
        const auto description = firstGroup(line, {&DescriptionEscaped,&DescriptionPlain}).value_or("");
        const auto dt = extractDevicesDateTime(line);
        const auto timestamp = group(Timestamp, line);
        Event event = {
            {"timestamp",timestamp ? std::stoll(*timestamp) : 0},
            {"date",dt ? Json(dt->date()) : Json(nullptr)}, {"time",dt ? Json(dt->time()) : Json(nullptr)},
            {"dt_obj",dt ? Json(dt->iso()) : Json(nullptr)}, {"event_type",*type},
            {"banknote_uuid",uuid ? Json(*uuid) : Json(nullptr)}, {"banknote_id",id.value_or("N/A")},
            {"banknote_raw",raw ? Json(*raw) : Json(nullptr)}, {"currency",currency ? Json(*currency) : Json(nullptr)},
            {"description",description}, {"device_model",model ? Json(*model) : Json(nullptr)},
            {"device_serial",serial ? Json(*serial) : Json(nullptr)}, {"device_vendor",vendor ? Json(*vendor) : Json(nullptr)},
            {"raw",truncate(line,250)}, {"is_bnv",false}
        };
        allEvents.push_back(event);
        if (type->find("STACKED") != std::string::npos) {
            if (raw && currency) {
                const double value = denominationValue(*raw);
                increment(stats,*currency,value);
                increment(statsByDevice[model.value_or("Unknown")][serial.value_or("N/A")],*currency,value);
                ++totalCount; totalSum += value; ++linesParsed;
            }
        }
        return;
    }
    if (auto bnv = processBnv(clean,line)) bnvEvents.push_back(std::move(*bnv));
}

void LimaAnalyzer::processCollection(const std::string& clean, const DateTime& dt) {
    auto text = group(NotesQuoted,clean);
    if (!text) text = group(NotesPlain,clean);
    std::map<int,int> notes;
    if (text) {
        for (std::sregex_iterator it(text->begin(),text->end(),NoteStats), end; it != end; ++it)
            notes[std::stoi((*it)[1])] = std::stoi((*it)[2]);
    }
    if (notes.empty()) return;
    std::ostringstream key; key << dt.unix();
    for (const auto& [denomination,count] : notes) key << ':' << denomination << '=' << count;
    if (collectionKeys_.insert(key.str()).second)
        collections.push_back({dt,dt.date(),dt.time(),notes,truncate(clean,300)});
}

std::optional<Event> LimaAnalyzer::processBnv(const std::string& clean, const std::string& original) {
    std::optional<std::string> validator;
    if (clean.find("BanknoteValidatorBV") != std::string::npos) validator = "BV20/30/50";
    else if (clean.find("BanknoteValidator") != std::string::npos) validator = "NV9/other";
    if (!validator) return std::nullopt;
    auto name = group(BnvQuoted,clean); if (!name) name = group(BnvPlain,clean);
    if (!name || !BnvNames.count(*name)) return std::nullopt;
    const auto dt = extractDevicesDateTime(original);
    const auto ts = group(Timestamp,original);
    const std::int64_t timestamp = ts ? std::stoll(*ts) : dt ? dt->unix() : 0;
    ++bnvEventsCount[*name]; ++bnvEventsByType[*validator][*name];
    return Event{{"timestamp",timestamp},{"date",dt ? Json(dt->date()) : Json(nullptr)},
        {"time",dt ? Json(dt->time()) : Json(nullptr)},{"dt_obj",dt ? Json(dt->iso()) : Json(nullptr)},
        {"event_type","BNV_"+*name},{"event_name",*name},{"validator_type",*validator},
        {"banknote_uuid",nullptr},{"banknote_id","N/A"},{"banknote_raw",nullptr},{"currency",nullptr},
        {"description",*name},{"raw",truncate(clean,200)},{"is_bnv",true}};
}

void LimaAnalyzer::buildSessions() {
    std::vector<std::pair<int,const Event*>> merged;
    merged.reserve(allEvents.size()+bnvEvents.size());
    for (const auto& event : allEvents) merged.push_back({0,&event});
    for (const auto& event : bnvEvents) merged.push_back({1,&event});
    std::stable_sort(merged.begin(),merged.end(),[](const auto& a,const auto& b) {
        return eventInt(*a.second,"timestamp") < eventInt(*b.second,"timestamp");
    });
    sessions.clear();
    std::map<std::string,BanknoteSession> current;
    for (const auto& [kind,eventPtr] : merged) {
        const auto& event = *eventPtr;
        if (kind == 1) {
            const auto validator = eventString(event,"validator_type");
            const auto name = eventString(event,"event_name");
            for (auto& [serial,session] : current) {
                if (session.closed) continue;
                const bool modelMatch = (validator == "BV20/30/50" && session.deviceModel.find("BV") != std::string::npos) ||
                    (validator == "NV9/other" && session.deviceModel.find("BV") == std::string::npos) || validator.empty();
                if (!modelMatch || !BnvErrors.count(name)) continue;
                session.markError(name);
                session.addEvent({{"timestamp",eventInt(event,"timestamp")},{"date",event.contains("date")?event["date"]:Json(nullptr)},
                    {"time",event.contains("time")?event["time"]:Json(nullptr)},{"event_type",eventString(event,"event_type")},
                    {"banknote_uuid",nullptr},{"banknote_id","N/A"},{"banknote_raw",nullptr},{"currency",nullptr},
                    {"description","[BNV ERROR] "+name},{"is_bnv",true}});
            }
            continue;
        }
        const auto type = eventString(event,"event_type");
        const auto uuid = eventString(event,"banknote_uuid");
        const auto serial = eventString(event,"device_serial","N/A");
        auto existing = current.find(serial);
        if (type.find("INSERTED") != std::string::npos && !uuid.empty()) {
            if (existing != current.end() && !existing->second.closed && existing->second.uuid == uuid) {
                existing->second.addEvent(event); continue;
            }
            if (existing != current.end()) sessions.push_back(existing->second);
            current[serial] = BanknoteSession(static_cast<int>(sessions.size()),event);
            continue;
        }
        if (existing != current.end()) {
            existing->second.addEvent(event);
            if (finalType(type)) { sessions.push_back(existing->second); current.erase(existing); }
        } else {
            BanknoteSession created(static_cast<int>(sessions.size()),event);
            if (finalType(type)) sessions.push_back(std::move(created)); else current[serial] = std::move(created);
        }
    }
    for (auto& [serial,session] : current) sessions.push_back(std::move(session));
    for (std::size_t i=0;i<sessions.size();++i) sessions[i].sessionId=static_cast<int>(i);
}

LimaAnalyzer::BetweenCollections LimaAnalyzer::betweenCollections() const {
    BetweenCollections result;
    if (collections.empty()) return result;
    auto sorted = collections;
    std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.dateTime<b.dateTime;});
    result.first=sorted.front(); result.last=sorted.back();
    DenomStats stats;
    for (const auto& event : allEvents) {
        if (eventString(event,"event_type").find("STACKED") == std::string::npos) continue;
        const auto dt=eventDate(event,"dt_obj");
        if (!dt || *dt<=result.first->dateTime || *dt>result.last->dateTime || !eventHas(event,"banknote_raw") || !eventHas(event,"currency")) continue;
        const double value=denominationValue(static_cast<int>(eventInt(event,"banknote_raw")));
        increment(stats,eventString(event,"currency"),value); ++result.count; result.sum+=value;
    }
    result.stats=std::move(stats); return result;
}

} // namespace fluxus
