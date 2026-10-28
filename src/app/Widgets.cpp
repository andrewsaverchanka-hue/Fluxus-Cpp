#include "App.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QDateTime>
#include <QStyle>
#include <QScrollArea>
#include <QtSvg/QSvgRenderer>
#include <QPainter>
#include <QFile>

QString appVersion() { return "1.0.1"; }
QString q(const std::string& value) { return QString::fromUtf8(value.c_str(), static_cast<qsizetype>(value.size())); }
QIcon appIcon(const QString& name) {
    QFile file(":/assets/icons/" + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray svg = file.readAll().replace("currentColor", "#9ca3bf");
    QSvgRenderer renderer(svg);
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    return QIcon(pixmap);
}
QFrame* makeCard() { auto* card = new QFrame; card->setObjectName("card"); return card; }
QLabel* makeTitle(const QString& text, const QString& icon) {
    auto* label = new QLabel(text);
    label->setObjectName("pageTitle");
    label->setStyleSheet("padding:0");
    if (!icon.isEmpty()) label->setPixmap(appIcon(icon).pixmap(28, 28));
    return label;
}
QPushButton* makeButton(const QString& text, const QString& icon, bool primary) {
    auto* button = new QPushButton(text);
    button->setObjectName(primary ? "compactPrimaryButton" : "compactButton");
    button->setCursor(Qt::PointingHandCursor);
    if (!icon.isEmpty()) { button->setIcon(appIcon(icon)); button->setIconSize({16, 16}); }
    return button;
}

void applyDarkTheme(QApplication& app) {
    QPalette palette;
    palette.setColor(QPalette::Window, "#1a1b2e");
    palette.setColor(QPalette::WindowText, "#e4e5f1");
    palette.setColor(QPalette::Base, "#2d2e45");
    palette.setColor(QPalette::AlternateBase, "#2a2b40");
    palette.setColor(QPalette::Text, "#e4e5f1");
    palette.setColor(QPalette::Button, "#2a2b40");
    palette.setColor(QPalette::ButtonText, "#e4e5f1");
    palette.setColor(QPalette::Highlight, "#6366f1");
    palette.setColor(QPalette::HighlightedText, "#ffffff");
    palette.setColor(QPalette::ToolTipBase, "#2a2b40");
    palette.setColor(QPalette::ToolTipText, "#e4e5f1");
    app.setPalette(palette);
    app.setStyleSheet(R"QSS(
QMainWindow,QWidget#centralWidget,QWidget#contentArea{background:#1a1b2e;color:#e4e5f1}
QWidget{color:#e4e5f1;font-family:'Segoe UI','Arial',sans-serif;font-size:10pt}
QFrame#sidebar{background:#232438;border-right:1px solid #34354e}
QLabel#sidebarAppName{font-size:11pt;font-weight:600;padding:5px 20px}
QLabel#sidebarAppVersion,QLabel#sidebarSectionTitle,QLabel#sidebarFooter{color:#9ca3bf;font-size:8pt;padding:4px 20px}
QPushButton#sidebarNavButton{background:transparent;border:none;border-radius:8px;padding:10px 15px;margin:2px 10px;text-align:left;min-height:24px}
QPushButton#sidebarNavButton:hover{background:#353657}QPushButton#sidebarNavButton:checked{background:#6366f1;color:white;font-weight:600}
QLabel#pageTitle{font-size:20pt;font-weight:700;padding:20px 0 10px}QLabel#pageSubtitle{color:#9ca3bf;padding:0 0 15px}
QFrame#card{background:#232438;border:1px solid #34354e;border-radius:12px;padding:8px}
QPushButton{background:#2a2b40;border:1px solid #34354e;border-radius:8px;padding:7px 14px;min-height:24px}
QPushButton:hover{background:#353657;border-color:#6366f1}QPushButton:pressed{background:#2a2b40}QPushButton:disabled{color:#5b5f7a;background:#232438}
QPushButton#compactPrimaryButton,QPushButton#primaryButton{background:#6366f1;color:white;border:none;border-radius:10px;padding:8px 18px;font-weight:600}
QPushButton#compactPrimaryButton:hover,QPushButton#primaryButton:hover{background:#7c7fe8}
QLineEdit,QTextEdit,QPlainTextEdit,QComboBox,QSpinBox,QDateEdit{background:#2d2e45;border:1px solid #34354e;border-radius:8px;padding:7px 10px;min-height:24px;selection-background-color:#6366f1}
QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDateEdit:focus{border-color:#6366f1}QComboBox::drop-down{border:none;width:24px}
QComboBox QAbstractItemView{background:#2a2b40;selection-background-color:#6366f1}
QProgressBar{background:#2a2b40;border:1px solid #34354e;border-radius:6px;text-align:center;height:20px}QProgressBar::chunk{background:#6366f1;border-radius:5px}
QLabel#statusLine{background:#141524;color:#c9cad8;border:1px solid #34354e;border-radius:8px;padding:7px 12px;font-family:Consolas,monospace}
QCheckBox,QRadioButton{spacing:8px;min-height:22px}QCheckBox::indicator,QRadioButton::indicator{width:16px;height:16px;border:1px solid #6366f1;background:#2d2e45}
QCheckBox::indicator{border-radius:4px}QRadioButton::indicator{border-radius:8px}QCheckBox::indicator:checked,QRadioButton::indicator:checked{background:#6366f1}
QTabWidget::pane{background:#232438;border:1px solid #34354e}QTabBar::tab{background:#2a2b40;color:#9ca3bf;padding:9px 14px;border-top-left-radius:6px;border-top-right-radius:6px;margin-right:2px}
QTabBar::tab:selected{background:#232438;color:#e4e5f1;border-top:2px solid #6366f1;font-weight:600}
QTreeWidget,QTableWidget,QListWidget{background:#232438;alternate-background-color:#2a2b40;border:1px solid #34354e;border-radius:6px;gridline-color:#2d2e45;selection-background-color:#6366f1;outline:none}
QTreeWidget::item,QListWidget::item{padding:5px}QHeaderView::section{background:#2a2b40;padding:8px;border:none;border-right:1px solid #2d2e45;border-bottom:1px solid #34354e;font-weight:600}
QScrollArea{background:transparent;border:none}QScrollBar:vertical{background:#1a1b2e;width:12px}QScrollBar::handle:vertical{background:#353657;border-radius:6px;min-height:30px}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0}QToolTip{background:#2a2b40;color:#e4e5f1;border:1px solid #6366f1;padding:5px}
QLabel{background:transparent}QStatusBar{background:#232438;color:#9ca3bf}
)QSS");
}

StatusLine::StatusLine(QWidget* parent) : QLabel(parent) {
    setObjectName("statusLine");
    setMinimumHeight(34);
    logInfo("Готов к работе");
}
void StatusLine::setMessage(const QString& text, const QString& color) {
    setText(QString("> [%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), text));
    setStyleSheet("QLabel#statusLine{background:#141524;border:1px solid #34354e;border-radius:8px;padding:7px 12px;color:" + color + ";font-family:Consolas,monospace}");
    setToolTip(text);
}
void StatusLine::logInfo(const QString& text) { setMessage(text, "#c9cad8"); }
void StatusLine::logSuccess(const QString& text) { setMessage(text, "#4ade80"); }
void StatusLine::logWarning(const QString& text) { setMessage(text, "#fbbf24"); }
void StatusLine::logError(const QString& text) { setMessage(text, "#f87171"); }

Sidebar::Sidebar(QWidget* parent) : QFrame(parent) {
    setObjectName("sidebar");
    setFixedWidth(240);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 12, 0, 8);
    root->setSpacing(4);
    auto* logo = new QLabel;
    logo->setPixmap(QPixmap(":/logo_full.png").scaledToWidth(170, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);
    root->addWidget(logo);
    auto* name = new QLabel("Fluxus"); name->setObjectName("sidebarAppName"); root->addWidget(name);
    auto* version = new QLabel("C++ • v" + appVersion()); version->setObjectName("sidebarAppVersion"); root->addWidget(version);
    auto* section = new QLabel("НАВИГАЦИЯ"); section->setObjectName("sidebarSectionTitle"); root->addWidget(section);
    navLayout_ = new QVBoxLayout; navLayout_->setSpacing(2); root->addLayout(navLayout_);
    root->addStretch();
    updateButton_ = makeButton("Новая версия", "download.svg", true);
    updateButton_->hide();
    root->addWidget(updateButton_);
    auto* footer = new QLabel("© 2026 Fluxus"); footer->setObjectName("sidebarFooter"); footer->setAlignment(Qt::AlignCenter); root->addWidget(footer);
}
void Sidebar::addNavItem(const QString& id, const QString& icon, const QString& text) {
    auto* button = new QPushButton("  " + text);
    button->setObjectName("sidebarNavButton");
    button->setCheckable(true);
    button->setIcon(appIcon(icon));
    button->setCursor(Qt::PointingHandCursor);
    navLayout_->addWidget(button);
    buttons_[id] = button;
    connect(button, &QPushButton::clicked, this, [this, id] { setActivePage(id); emit pageChanged(id); });
}
void Sidebar::setActivePage(const QString& id) {
    for (auto& [key, button] : buttons_) button->setChecked(key == id);
}
void Sidebar::showUpdateAvailable(const QString& version) { updateButton_->setText("Доступна v" + version); updateButton_->show(); }

DeviceCard::DeviceCard(QString deviceId, QString vpnHost, QString deviceComment, QWidget* parent)
    : QFrame(parent), id(std::move(deviceId)), host(std::move(vpnHost)), comment(std::move(deviceComment)) {
    setObjectName("deviceCard");
    setFixedWidth(250);
    setCursor(Qt::PointingHandCursor);
    setToolTip("ID: #" + id + "\nVPN: " + host);
    auto* root = new QVBoxLayout(this); root->setContentsMargins(10, 8, 10, 10); root->setSpacing(6);
    auto* header = new QHBoxLayout;
    auto* vpn = new QLabel(host); vpn->setStyleSheet("font-weight:600;font-size:12pt"); header->addWidget(vpn); header->addStretch();
    check_ = new QLabel; check_->setFixedSize(24, 24); check_->setAlignment(Qt::AlignCenter); header->addWidget(check_);
    badge_ = new QLabel; badge_->setStyleSheet("color:#10b981;font-weight:600;padding:2px 8px;background:#203d3b;border-radius:4px"); badge_->hide(); header->addWidget(badge_);
    root->addLayout(header);
    auto* statusRow = new QHBoxLayout;
    dot_ = new QLabel; dot_->setFixedWidth(16); dot_->setAlignment(Qt::AlignCenter); statusRow->addWidget(dot_);
    statusText_ = new QLabel; statusRow->addWidget(statusText_, 1); root->addLayout(statusRow);
    separator_ = new QFrame; separator_->setFrameShape(QFrame::HLine); separator_->setStyleSheet("color:#34354e;max-height:1px"); separator_->hide(); root->addWidget(separator_);
    info_ = new QWidget; auto* infoLayout = new QVBoxLayout(info_); infoLayout->setContentsMargins(0,0,0,0); infoLayout->setSpacing(3);
    idRow_ = new QLabel; ramRow_ = new QLabel; cpuRow_ = new QLabel; uptimeRow_ = new QLabel;
    for (auto* label : {idRow_, ramRow_, cpuRow_, uptimeRow_}) { label->setMinimumHeight(20); label->setStyleSheet("color:#c9cad8;font-size:9pt"); infoLayout->addWidget(label); }
    info_->hide(); root->addWidget(info_);
    if (!comment.isEmpty()) { auto* text = new QLabel("💬 " + comment); text->setWordWrap(true); text->setStyleSheet("color:#9ca3bf;font-size:8pt;font-style:italic"); root->addWidget(text); }
    progress_ = new QProgressBar; progress_->setMaximumHeight(8); progress_->setTextVisible(false); progress_->hide(); root->addWidget(progress_);
    refresh();
}
void DeviceCard::setChecked(bool value) { if (checked_ == value) return; checked_ = value; refresh(); emit checkedChanged(id, value); }
void DeviceCard::mousePressEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton) setChecked(!checked_); QFrame::mousePressEvent(event); }
void DeviceCard::setStatus(const QString& status, const QString& type, const QString& label) {
    status_ = status;
    if (status == "connected") { type_ = type; label_ = label; }
    if (status == "disconnected" || status == "error") { type_.clear(); label_.clear(); }
    refresh();
}
void DeviceCard::setSystemInfo(const QString& ram, const QString& cpu, const QString& uptime) { ramRow_->setText("💾 RAM: " + ram); cpuRow_->setText("🌡️ CPU: " + cpu); uptimeRow_->setText("⏱ uptime: " + uptime); }
void DeviceCard::setProgress(int current, int total, const QString& file) { progress_->setVisible(total > 0); progress_->setMaximum(total); progress_->setValue(current); if (!file.isEmpty()) progress_->setToolTip(QString("%1/%2: %3").arg(current).arg(total).arg(file)); }
void DeviceCard::resetProgress() { progress_->hide(); progress_->setValue(0); }
void DeviceCard::refresh() {
    check_->setText(checked_ ? "✓" : "");
    check_->setStyleSheet(checked_ ? "background:#8b5cf6;color:white;font-weight:700;font-size:13pt;border-radius:12px" : "background:transparent;border:1.5px solid #4a4b63;border-radius:12px");
    setStyleSheet(QString("QFrame#deviceCard{background:%1;border:2px solid %2;border-radius:10px}QFrame#deviceCard:hover{background:#2a2b40;border:2px solid #6366f1}").arg(checked_ ? "#332d51" : "#232438", checked_ ? "#8b5cf6" : "#34354e"));
    QString symbol = "○", text = "Не подключено", color = "#9ca3bf";
    if (status_ == "connecting") { symbol = "●"; text = "Подключение..."; color = "#f59e0b"; }
    else if (status_ == "connected") { symbol = "●"; text = "Подключено"; color = "#10b981"; }
    else if (status_ == "error") { symbol = "●"; text = "Ошибка"; color = "#ef4444"; }
    dot_->setText(symbol); dot_->setStyleSheet("color:" + color + ";font-size:14pt"); statusText_->setText(text); statusText_->setStyleSheet("color:" + color);
    const bool connected = status_ == "connected" && !type_.isEmpty();
    badge_->setVisible(connected); separator_->setVisible(connected); info_->setVisible(connected);
    if (connected) {
        badge_->setText(type_);
        idRow_->setText((type_ == "Lima" ? "🔑 ID: " : type_ == "W2" ? "💳 Счёт: " : "🏷️ ID: ") + (label_.size() > 17 ? label_.left(16) + "…" : label_));
        if (ramRow_->text().isEmpty()) { ramRow_->setText("💾 RAM: загрузка..."); cpuRow_->setText("🌡️ CPU: загрузка..."); uptimeRow_->setText("⏱ uptime: загрузка..."); }
    } else { ramRow_->clear(); cpuRow_->clear(); uptimeRow_->clear(); }
}
