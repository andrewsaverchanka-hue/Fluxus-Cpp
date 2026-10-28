#pragma once

#include "Models.h"
#include <filesystem>
#include <mutex>

namespace fluxus {

class Config {
public:
    static Config& instance();
    void setDataDirectory(const std::filesystem::path& path);
    void load();
    void save();
    Json get(const std::string& key,const Json& fallback = Json()) const;
    void set(const std::string& key,const Json& value,bool saveNow = false);
    std::string string(const std::string& key,const std::string& fallback = "") const;
    bool boolean(const std::string& key,bool fallback = true) const;
    std::vector<DeviceConnection> devices() const;
    void setDevices(const std::vector<DeviceConnection>& devices,bool saveNow = true);
    std::filesystem::path dataDirectory() const { return dataDirectory_; }
    std::filesystem::path downloadedLogsDirectory() const { return dataDirectory_/"downloaded_logs"; }
private:
    Config() = default;
    mutable std::mutex mutex_;
    Json values_ = Json::object();
    std::filesystem::path dataDirectory_ = std::filesystem::current_path();
};

} // namespace fluxus
