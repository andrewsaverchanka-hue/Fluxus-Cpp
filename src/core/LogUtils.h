#pragma once

#include "Models.h"
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace fluxus {

std::string stripAnsi(const std::string& text);
std::optional<DateTime> extractDevicesDateTime(const std::string& line, std::optional<int> year = std::nullopt);
std::optional<DateTime> extractFrontendDateTime(const std::string& line);
std::string detectLogType(const std::string& path);
std::string safeFilename(const std::string& value, std::size_t maxLength = 64);
std::string truncate(const std::string& value, std::size_t length);

class LineReader {
public:
    explicit LineReader(const std::string& path);
    ~LineReader();
    LineReader(const LineReader&) = delete;
    LineReader& operator=(const LineReader&) = delete;
    bool readLine(std::string& line);
    bool isOpen() const { return open_; }
private:
    int readChunk(char* target, int size);
    std::ifstream file_;
    gzFile gzip_ = nullptr;
    std::vector<char> buffer_;
    std::size_t position_ = 0;
    std::size_t available_ = 0;
    bool open_ = false;
    bool eof_ = false;
};

} // namespace fluxus
