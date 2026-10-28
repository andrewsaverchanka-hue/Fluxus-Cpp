#include "App.h"
#include <QHBoxLayout>
#include <QScrollArea>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDateTime>
#include <QFileInfo>
#include <QThread>
#include <chrono>

DeviceWorker::DeviceWorker(fluxus::DeviceConnection device, QString keyPath, QString password, QObject* parent)
    : QThread(parent), device_(std::move(device)), keyPath_(std::move(keyPath)), password_(std::move(password)) {}
DeviceWorker::~DeviceWorker() { requestDisconnect(); wait(); }
void DeviceWorker::requestFetch(const Request& request) { QMutexLocker lock(&mutex_); requests_.push_back(request); condition_.wakeOne(); }
void DeviceWorker::requestDisconnect() { cancelled_ = true; QMutexLocker lock(&mutex_); stopping_ = true; condition_.wakeOne(); }
void DeviceWorker::run() {
    const QString id = q(device_.id);
    emit statusChanged(id, "connecting", {}, {});
    ssh_ = std::make_unique<fluxus::SshClient>(device_, keyPath_.toStdString(), password_.toStdString());
    connected_ = ssh_->connect([this, id](const std::string& level, const std::string& message) { emit logMessage(id, q(level), q(message)); });
    if (!connected_) { emit statusChanged(id, "error", {}, {}); ssh_.reset(); return; }
    try {
        const auto [type, label] = fluxus::DeviceService::identify(*ssh_);
        emit statusChanged(id, "connected", q(type), q(label));
        const auto info = fluxus::DeviceService::collectSystemInfo(*ssh_);
        QString ram = "—", cpu = "—", uptime = q(info.uptime);
        if (!info.memTotal.empty()) {
            const QStringList parts = q(info.memTotal).split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                const double total = parts[0].toDouble(), used = parts[1].toDouble();
                ram = QString("%1 / %2 МБ (%3%)").arg(qint64(used)).arg(qint64(total)).arg(total > 0 ? qRound(used * 100 / total) : 0);
            }
        }
        if (!info.load.empty()) cpu = q(info.load) + " / " + q(info.cores) + " cores";
        if (!info.temperature.empty()) cpu += ", " + QString::number(info.temperature.empty() ? 0 : std::stod(info.temperature) / 1000.0, 'f', 1) + "°C";
        emit systemInfoReady(id, ram, cpu, uptime.isEmpty() ? "—" : uptime);
    } catch (const std::exception& error) { emit logMessage(id, "warning", error.what()); }
    while (true) {
        Request request;
        {
            QMutexLocker lock(&mutex_);
            if (requests_.empty() && !stopping_) condition_.wait(&mutex_);
            if (stopping_) break;
            if (requests_.empty()) continue;
            request = requests_.front(); requests_.pop_front();
        }
        try { fetch(request); }
        catch (const std::exception& error) { emit logMessage(id, "error", error.what()); }
    }
    ssh_->disconnect(); ssh_.reset(); connected_ = false;
    emit statusChanged(id, "disconnected", {}, {});
    emit logMessage(id, "info", "SSH-соединения закрыты");
}
void DeviceWorker::fetch(const Request& request) {
    const QString id = q(device_.id);
    emit logMessage(id, "info", request.stream ? "Чтение логов на устройстве…" : "Поиск логов для скачивания…");
    auto logs = fluxus::LogFetcher::listLogs(*ssh_);
    logs = request.hours > 0 ? fluxus::LogFetcher::filterByHours(logs, request.hours) : fluxus::LogFetcher::filterByDates(logs, request.start, request.end);
    if (logs.empty()) throw fluxus::SshError("За выбранный период логи не найдены");
    if (request.stream) {
        std::vector<fluxus::StreamInput> streams;
        int current = 0;
        for (const auto& log : logs) {
            emit downloadProgress(id, current, static_cast<int>(logs.size()), q(log.name));
            const auto type = log.path.find("frontend") != std::string::npos ? "frontend" : "devices";
            streams.emplace_back(log.name, fluxus::LogFetcher::streamLogFiltered(*ssh_, log, type), type);
            ++current; emit downloadProgress(id, current, static_cast<int>(logs.size()), q(log.name));
        }
        auto result = std::make_shared<AnalysisResult>(fluxus::AnalysisEngine::analyzeStreams(streams));
        const auto [type, label] = fluxus::DeviceService::identify(*ssh_);
        emit streamsReady(id, result, q(type + "_" + label));
        emit logMessage(id, "success", QString("Собрано стримов: %1").arg(streams.size()));
    } else {
        const auto [type, label] = fluxus::DeviceService::identify(*ssh_);
        const QString folder = QString("%1/%2_%3_%4").arg(q(fluxus::Config::instance().downloadedLogsDirectory().string()), q(type), q(label), QDate::currentDate().toString("yyyy-MM-dd"));
        auto paths = fluxus::LogFetcher::downloadLogs(*ssh_, logs, folder.toStdString(), 4, [this, id](const fluxus::DownloadProgress& progress) {
            emit downloadProgress(id, progress.completed, progress.total, q(progress.fileName));
        }, &cancelled_);
        QStringList files;
        for (const auto& path : paths) files << q(path);
        emit logsDownloaded(id, files);
        emit logMessage(id, "success", QString("Скачано файлов: %1").arg(files.size()));
    }
}

ConnectorPage::ConnectorPage(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this); root->setContentsMargins(25, 20, 25, 20); root->setSpacing(10);
    auto* titleRow = new QHBoxLayout; auto* icon = new QLabel; icon->setPixmap(appIcon("ssh.svg").pixmap(28,28)); titleRow->addWidget(icon);
    auto* title = makeTitle("Подключение SSH"); titleRow->addWidget(title); titleRow->addStretch(); root->addLayout(titleRow);
    auto* addCard = makeCard(); auto* add = new QHBoxLayout(addCard); add->setContentsMargins(12,10,12,10);
    vpnInput_ = new QLineEdit; vpnInput_->setPlaceholderText("10.8.0.1"); vpnInput_->setMaximumWidth(180); add->addWidget(vpnInput_);
    commentInput_ = new QLineEdit; commentInput_->setPlaceholderText("Комментарий (опционально)"); add->addWidget(commentInput_, 1);
    auto* addButton = makeButton("Добавить", "plus.svg", true); add->addWidget(addButton); root->addWidget(addCard);
    connect(addButton, &QPushButton::clicked, this, &ConnectorPage::addDevice); connect(vpnInput_, &QLineEdit::returnPressed, this, &ConnectorPage::addDevice);
    auto* periodCard = makeCard(); auto* period = new QHBoxLayout(periodCard); period->setContentsMargins(12,10,12,10);
    period->addWidget(new QLabel("Период:")); hoursMode_ = new QRadioButton("За последние"); hoursMode_->setChecked(true); period->addWidget(hoursMode_);
    hours_ = new QSpinBox; hours_->setRange(1,1440); hours_->setValue(24); hours_->setSuffix(" ч"); hours_->setButtonSymbols(QAbstractSpinBox::NoButtons); period->addWidget(hours_);
    period->addSpacing(20); rangeMode_ = new QRadioButton("Диапазон:"); period->addWidget(rangeMode_);
    fromDate_ = new QDateEdit(QDate::currentDate().addDays(-1)); toDate_ = new QDateEdit(QDate::currentDate());
    for (auto* date : {fromDate_, toDate_}) { date->setCalendarPopup(true); date->setDisplayFormat("dd.MM.yyyy"); period->addWidget(date); if (date == fromDate_) period->addWidget(new QLabel("—")); }
    period->addStretch(); root->addWidget(periodCard);
    auto* devicesCard = makeCard(); auto* devices = new QVBoxLayout(devicesCard); devices->setContentsMargins(12,10,12,10);
    auto* header = new QHBoxLayout; auto* heading = new QLabel("Устройства"); heading->setStyleSheet("font-weight:600"); header->addWidget(heading); header->addStretch();
    auto* clear = makeButton("Очистить все", "broom.svg"); header->addWidget(clear); devices->addLayout(header);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); auto* container = new QWidget; cardsGrid_ = new QGridLayout(container); cardsGrid_->setContentsMargins(0,0,0,0); cardsGrid_->setSpacing(10); cardsGrid_->setAlignment(Qt::AlignTop|Qt::AlignLeft); scroll->setWidget(container); devices->addWidget(scroll,1);
    auto* actions = new QHBoxLayout;
    auto* connectButton = makeButton("Подключиться", "connect.svg", true); auto* disconnect = makeButton("Отключить всё", "disconnect.svg"); auto* remove = makeButton("Удалить", "trash.svg");
    fetchButton_ = makeButton("Скачать логи", "download.svg"); streamButton_ = makeButton("Открыть на устройстве", "stream.svg", true);
    for (auto* button : {connectButton, disconnect, remove, fetchButton_, streamButton_}) actions->addWidget(button);
    actions->addStretch(); devices->addLayout(actions); root->addWidget(devicesCard,1);
    connect(clear,&QPushButton::clicked,this,&ConnectorPage::clearDevices); connect(remove,&QPushButton::clicked,this,&ConnectorPage::removeSelected);
    connect(connectButton,&QPushButton::clicked,this,&ConnectorPage::connectSelected); connect(disconnect,&QPushButton::clicked,this,&ConnectorPage::disconnectAll);
    connect(fetchButton_,&QPushButton::clicked,this,[this]{fetchSelected(false);}); connect(streamButton_,&QPushButton::clicked,this,[this]{fetchSelected(true);});
    for (const auto& device : fluxus::Config::instance().devices()) {
        vpnInput_->setText(q(device.host)); commentInput_->setText(q(device.comment)); addDevice();
    }
}
ConnectorPage::~ConnectorPage() { disconnectAll(); for (auto& [id, worker] : workers_) { worker->wait(); worker->deleteLater(); } }
std::vector<QString> ConnectorPage::selectedIds() { std::vector<QString> ids; for (const auto& [id, card] : cards_) if (card->checked()) ids.push_back(id); return ids; }
void ConnectorPage::addDevice() {
    const QString vpn = vpnInput_->text().trimmed(), comment = commentInput_->text().trimmed();
    if (!QRegularExpression("^10\\.8\\.\\d{1,3}\\.\\d{1,3}$").match(vpn).hasMatch()) { QMessageBox::warning(this,"Неверный формат","VPN-адрес должен быть в формате 10.8.x.x"); return; }
    for (const auto& [id,card] : cards_) if (card->host == vpn) { QMessageBox::information(this,"Уже добавлено","Это устройство уже есть в списке"); return; }
    const QString id = QString("%1").arg(nextId_++,3,10,QChar('0')); auto* card = new DeviceCard(id,vpn,comment); cards_[id] = card; rebuildGrid();
    vpnInput_->clear(); commentInput_->clear(); emit logInfo("Добавлено устройство " + vpn);
}
void ConnectorPage::rebuildGrid() { int index = 0; for (const auto& [id,card] : cards_) { cardsGrid_->removeWidget(card); cardsGrid_->addWidget(card,index/4,index%4); ++index; } }
void ConnectorPage::removeSelected() {
    const auto ids = selectedIds(); if (ids.empty()) { QMessageBox::warning(this,"Нет выбора","Выберите устройства кликом по карточке"); return; }
    for (const auto& id : ids) { if (workers_.contains(id)) workers_[id]->requestDisconnect(); cards_[id]->deleteLater(); cards_.erase(id); } rebuildGrid();
}
void ConnectorPage::clearDevices() {
    if (cards_.empty()) return; if (QMessageBox::question(this,"Очистить","Отключить и удалить все устройства?") != QMessageBox::Yes) return;
    disconnectAll(); for (auto& [id,card] : cards_) card->deleteLater(); cards_.clear(); nextId_ = 1;
}
void ConnectorPage::connectSelected() {
    const auto ids = selectedIds(); if (ids.empty()) { QMessageBox::warning(this,"Нет выбора","Выберите хотя бы одно устройство"); return; }
    const auto& config = fluxus::Config::instance(); const QString key = q(config.string("ssh_key_path"));
    if (key.isEmpty() || !QFileInfo::exists(key)) { QMessageBox::warning(this,"SSH-ключ не настроен","Укажите SSH-ключ в разделе «Настройки»"); return; }
    for (const auto& id : ids) {
        if (workers_.contains(id) && workers_[id]->isRunning()) continue;
        auto* card = cards_[id]; fluxus::DeviceConnection device; device.id = id.toStdString(); device.host = card->host.toStdString(); device.comment = card->comment.toStdString();
        auto* worker = new DeviceWorker(device,key,q(config.string("default_device_password")),this); workers_[id] = worker;
        connect(worker,&DeviceWorker::statusChanged,this,[this](const QString& id,const QString& status,const QString& type,const QString& label){ if(cards_.contains(id)) cards_[id]->setStatus(status,type,label); });
        connect(worker,&DeviceWorker::logMessage,this,[this](const QString& id,const QString& level,const QString& message){ const QString text="["+id+"] "+message; if(level=="error")emit logError(text);else if(level=="warning")emit logWarning(text);else if(level=="success")emit logSuccess(text);else emit logInfo(text); });
        connect(worker,&DeviceWorker::systemInfoReady,this,[this](const QString& id,const QString& ram,const QString& cpu,const QString& uptime){if(cards_.contains(id))cards_[id]->setSystemInfo(ram,cpu,uptime);});
        connect(worker,&DeviceWorker::downloadProgress,this,[this](const QString& id,int current,int total,const QString& file){if(cards_.contains(id))cards_[id]->setProgress(current,total,file);});
        connect(worker,&DeviceWorker::logsDownloaded,this,[this](const QString& id,const QStringList& files){if(cards_.contains(id))cards_[id]->resetProgress();emit logsReadyForAnalysis(files);});
        connect(worker,&DeviceWorker::streamsReady,this,[this](const QString& id,std::shared_ptr<AnalysisResult> result,const QString& label){if(cards_.contains(id))cards_[id]->resetProgress();emit streamsReadyForAnalysis(id,result,label);});
        worker->start();
    }
}
void ConnectorPage::disconnectAll() { for (auto& [id,worker] : workers_) if (worker->isRunning()) worker->requestDisconnect(); }
void ConnectorPage::fetchSelected(bool stream) {
    const auto ids = selectedIds(); if (ids.empty()) { QMessageBox::warning(this,"Нет выбора","Выберите устройства"); return; }
    DeviceWorker::Request request; request.stream = stream;
    if (hoursMode_->isChecked()) request.hours = hours_->value();
    else {
        request.hours = 0;
        request.start = QDateTime(fromDate_->date(),QTime(0,0)).toSecsSinceEpoch(); request.end = QDateTime(toDate_->date(),QTime(23,59,59)).toSecsSinceEpoch();
        if (request.start >= request.end) { QMessageBox::warning(this,"Неверный диапазон","Проверьте даты"); return; }
    }
    int sent = 0; for (const auto& id : ids) if (workers_.contains(id) && workers_[id]->isRunning() && workers_[id]->connected()) { workers_[id]->requestFetch(request); ++sent; }
    if (!sent) QMessageBox::warning(this,"Нет подключённых","Сначала подключитесь к выбранным устройствам");
}
