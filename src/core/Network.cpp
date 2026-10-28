#include "Network.h"

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using Socket = SOCKET;
constexpr Socket InvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket InvalidSocket = -1;
#endif

namespace fluxus {
namespace fs = std::filesystem;
namespace {

constexpr const char* GatewayHost = "45.82.66.80";
constexpr int GatewayPort = 22;
constexpr const char* GatewayUser = "support";
constexpr const char* LimaKeyPath = "/opt/sftw/build/BACKSERVER/mc3_key.txt";
constexpr const char* W2ConfigPath = "/root/configuration.yml";

void closeSocket(Socket socket) {
    if (socket == InvalidSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string shellQuote(const std::string& value) {
    std::string result = "'";
    for (char ch : value) {
        if (ch == '\'') result += "'\\''";
        else result += ch;
    }
    return result + "'";
}

void checkKnownHost(ssh_session session) {
    const auto state = ssh_session_is_known_server(session);
    if (state == SSH_KNOWN_HOSTS_OK || state == SSH_KNOWN_HOSTS_NOT_FOUND ||
        state == SSH_KNOWN_HOSTS_UNKNOWN) {
        if (state != SSH_KNOWN_HOSTS_OK) {
            if (ssh_session_update_known_hosts(session) != SSH_OK)
                throw SshError("Не удалось сохранить SSH-ключ сервера");
        }
        return;
    }
    throw SshError("SSH-ключ сервера изменился или не прошёл проверку");
}

} // namespace

class SshSession {
public:
    ssh_session session = nullptr;
    ~SshSession() { disconnect(); }

    void connect(const std::string& host, int port, const std::string& user,
                 const std::string& keyPath, const std::string& password,
                 bool allowAgent = true) {
        session = ssh_new();
        if (!session) throw SshError("Не удалось создать SSH-сессию");
        ssh_options_set(session, SSH_OPTIONS_HOST, host.c_str());
        ssh_options_set(session, SSH_OPTIONS_PORT, &port);
        ssh_options_set(session, SSH_OPTIONS_USER, user.c_str());
        long timeout = 15;
        ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
        if (!keyPath.empty()) ssh_options_set(session, SSH_OPTIONS_IDENTITY, keyPath.c_str());
        if (ssh_connect(session) != SSH_OK) fail("SSH-подключение");
        try { checkKnownHost(session); }
        catch (...) { disconnect(); throw; }
        int rc = SSH_AUTH_DENIED;
        if ((!keyPath.empty() || allowAgent) && ssh_userauth_publickey_auto(session, nullptr, nullptr) == SSH_AUTH_SUCCESS)
            rc = SSH_AUTH_SUCCESS;
        if (rc != SSH_AUTH_SUCCESS && !password.empty())
            rc = ssh_userauth_password(session, nullptr, password.c_str());
        if (rc != SSH_AUTH_SUCCESS) fail("SSH-аутентификация");
    }

    CommandResult run(const std::string& command, int timeoutSeconds) {
        if (!session || !ssh_is_connected(session)) throw SshError("Нет подключения к устройству");
        ssh_channel channel = ssh_channel_new(session);
        if (!channel) fail("Создание SSH-канала");
        if (ssh_channel_open_session(channel) != SSH_OK || ssh_channel_request_exec(channel, command.c_str()) != SSH_OK) {
            ssh_channel_free(channel);
            fail("Выполнение SSH-команды");
        }
        CommandResult result;
        std::array<char, 32768> buffer{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        while (!ssh_channel_is_eof(channel)) {
            if (std::chrono::steady_clock::now() > deadline) {
                ssh_channel_close(channel);
                ssh_channel_free(channel);
                throw SshError("Тайм-аут команды (" + std::to_string(timeoutSeconds) + " с): " + command);
            }
            int count = ssh_channel_read_timeout(channel, buffer.data(), buffer.size(), 0, 200);
            if (count > 0) result.output.append(buffer.data(), static_cast<std::size_t>(count));
            int errorCount = ssh_channel_read_timeout(channel, buffer.data(), buffer.size(), 1, 0);
            if (errorCount > 0) result.error.append(buffer.data(), static_cast<std::size_t>(errorCount));
            if (count == SSH_ERROR || errorCount == SSH_ERROR) break;
        }
        result.exitCode = ssh_channel_get_exit_status(channel);
        result.output = trim(result.output);
        result.error = trim(result.error);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        return result;
    }

    void disconnect() {
        if (!session) return;
        if (ssh_is_connected(session)) ssh_disconnect(session);
        ssh_free(session);
        session = nullptr;
    }
private:
    [[noreturn]] void fail(const std::string& stage) {
        const std::string message = ssh_get_error(session);
        disconnect();
        throw SshError(stage + ": " + message);
    }
};

class LocalTunnel {
public:
    LocalTunnel(ssh_session gateway, std::string remoteHost, int remotePort)
        : gateway_(gateway), remoteHost_(std::move(remoteHost)), remotePort_(remotePort) {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw SshError("Не удалось инициализировать сеть");
        winsockStarted_ = true;
#endif
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ == InvalidSocket) throw SshError("Не удалось создать локальный SSH-туннель");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        int reuse = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(listener_, 16) != 0) throw SshError("Не удалось запустить локальный SSH-туннель");
        socklen_t length = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0)
            throw SshError("Не удалось определить порт SSH-туннеля");
        port_ = ntohs(address.sin_port);
        acceptor_ = std::thread([this] { acceptLoop(); });
    }

    ~LocalTunnel() {
        stopping_ = true;
        if (listener_ != InvalidSocket) ::shutdown(listener_, SHUT_RDWR);
        closeSocket(listener_);
        listener_ = InvalidSocket;
        if (acceptor_.joinable()) acceptor_.join();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
#ifdef _WIN32
        if (winsockStarted_) WSACleanup();
#endif
    }
    int port() const { return port_; }
private:
    void acceptLoop() {
        while (!stopping_) {
            Socket client = ::accept(listener_, nullptr, nullptr);
            if (client == InvalidSocket) break;
            workers_.emplace_back([this, client] { forward(client); });
        }
    }

    void forward(Socket client) {
        ssh_channel channel = ssh_channel_new(gateway_);
        if (!channel || ssh_channel_open_forward(channel, remoteHost_.c_str(), remotePort_, "127.0.0.1", port_) != SSH_OK) {
            if (channel) ssh_channel_free(channel);
            closeSocket(client);
            return;
        }
        std::array<char, 32768> buffer{};
        while (!stopping_) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(client, &readSet);
            timeval timeout{0, 100000};
            const int ready = ::select(static_cast<int>(client + 1), &readSet, nullptr, nullptr, &timeout);
            if (ready > 0 && FD_ISSET(client, &readSet)) {
                const int count = ::recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (count <= 0) break;
                int written = 0;
                while (written < count) {
                    const int rc = ssh_channel_write(channel, buffer.data() + written, static_cast<std::uint32_t>(count - written));
                    if (rc <= 0) { written = -1; break; }
                    written += rc;
                }
                if (written < 0) break;
            }
            int count = 0;
            while ((count = ssh_channel_read_nonblocking(channel, buffer.data(), buffer.size(), 0)) > 0) {
                int sent = 0;
                while (sent < count) {
                    const int rc = ::send(client, buffer.data() + sent, count - sent, 0);
                    if (rc <= 0) { sent = -1; break; }
                    sent += rc;
                }
                if (sent < 0) { stopping_ = false; count = -1; break; }
            }
            if (count < 0) break;
            if (ssh_channel_is_eof(channel) && !FD_ISSET(client, &readSet)) break;
        }
        ssh_channel_send_eof(channel);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        closeSocket(client);
    }

    ssh_session gateway_;
    std::string remoteHost_;
    int remotePort_;
    int port_ = 0;
    Socket listener_ = InvalidSocket;
    std::atomic_bool stopping_ = false;
    std::thread acceptor_;
    std::vector<std::thread> workers_;
    bool winsockStarted_ = false;
};

struct SftpSession::Impl {
    std::shared_ptr<SshSession> connection;
    sftp_session sftp = nullptr;
    ~Impl() { if (sftp) sftp_free(sftp); }
};

SftpSession::SftpSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SftpSession::~SftpSession() = default;

void SftpSession::download(const std::string& remotePath, const std::string& localPath,
                           std::int64_t expectedSize,
                           const std::function<void(std::int64_t)>& progress,
                           const std::atomic_bool* cancelled) {
    sftp_file input = sftp_open(impl_->sftp, remotePath.c_str(), O_RDONLY, 0);
    if (!input) throw SshError("SFTP: не удалось открыть " + remotePath + ": " + ssh_get_error(impl_->connection->session));
    std::ofstream output(localPath, std::ios::binary | std::ios::trunc);
    if (!output) { sftp_close(input); throw SshError("Не удалось создать файл " + localPath); }
    std::array<char, 262144> buffer{};
    std::int64_t total = 0;
    try {
        while (true) {
            if (cancelled && cancelled->load()) throw SshError("Загрузка отменена");
            const int count = sftp_read(input, buffer.data(), buffer.size());
            if (count < 0) throw SshError("SFTP: ошибка чтения " + remotePath);
            if (count == 0) break;
            output.write(buffer.data(), count);
            if (!output) throw SshError("Ошибка записи файла " + localPath);
            total += count;
            if (progress) progress(total);
        }
        output.close();
        sftp_close(input);
        input = nullptr;
        if (expectedSize >= 0 && total != expectedSize)
            throw SshError("SFTP: размер файла не совпадает (" + std::to_string(total) + " / " + std::to_string(expectedSize) + ")");
    } catch (...) {
        output.close();
        if (input) sftp_close(input);
        throw;
    }
}

SshClient::SshClient(DeviceConnection device, std::string keyPath, std::string defaultPassword)
    : device_(std::move(device)), keyPath_(std::move(keyPath)), defaultPassword_(std::move(defaultPassword)) {}
SshClient::~SshClient() { disconnect(); }

bool SshClient::connect(LogCallback log) {
    disconnect();
    log_ = std::move(log);
    try {
        if (log_) log_("info", "Подключение к SSH-шлюзу…");
        gateway_ = std::make_unique<SshSession>();
        gateway_->connect(GatewayHost, GatewayPort, GatewayUser, keyPath_, "");
        tunnel_ = std::make_unique<LocalTunnel>(gateway_->session, device_.host, device_.port);
        const std::string password = device_.password.empty() ? defaultPassword_ : device_.password;
        target_ = openTargetSession();
        if (log_) log_("info", "✓ Устройство " + device_.host + " подключено");
        return true;
    } catch (const std::exception& error) {
        if (log_) log_("error", std::string("Ошибка SSH: ") + error.what());
        disconnect();
        return false;
    }
}

std::shared_ptr<SshSession> SshClient::openTargetSession() {
    if (!tunnel_) throw SshError("SSH-туннель не запущен");
    const std::string password = device_.password.empty() ? defaultPassword_ : device_.password;
    auto target = std::make_shared<SshSession>();
    target->connect("127.0.0.1", tunnel_->port(), device_.username, keyPath_, password);
    return target;
}

CommandResult SshClient::runCommand(const std::string& command, int timeoutSeconds) {
    std::lock_guard lock(commandMutex_);
    if (!target_) throw SshError("Нет подключения к устройству");
    return target_->run(command, timeoutSeconds);
}

std::unique_ptr<SftpSession> SshClient::openSftp() {
    auto impl = std::make_unique<SftpSession::Impl>();
    impl->connection = openTargetSession();
    impl->sftp = sftp_new(impl->connection->session);
    if (!impl->sftp || sftp_init(impl->sftp) != SSH_OK)
        throw SshError("Не удалось открыть SFTP: " + std::string(ssh_get_error(impl->connection->session)));
    return std::unique_ptr<SftpSession>(new SftpSession(std::move(impl)));
}

bool SshClient::isConnected() const {
    return target_ && target_->session && ssh_is_connected(target_->session);
}

void SshClient::disconnect() {
    if (target_) target_->disconnect();
    target_.reset();
    tunnel_.reset();
    if (gateway_) gateway_->disconnect();
    gateway_.reset();
}

std::string SshClient::streamLogFiltered(const RemoteLogFile& log, const std::string& patternKey) {
    return LogFetcher::streamLogFiltered(*this, log, patternKey);
}

const std::map<std::string, std::string>& LogFetcher::logPatterns() {
    static const std::map<std::string, std::string> patterns = {
        {"devices_server", "/var/log/devices_server.log*"},
        {"frontend", "/var/log/skills-battle-frontend.log*"}
    };
    return patterns;
}

const std::map<std::string, std::string>& LogFetcher::grepPatterns() {
    static const std::map<std::string, std::string> patterns = {
        {"devices", "DEVICE_EVENT_TYPE_BILL_ACCEPTOR_BANKNOTE_|BanknoteValidator|COLLECTION REPORT|Print.*notes=|Handle Dispense Request:|Dispense worker:|Dispense: |CommandDispense:|LCDM4000 CCTalk:|Request: Print|Withdraw report|Cash in|takenFromDispenserBoxes|takenFromRejectedBox|addedToDispenserBoxes|outByNotes|inByDatesAndNotes"},
        {"frontend", "payoutCoupon|INSERT INTO terminal_|UPDATE terminal_|DispenserService.dispense|processNewEvent|cannotContinue|Error occurs while withdrawing|createCoupon|printSlip|Complete transaction|Redirect to HOME|COLLECTION REPORT|Withdraw report|Cash in|takenFromDispenserBoxes|takenFromRejectedBox|addedToDispenserBoxes|outByNotes|inByDatesAndNotes"}
    };
    return patterns;
}

std::vector<RemoteLogFile> LogFetcher::listLogs(SshClient& ssh, const std::map<std::string, std::string>& patterns) {
    const auto& selected = patterns.empty() ? logPatterns() : patterns;
    std::vector<RemoteLogFile> result;
    for (const auto& [key, pattern] : selected) {
        (void)key;
        const auto command = ssh.runCommand("sh -c 'for f in " + pattern + "; do [ -e \"$f\" ] && stat -c \"%s %Y %n\" \"$f\"; done' 2>/dev/null", 15);
        std::istringstream lines(command.output);
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream parts(line);
            RemoteLogFile file;
            if (!(parts >> file.size >> file.modified >> file.path)) continue;
            file.name = fs::path(file.path).filename().string();
            result.push_back(std::move(file));
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    return result;
}

std::vector<RemoteLogFile> LogFetcher::filterByHours(const std::vector<RemoteLogFile>& logs, int hours) {
    const auto cutoff = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() - std::chrono::hours(hours));
    std::vector<RemoteLogFile> result;
    std::copy_if(logs.begin(), logs.end(), std::back_inserter(result), [cutoff](const auto& log) { return log.modified >= cutoff; });
    return result;
}

std::vector<RemoteLogFile> LogFetcher::filterByDates(const std::vector<RemoteLogFile>& logs, std::int64_t start, std::int64_t end) {
    std::vector<RemoteLogFile> result;
    std::copy_if(logs.begin(), logs.end(), std::back_inserter(result), [start, end](const auto& log) { return log.modified >= start && log.modified <= end; });
    return result;
}

std::vector<std::string> LogFetcher::downloadLogs(SshClient& ssh, const std::vector<RemoteLogFile>& logs,
        const std::string& targetFolder, int maxParallel,
        const std::function<void(const DownloadProgress&)>& progress, const std::atomic_bool* cancelled) {
    fs::create_directories(targetFolder);
    if (logs.empty()) return {};
    const int workersCount = std::max(1, std::min<int>({maxParallel, 4, static_cast<int>(logs.size())}));
    std::atomic<std::size_t> next = 0;
    std::atomic<int> completed = 0;
    std::mutex resultMutex;
    std::vector<std::string> downloaded;
    std::vector<std::string> errors;
    std::vector<std::thread> workers;
    for (int worker = 0; worker < workersCount; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                if (cancelled && cancelled->load()) break;
                const std::size_t index = next.fetch_add(1);
                if (index >= logs.size()) break;
                const auto& log = logs[index];
                const fs::path finalPath = fs::path(targetFolder) / log.name;
                const fs::path partialPath = finalPath.string() + ".part";
                try {
                    auto sftp = ssh.openSftp();
                    sftp->download(log.path, partialPath.string(), log.size, [&](std::int64_t read) {
                        if (progress) progress({completed.load() + 1, static_cast<int>(logs.size()), log.name, read, log.size});
                    }, cancelled);
                    std::error_code ec;
                    fs::remove(finalPath, ec);
                    fs::rename(partialPath, finalPath, ec);
                    if (ec) throw SshError("Не удалось завершить файл " + log.name + ": " + ec.message());
                    std::lock_guard lock(resultMutex);
                    downloaded.push_back(finalPath.string());
                    ++completed;
                    if (progress) progress({completed.load(), static_cast<int>(logs.size()), log.name, log.size, log.size});
                } catch (const std::exception& error) {
                    std::error_code ec;
                    fs::remove(partialPath, ec);
                    std::lock_guard lock(resultMutex);
                    errors.push_back(log.name + ": " + error.what());
                    ++completed;
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    if (cancelled && cancelled->load()) throw SshError("Загрузка отменена");
    if (!errors.empty()) {
        std::string message = "Не удалось скачать " + std::to_string(errors.size()) + " из " + std::to_string(logs.size()) + " файлов";
        for (const auto& error : errors) message += "\n" + error;
        throw SshError(message);
    }
    std::sort(downloaded.begin(), downloaded.end());
    return downloaded;
}

std::string LogFetcher::streamLogFiltered(SshClient& ssh, const RemoteLogFile& log, const std::string& patternKey) {
    const std::string key = patternKey.empty() ? (log.path.find("frontend") != std::string::npos ? "frontend" : "devices") : patternKey;
    std::string pattern;
    if (key == "unknown") pattern = grepPatterns().at("devices") + "|" + grepPatterns().at("frontend");
    else if (grepPatterns().contains(key)) pattern = grepPatterns().at(key);
    else throw SshError("Неизвестный тип лога: " + key);
    const bool gzip = log.name.size() > 3 && log.name.ends_with(".gz");
    std::string command;
    if (gzip) {
        const bool hasZgrep = !ssh.runCommand("command -v zgrep 2>/dev/null", 5).output.empty();
        if (hasZgrep) command = "zgrep -E -a " + shellQuote(pattern) + " " + shellQuote(log.path);
        else {
            if (ssh.runCommand("command -v zcat 2>/dev/null", 5).output.empty())
                throw SshError("На устройстве недоступны zgrep и zcat: " + log.name);
            command = "zcat " + shellQuote(log.path) + " | grep -E -a " + shellQuote(pattern);
        }
    } else command = "grep -E -a " + shellQuote(pattern) + " " + shellQuote(log.path);
    auto result = ssh.runCommand(command, 300);
    if (result.output.empty()) throw SshError("grep вернул пустой результат для " + log.name);
    return result.output;
}

std::pair<std::string, std::string> DeviceService::identify(SshClient& ssh) {
    const auto lima = ssh.runCommand(std::string("cat ") + LimaKeyPath + " 2>/dev/null", 10).output;
    if (!trim(lima).empty()) return {"Lima", trim(lima)};
    const auto w2 = ssh.runCommand(std::string("grep 'terminalAccount:' ") + W2ConfigPath + " 2>/dev/null | head -1 | awk -F: '{print $2}' | tr -d '\" '", 10).output;
    return !trim(w2).empty() ? std::make_pair("W2", trim(w2)) : std::make_pair("Unknown", "unknown");
}

SystemInfo DeviceService::collectSystemInfo(SshClient& ssh) {
    SystemInfo info;
    const auto read = [&](const std::string& category, const std::string& command, std::string& target) {
        try { target = trim(ssh.runCommand(command, 10).output); }
        catch (const std::exception& error) { info.errors.push_back(category + ": " + error.what()); }
    };
    read("mem", "free -m 2>/dev/null | awk '/^Mem:/ {print $2, $3}'", info.memTotal);
    read("cores", "nproc 2>/dev/null", info.cores);
    read("loadavg", "cat /proc/loadavg 2>/dev/null | awk '{print $1}'", info.load);
    read("temp", "cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null", info.temperature);
    read("uptime", "uptime -p 2>/dev/null", info.uptime);
    read("build", "cat /opt/buildnumber 2>/dev/null", info.build);
    return info;
}

} // namespace fluxus
