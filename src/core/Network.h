#pragma once

#include "Models.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace fluxus {

class SshError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CommandResult {
    std::string output;
    std::string error;
    int exitCode = 0;
};

class LocalTunnel;
class SshSession;
class SftpSession;

class SshClient {
public:
    using LogCallback = std::function<void(const std::string&, const std::string&)>;
    SshClient(DeviceConnection device, std::string keyPath = "", std::string defaultPassword = "");
    ~SshClient();
    SshClient(const SshClient&) = delete;
    SshClient& operator=(const SshClient&) = delete;

    bool connect(LogCallback log = {});
    void disconnect();
    bool isConnected() const;
    CommandResult runCommand(const std::string& command, int timeoutSeconds = 60);
    std::shared_ptr<SshSession> openTargetSession();
    std::unique_ptr<SftpSession> openSftp();
    std::string streamLogFiltered(const RemoteLogFile& log, const std::string& patternKey = "");
    const DeviceConnection& device() const { return device_; }
private:
    DeviceConnection device_;
    std::string keyPath_;
    std::string defaultPassword_;
    std::unique_ptr<SshSession> gateway_;
    std::unique_ptr<LocalTunnel> tunnel_;
    std::shared_ptr<SshSession> target_;
    mutable std::mutex commandMutex_;
    LogCallback log_;
};

class SftpSession {
public:
    ~SftpSession();
    SftpSession(const SftpSession&) = delete;
    SftpSession& operator=(const SftpSession&) = delete;
    void download(const std::string& remotePath, const std::string& localPath,
                  std::int64_t expectedSize,
                  const std::function<void(std::int64_t)>& progress = {},
                  const std::atomic_bool* cancelled = nullptr);
private:
    struct Impl;
    explicit SftpSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class SshClient;
};

struct DownloadProgress {
    int completed = 0;
    int total = 0;
    std::string fileName;
    std::int64_t fileRead = 0;
    std::int64_t fileSize = 0;
};

class LogFetcher {
public:
    static const std::map<std::string, std::string>& logPatterns();
    static const std::map<std::string, std::string>& grepPatterns();
    static std::vector<RemoteLogFile> listLogs(SshClient& ssh,
        const std::map<std::string, std::string>& patterns = {});
    static std::vector<RemoteLogFile> filterByHours(const std::vector<RemoteLogFile>& logs, int hours);
    static std::vector<RemoteLogFile> filterByDates(const std::vector<RemoteLogFile>& logs,
        std::int64_t start, std::int64_t end);
    static std::vector<std::string> downloadLogs(SshClient& ssh,
        const std::vector<RemoteLogFile>& logs, const std::string& targetFolder,
        int maxParallel = 4,
        const std::function<void(const DownloadProgress&)>& progress = {},
        const std::atomic_bool* cancelled = nullptr);
    static std::string streamLogFiltered(SshClient& ssh, const RemoteLogFile& log,
        const std::string& patternKey = "");
};

class DeviceService {
public:
    static std::pair<std::string, std::string> identify(SshClient& ssh);
    static SystemInfo collectSystemInfo(SshClient& ssh);
};

} // namespace fluxus
