#pragma once

#include "AnalysisEngine.h"
#include "Config.h"
#include "Network.h"
#include "Serializer.h"

#include <QMainWindow>
#include <QFrame>
#include <QThread>
#include <QWaitCondition>
#include <QMutex>
#include <QPointer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QDateEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QStackedWidget>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QComboBox>
#include <QTabWidget>
#include <atomic>
#include <deque>
#include <map>
#include <memory>

using AnalysisResult = fluxus::AnalysisResult;
using AnalysisData = std::map<QString, std::shared_ptr<AnalysisResult>>;
Q_DECLARE_METATYPE(std::shared_ptr<AnalysisResult>)
Q_DECLARE_METATYPE(AnalysisData)

QString appVersion();
void applyDarkTheme(class QApplication& app);
QIcon appIcon(const QString& name);
QFrame* makeCard();
QLabel* makeTitle(const QString& text, const QString& icon = {});
QPushButton* makeButton(const QString& text, const QString& icon = {}, bool primary = false);
QString q(const std::string& value);

class StatusLine : public QLabel {
    Q_OBJECT
public:
    explicit StatusLine(QWidget* parent = nullptr);
public slots:
    void logInfo(const QString& text);
    void logSuccess(const QString& text);
    void logWarning(const QString& text);
    void logError(const QString& text);
private:
    void setMessage(const QString& text, const QString& color);
};

class Sidebar : public QFrame {
    Q_OBJECT
public:
    explicit Sidebar(QWidget* parent = nullptr);
    void addNavItem(const QString& id, const QString& icon, const QString& text);
public slots:
    void setActivePage(const QString& id);
    void showUpdateAvailable(const QString& version);
signals:
    void pageChanged(const QString& id);
private:
    QVBoxLayout* navLayout_;
    QPushButton* updateButton_;
    std::map<QString, QPushButton*> buttons_;
};

class DeviceCard : public QFrame {
    Q_OBJECT
public:
    DeviceCard(QString id, QString host, QString comment, QWidget* parent = nullptr);
    QString id, host, comment;
    bool checked() const { return checked_; }
    void setChecked(bool value);
    void setStatus(const QString& status, const QString& type = {}, const QString& label = {});
    void setSystemInfo(const QString& ram, const QString& cpu, const QString& uptime);
    void setProgress(int current, int total, const QString& file = {});
    void resetProgress();
signals:
    void checkedChanged(const QString& id, bool checked);
protected:
    void mousePressEvent(QMouseEvent* event) override;
private:
    void refresh();
    bool checked_ = false;
    QString status_ = "disconnected", type_, label_;
    QLabel *check_, *dot_, *statusText_, *badge_, *idRow_, *ramRow_, *cpuRow_, *uptimeRow_;
    QWidget* info_;
    QFrame* separator_;
    QProgressBar* progress_;
};

class DeviceWorker : public QThread {
    Q_OBJECT
public:
    struct Request { bool stream = false; int hours = 24; qint64 start = 0, end = 0; };
    DeviceWorker(fluxus::DeviceConnection device, QString keyPath, QString password, QObject* parent = nullptr);
    ~DeviceWorker() override;
    void requestFetch(const Request& request);
    void requestDisconnect();
    bool connected() const { return connected_.load(); }
signals:
    void statusChanged(const QString& id, const QString& status, const QString& type, const QString& label);
    void logMessage(const QString& id, const QString& level, const QString& message);
    void systemInfoReady(const QString& id, const QString& ram, const QString& cpu, const QString& uptime);
    void downloadProgress(const QString& id, int current, int total, const QString& file);
    void logsDownloaded(const QString& id, const QStringList& files);
    void streamsReady(const QString& id, std::shared_ptr<AnalysisResult> result, const QString& label);
protected:
    void run() override;
private:
    void fetch(const Request& request);
    fluxus::DeviceConnection device_;
    QString keyPath_, password_;
    std::unique_ptr<fluxus::SshClient> ssh_;
    QMutex mutex_;
    QWaitCondition condition_;
    std::deque<Request> requests_;
    bool stopping_ = false, disconnecting_ = false;
    std::atomic_bool connected_ = false, cancelled_ = false;
};

class ConnectorPage : public QWidget {
    Q_OBJECT
public:
    explicit ConnectorPage(QWidget* parent = nullptr);
    ~ConnectorPage() override;
    void disconnectAll();
signals:
    void logInfo(const QString& text);
    void logSuccess(const QString& text);
    void logWarning(const QString& text);
    void logError(const QString& text);
    void logsReadyForAnalysis(const QStringList& files);
    void streamsReadyForAnalysis(const QString& id, std::shared_ptr<AnalysisResult> result, const QString& label);
private slots:
    void addDevice();
    void removeSelected();
    void clearDevices();
    void connectSelected();
    void fetchSelected(bool stream);
private:
    void rebuildGrid();
    std::vector<QString> selectedIds();
    QLineEdit *vpnInput_, *commentInput_;
    QRadioButton *hoursMode_, *rangeMode_;
    QSpinBox* hours_;
    QDateEdit *fromDate_, *toDate_;
    QGridLayout* cardsGrid_;
    QPushButton *fetchButton_, *streamButton_;
    std::map<QString, DeviceCard*> cards_;
    std::map<QString, DeviceWorker*> workers_;
    int nextId_ = 1;
};

class AnalysisWorker : public QThread {
    Q_OBJECT
public:
    explicit AnalysisWorker(QStringList files, QObject* parent = nullptr);
signals:
    void progress(int percent, const QString& file);
    void finishedWithData(const AnalysisData& data);
    void failed(const QString& error);
protected:
    void run() override;
private:
    QStringList files_;
};

class AnalysisResultWindow : public QMainWindow {
    Q_OBJECT
public:
    AnalysisResultWindow(AnalysisData data, QString title, QWidget* parent = nullptr);
private:
    QWidget* statsTab(const AnalysisResult& result);
    QWidget* lifecycleTab(const AnalysisResult& result);
    QWidget* bnvTab(const AnalysisResult& result);
    QWidget* w2Tab(const AnalysisResult& result);
    QWidget* lowLevelTab(const AnalysisResult& result);
    QWidget* healthTab(const AnalysisResult& result);
    QTreeWidget* makeTree(const QStringList& headers);
    void saveAnalysis(bool all);
    AnalysisData data_;
};

class ParserPage : public QWidget {
    Q_OBJECT
public:
    explicit ParserPage(QWidget* parent = nullptr);
    void addFiles(const QStringList& files);
    void openStreams(const QString& id, std::shared_ptr<AnalysisResult> result, const QString& label);
signals:
    void logInfo(const QString& text);
    void logSuccess(const QString& text);
    void logWarning(const QString& text);
    void logError(const QString& text);
    void progressUpdate(int value, const QString& status);
public slots:
    void chooseFiles();
    void scanFolder();
    void startAnalysis();
    void loadAnalysis();
private:
    struct Record { QString title; AnalysisData data; QPointer<AnalysisResultWindow> window; QFrame* card = nullptr; };
    void refreshFiles();
    void addAnalysis(const QString& title, const AnalysisData& data);
    void removeRecord(int index);
    void refreshRecords();
    QListWidget* filesList_;
    QPushButton *analyzeButton_, *loadButton_;
    QVBoxLayout* recordsLayout_;
    QStringList selectedFiles_;
    std::vector<Record> records_;
    QPointer<AnalysisWorker> worker_;
};

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);
signals:
    void logInfo(const QString& text);
    void logWarning(const QString& text);
    void logError(const QString& text);
public slots:
    void browseKey();
    void saveSettings();
    void checkUpdates();
private:
    QLineEdit *keyPath_, *password_;
    QCheckBox* autoUpdates_;
    QLabel* updateStatus_;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    Sidebar* sidebar_;
    QStackedWidget* stack_;
    ConnectorPage* connector_;
    ParserPage* parser_;
    SettingsPage* settings_;
    StatusLine* status_;
    QProgressBar* progress_;
    QLabel* progressText_;
};
