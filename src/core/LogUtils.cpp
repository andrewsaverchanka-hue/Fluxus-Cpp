#include "LogUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

namespace fluxus {
namespace {
bool digit(char value) { return value >= '0' && value <= '9'; }
bool pairAt(const std::string& value, std::size_t index, int& result) {
    if (index + 1 >= value.size() || !digit(value[index]) || !digit(value[index + 1])) return false;
    result = (value[index] - '0') * 10 + value[index + 1] - '0';
    return true;
}
bool validDate(const DateTime& value) {
    if (value.month < 1 || value.month > 12 || value.day < 1 || value.hour > 23 || value.minute > 59 || value.second > 59) return false;
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int maximum = lengths[value.month - 1];
    if (value.month == 2 && ((value.year % 4 == 0 && value.year % 100 != 0) || value.year % 400 == 0)) ++maximum;
    return value.day <= maximum;
}
std::string extensionLower(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}
}

std::string stripAnsi(const std::string& text) {
    if (text.find('\x1b') == std::string::npos) return text;
    std::string result; result.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size()) {
                const char c = text[i++];
                if (c >= '@' && c <= '~') break;
            }
        } else result.push_back(text[i++]);
    }
    return result;
}

std::optional<DateTime> extractDevicesDateTime(const std::string& line, std::optional<int> year) {
    if (line.size() < 14) return std::nullopt;
    for (std::size_t i = 0; i + 13 < line.size(); ++i) {
        if (!digit(line[i]) || !digit(line[i+1]) || line[i+2] != '/' || !digit(line[i+3]) || !digit(line[i+4])) continue;
        if (!std::isspace(static_cast<unsigned char>(line[i+5])) || line[i+8] != ':' || line[i+11] != ':') continue;
        int first, second, hour, minute, sec;
        if (!pairAt(line,i,first) || !pairAt(line,i+3,second) || !pairAt(line,i+6,hour) || !pairAt(line,i+9,minute) || !pairAt(line,i+12,sec)) continue;
        const int actualYear = year.value_or(DateTime::now().year);
        DateTime direct{actualYear,second,first,hour,minute,sec};
        if (validDate(direct)) return direct;
        DateTime fallback{actualYear,first,second,hour,minute,sec};
        if (validDate(fallback)) return fallback;
    }
    return std::nullopt;
}

std::optional<DateTime> extractFrontendDateTime(const std::string& line) {
    static const std::regex pattern(R"(\[(\d{1,2})/(\d{1,2})/(\d{4}),\s*(\d{1,2}):(\d{2}):(\d{2})\s*(AM|PM)\])");
    std::smatch match;
    if (!std::regex_search(line, match, pattern)) return std::nullopt;
    int hour = std::stoi(match[4]);
    const auto suffix = match[7].str();
    if (suffix == "PM" && hour != 12) hour += 12;
    if (suffix == "AM" && hour == 12) hour = 0;
    DateTime result{std::stoi(match[3]),std::stoi(match[1]),std::stoi(match[2]),hour,std::stoi(match[5]),std::stoi(match[6])};
    return validDate(result) ? std::optional<DateTime>(result) : std::nullopt;
}

LineReader::LineReader(const std::string& path) : buffer_(1024 * 1024) {
    if (extensionLower(path) == ".gz") {
        gzip_ = gzopen(path.c_str(), "rb");
        if (gzip_) { gzbuffer(gzip_, 1024 * 1024); open_ = true; }
    } else {
        file_.open(path, std::ios::binary);
        open_ = file_.is_open();
    }
}
LineReader::~LineReader() { if (gzip_) gzclose(gzip_); }
int LineReader::readChunk(char* target, int size) {
    if (gzip_) return gzread(gzip_, target, static_cast<unsigned>(size));
    file_.read(target, size);
    return static_cast<int>(file_.gcount());
}
bool LineReader::readLine(std::string& line) {
    line.clear();
    if (!open_ || eof_) return false;
    bool any = false;
    while (true) {
        if (position_ == available_) {
            const int count = readChunk(buffer_.data(), static_cast<int>(buffer_.size()));
            if (count <= 0) { eof_ = true; break; }
            position_ = 0; available_ = static_cast<std::size_t>(count);
        }
        const char c = buffer_[position_++]; any = true;
        if (c == '\n') break;
        line.push_back(c);
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return any || !line.empty();
}

std::string detectLogType(const std::string& path) {
    try {
        LineReader reader(path);
        if (!reader.isOpen()) return "unknown";
        std::string head, line;
        while (head.size() < 8192 && reader.readLine(line)) { head += line; head.push_back('\n'); }
        head = stripAnsi(head);
        static const std::regex frontend(R"(\[\d{1,2}/\d{1,2}/\d{4},\s*\d{1,2}:\d{2}:\d{2}\s*(AM|PM)\])");
        static const std::regex devices(R"(\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}\s+DBG)");
        if (std::regex_search(head, frontend)) return "frontend";
        if (std::regex_search(head, devices) || head.find("DevicesServer") != std::string::npos ||
            head.find("LCDM") != std::string::npos || head.find("serialPath=") != std::string::npos) return "devices";
    } catch (...) {}
    return "unknown";
}

std::string safeFilename(const std::string& value, std::size_t maxLength) {
    if (value.empty()) return "unknown";
    std::string result; result.reserve(value.size());
    bool underscore = false;
    for (unsigned char c : value) {
        const bool valid = std::isalnum(c) || c == '_' || c == '-' || c >= 128;
        if (valid) { result.push_back(static_cast<char>(c)); underscore = false; }
        else if (!underscore && !result.empty()) { result.push_back('_'); underscore = true; }
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    if (result.size() > maxLength) result.resize(maxLength);
    return result.empty() ? "unknown" : result;
}
std::string truncate(const std::string& value, std::size_t length) { return value.size() <= length ? value : value.substr(0, length); }

} // namespace fluxus
