#include "App.h"
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QUrl>
#include <QCloseEvent>
#include <QTimer>

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    auto* root=new QVBoxLayout(this);root->setContentsMargins(25,20,25,20);root->setSpacing(15);
    auto* titleRow=new QHBoxLayout;auto* icon=new QLabel;icon->setPixmap(appIcon("settings.svg").pixmap(28,28));titleRow->addWidget(icon);titleRow->addWidget(makeTitle("Настройки"));titleRow->addStretch();root->addLayout(titleRow);
    auto* subtitle=new QLabel("SSH-ключ для подключения к устройствам, проверка обновлений.");subtitle->setObjectName("pageSubtitle");root->addWidget(subtitle);
    auto* ssh=makeCard();auto* sshLayout=new QVBoxLayout(ssh);auto* sshTitle=new QLabel("🔑  SSH-ключ для подключения к устройствам");sshTitle->setStyleSheet("font-size:11pt;font-weight:600");sshLayout->addWidget(sshTitle);
    auto* keyRow=new QHBoxLayout;keyRow->addWidget(new QLabel("Путь к ключу:"));keyPath_=new QLineEdit;keyPath_->setReadOnly(true);keyPath_->setPlaceholderText("Ключ не выбран");keyRow->addWidget(keyPath_,1);auto* browse=makeButton("Обзор...","folder.svg");keyRow->addWidget(browse);sshLayout->addLayout(keyRow);
    auto* pwdRow=new QHBoxLayout;pwdRow->addWidget(new QLabel("Пароль (fallback):"));password_=new QLineEdit;password_->setEchoMode(QLineEdit::Password);password_->setPlaceholderText("Используется, если на устройстве не настроен SSH-ключ");pwdRow->addWidget(password_,1);sshLayout->addLayout(pwdRow);auto* save=makeButton("Сохранить","save.svg",true);sshLayout->addWidget(save);root->addWidget(ssh);
    auto* updates=makeCard();auto* updatesLayout=new QVBoxLayout(updates);auto* updatesTitle=new QLabel("🔄  Обновления");updatesTitle->setStyleSheet("font-size:11pt;font-weight:600");updatesLayout->addWidget(updatesTitle);updatesLayout->addWidget(new QLabel("Текущая версия: Fluxus v"+appVersion()));
    autoUpdates_=new QCheckBox("Проверять обновления при запуске");updatesLayout->addWidget(autoUpdates_);auto* check=makeButton("Проверить обновления сейчас","refresh.svg",true);updatesLayout->addWidget(check,0,Qt::AlignLeft);updateStatus_=new QLabel;updateStatus_->setWordWrap(true);updateStatus_->setStyleSheet("color:#9ca3bf");updatesLayout->addWidget(updateStatus_);root->addWidget(updates);root->addStretch();
    auto& config=fluxus::Config::instance();keyPath_->setText(q(config.string("ssh_key_path")));password_->setText(q(config.string("default_device_password")));autoUpdates_->setChecked(config.boolean("auto_check_updates",true));
    connect(browse,&QPushButton::clicked,this,&SettingsPage::browseKey);connect(save,&QPushButton::clicked,this,&SettingsPage::saveSettings);connect(check,&QPushButton::clicked,this,&SettingsPage::checkUpdates);
    connect(autoUpdates_,&QCheckBox::toggled,this,[](bool checked){fluxus::Config::instance().set("auto_check_updates",checked,true);});
}
void SettingsPage::browseKey() {const auto file=QFileDialog::getOpenFileName(this,"Выберите SSH-ключ",keyPath_->text());if(!file.isEmpty()){keyPath_->setText(file);fluxus::Config::instance().set("ssh_key_path",file.toStdString(),true);emit logInfo("Выбран SSH-ключ: "+file);}}
void SettingsPage::saveSettings() {auto& config=fluxus::Config::instance();config.set("ssh_key_path",keyPath_->text().toStdString());config.set("default_device_password",password_->text().toStdString());config.save();emit logInfo("Настройки сохранены");}
void SettingsPage::checkUpdates() {
    updateStatus_->setText("Проверка обновлений…");auto* manager=new QNetworkAccessManager(this);QNetworkRequest request(QUrl("https://api.github.com/repos/andrewsaverchanka-hue/Fluxus/releases/latest"));request.setHeader(QNetworkRequest::UserAgentHeader,"Fluxus-Cpp/"+appVersion());auto* reply=manager->get(request);
    connect(reply,&QNetworkReply::finished,this,[this,reply,manager]{manager->deleteLater();reply->deleteLater();if(reply->error()!=QNetworkReply::NoError){updateStatus_->setText("Не удалось проверить обновления");emit logWarning("Не удалось проверить обновления");return;}const auto doc=QJsonDocument::fromJson(reply->readAll());const QString version=doc.object().value("tag_name").toString();updateStatus_->setText("Последняя опубликованная версия: "+version);updateStatus_->setStyleSheet("color:#10b981");const auto url=doc.object().value("html_url").toString();if(!version.isEmpty()&&version!=appVersion()&&version!="v"+appVersion()){if(QMessageBox::question(this,"Обновление","Доступна версия "+version+". Открыть страницу загрузки?")==QMessageBox::Yes)QDesktopServices::openUrl(url);}else updateStatus_->setText("У вас установлена последняя версия ("+appVersion()+")");});
}

MainWindow::MainWindow() {
    setWindowTitle("Fluxus v"+appVersion());setWindowIcon(QIcon(":/logo.png"));resize(1400,900);setMinimumSize(1100,700);
    const auto geometry=fluxus::Config::instance().get("last_window_geometry");if(geometry.is_array()&&geometry.size()==4)setGeometry(geometry[0].get<int>(),geometry[1].get<int>(),geometry[2].get<int>(),geometry[3].get<int>());
    auto* central=new QWidget;central->setObjectName("centralWidget");setCentralWidget(central);auto* root=new QHBoxLayout(central);root->setContentsMargins(0,0,0,0);root->setSpacing(0);
    sidebar_=new Sidebar;root->addWidget(sidebar_);auto* right=new QWidget;right->setObjectName("contentArea");auto* rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(0,0,0,0);rightLayout->setSpacing(0);stack_=new QStackedWidget;rightLayout->addWidget(stack_,1);
    auto* progressFrame=new QFrame;auto* progressRow=new QHBoxLayout(progressFrame);progressRow->setContentsMargins(25,5,25,5);progressText_=new QLabel("Готов к работе");progressText_->setStyleSheet("color:#9ca3bf");progressRow->addWidget(progressText_,1);progress_=new QProgressBar;progress_->setMinimumWidth(300);progress_->setMaximumWidth(500);progressRow->addWidget(progress_);rightLayout->addWidget(progressFrame);
    auto* statusFrame=new QFrame;auto* statusLayout=new QVBoxLayout(statusFrame);statusLayout->setContentsMargins(25,5,25,15);status_=new StatusLine;statusLayout->addWidget(status_);rightLayout->addWidget(statusFrame);root->addWidget(right,1);
    connector_=new ConnectorPage;parser_=new ParserPage;settings_=new SettingsPage;stack_->addWidget(connector_);stack_->addWidget(parser_);stack_->addWidget(settings_);
    sidebar_->addNavItem("connector","ssh.svg","Подключение SSH");sidebar_->addNavItem("parser","parser.svg","Анализатор логов");sidebar_->addNavItem("settings","settings.svg","Настройки");sidebar_->setActivePage("connector");
    connect(sidebar_,&Sidebar::pageChanged,this,[this](const QString& id){stack_->setCurrentIndex(id=="connector"?0:id=="parser"?1:2);});
    auto connectLogs=[this](QWidget* page){connect(page,SIGNAL(logInfo(QString)),status_,SLOT(logInfo(QString)));connect(page,SIGNAL(logWarning(QString)),status_,SLOT(logWarning(QString)));connect(page,SIGNAL(logError(QString)),status_,SLOT(logError(QString)));};connectLogs(connector_);connectLogs(parser_);connectLogs(settings_);connect(connector_,&ConnectorPage::logSuccess,status_,&StatusLine::logSuccess);connect(parser_,&ParserPage::logSuccess,status_,&StatusLine::logSuccess);
    connect(connector_,&ConnectorPage::logsReadyForAnalysis,this,[this](const QStringList& files){parser_->addFiles(files);stack_->setCurrentWidget(parser_);sidebar_->setActivePage("parser");parser_->startAnalysis();});
    connect(connector_,&ConnectorPage::streamsReadyForAnalysis,this,[this](const QString& id,std::shared_ptr<AnalysisResult> result,const QString& label){parser_->openStreams(id,result,label);stack_->setCurrentWidget(parser_);sidebar_->setActivePage("parser");});
    connect(parser_,&ParserPage::progressUpdate,this,[this](int value,const QString& text){progress_->setValue(value);progressText_->setText(text);});
    if(fluxus::Config::instance().boolean("auto_check_updates",true))QTimer::singleShot(2000,settings_,&SettingsPage::checkUpdates);
}
void MainWindow::closeEvent(QCloseEvent* event) {connector_->disconnectAll();const auto g=geometry();fluxus::Config::instance().set("last_window_geometry",fluxus::Json::array({g.x(),g.y(),g.width(),g.height()}),true);QMainWindow::closeEvent(event);}
