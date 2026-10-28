#include "App.h"
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <thread>
#include <atomic>

AnalysisWorker::AnalysisWorker(QStringList files, QObject* parent) : QThread(parent), files_(std::move(files)) {}
void AnalysisWorker::run() {
    try {
        std::map<QString, QStringList> groups;
        for (const auto& file : files_) groups[QFileInfo(file).absolutePath()] << file;
        std::vector<std::pair<QString,QStringList>> work(groups.begin(),groups.end());
        std::vector<std::shared_ptr<AnalysisResult>> results(work.size());
        std::atomic<std::size_t> next=0; std::atomic<int> completed=0; std::atomic_bool failed=false; std::exception_ptr error; QMutex errorMutex;
        auto task=[&] {
            while(!failed){
                const auto index=next.fetch_add(1);if(index>=work.size())break;
                try{
                    std::vector<std::string> paths;for(const auto& file:work[index].second)paths.push_back(file.toStdString());
                    results[index]=std::make_shared<AnalysisResult>(fluxus::AnalysisEngine::analyzeDevice(paths,[&](const std::string& file){emit progress(files_.size()?completed.load()*100/files_.size():0,q(file));}));
                    completed+=static_cast<int>(paths.size());emit progress(files_.size()?completed.load()*100/files_.size():100,QFileInfo(work[index].second.last()).fileName());
                }catch(...){failed=true;QMutexLocker lock(&errorMutex);if(!error)error=std::current_exception();}
            }
        };
        std::vector<std::thread> threads;const int count=std::max(1,std::min<int>(4,work.size()));for(int i=0;i<count;++i)threads.emplace_back(task);for(auto& thread:threads)thread.join();if(error)std::rethrow_exception(error);
        AnalysisData data;
        for(std::size_t i=0;i<work.size();++i){QString id=QFileInfo(work[i].first).fileName();if(id.isEmpty())id="local";if(data.contains(id))id+=QString("_%1").arg(i+1);data[id]=results[i];}
        emit finishedWithData(data);
    } catch(const std::exception& e){emit failed(e.what());}
}

ParserPage::ParserPage(QWidget* parent) : QWidget(parent) {
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);auto* container=new QWidget;auto* root=new QVBoxLayout(container);root->setContentsMargins(25,20,25,20);root->setSpacing(14);
    auto* titleRow=new QHBoxLayout;auto* icon=new QLabel;icon->setPixmap(appIcon("parser.svg").pixmap(28,28));titleRow->addWidget(icon);titleRow->addWidget(makeTitle("Анализатор логов"));titleRow->addStretch();root->addLayout(titleRow);
    auto* newCard=makeCard();auto* analysis=new QVBoxLayout(newCard);auto* heading=new QLabel("🚀  Запуск нового анализа");heading->setStyleSheet("font-size:12pt;font-weight:600");analysis->addWidget(heading);analysis->addWidget(new QLabel("Выбранные файлы:"));
    auto* filesRow=new QHBoxLayout;filesList_=new QListWidget;filesList_->setMinimumHeight(200);filesList_->setSelectionMode(QAbstractItemView::ExtendedSelection);filesRow->addWidget(filesList_,1);
    auto* buttons=new QVBoxLayout;auto* add=makeButton("Добавить файлы","plus.svg");auto* scan=makeButton("Сканировать папку","folder.svg");auto* remove=makeButton("Удалить","trash.svg");auto* clear=makeButton("Очистить","broom.svg");for(auto* b:{add,scan,remove,clear})buttons->addWidget(b);buttons->addStretch();filesRow->addLayout(buttons);analysis->addLayout(filesRow);
    analyzeButton_=makeButton("Начать анализ","rocket.svg",true);analyzeButton_->setMinimumWidth(180);auto* analyzeRow=new QHBoxLayout;analyzeRow->addStretch();analyzeRow->addWidget(analyzeButton_);analyzeRow->addStretch();analysis->addLayout(analyzeRow);root->addWidget(newCard);
    auto* loadCard=makeCard();auto* loadRow=new QHBoxLayout(loadCard);auto* loadText=new QLabel("📁  Загрузить сохранённый анализ");loadText->setStyleSheet("font-size:12pt;font-weight:600");loadRow->addWidget(loadText);loadRow->addStretch();loadButton_=makeButton("Выбрать .anl.gz","folder.svg",true);loadRow->addWidget(loadButton_);root->addWidget(loadCard);
    auto* history=makeCard();auto* historyLayout=new QVBoxLayout(history);auto* historyHeader=new QHBoxLayout;auto* historyText=new QLabel("Выполненные анализы");historyText->setStyleSheet("font-size:12pt;font-weight:600");historyHeader->addWidget(historyText);historyHeader->addStretch();auto* clearHistory=makeButton("Очистить все","broom.svg");historyHeader->addWidget(clearHistory);historyLayout->addLayout(historyHeader);recordsLayout_=new QVBoxLayout;historyLayout->addLayout(recordsLayout_);root->addWidget(history);root->addStretch();
    auto* pageLayout=new QVBoxLayout(this);pageLayout->setContentsMargins(0,0,0,0);scroll->setWidget(container);pageLayout->addWidget(scroll);
    connect(add,&QPushButton::clicked,this,&ParserPage::chooseFiles);connect(scan,&QPushButton::clicked,this,&ParserPage::scanFolder);connect(analyzeButton_,&QPushButton::clicked,this,&ParserPage::startAnalysis);connect(loadButton_,&QPushButton::clicked,this,&ParserPage::loadAnalysis);
    connect(remove,&QPushButton::clicked,this,[this]{auto items=filesList_->selectedItems();std::vector<int> rows;for(auto* item:items)rows.push_back(filesList_->row(item));std::sort(rows.rbegin(),rows.rend());for(int row:rows)selectedFiles_.removeAt(row);refreshFiles();});
    connect(clear,&QPushButton::clicked,this,[this]{selectedFiles_.clear();refreshFiles();});
    connect(clearHistory,&QPushButton::clicked,this,[this]{if(QMessageBox::question(this,"Очистить","Удалить все выполненные анализы из истории?")==QMessageBox::Yes){for(auto& record:records_)if(record.window)record.window->close();records_.clear();refreshRecords();}});
    refreshRecords();
}
void ParserPage::addFiles(const QStringList& files) {for(const auto& file:files)if(!selectedFiles_.contains(file))selectedFiles_<<file;refreshFiles();if(!files.isEmpty())emit logInfo(QString("Добавлено файлов: %1").arg(files.size()));}
void ParserPage::refreshFiles() {filesList_->clear();for(const auto& file:selectedFiles_){QFileInfo info(file);auto* item=new QListWidgetItem(info.fileName()+"   •   "+info.dir().dirName()+(file.endsWith(".gz")?"   [gz]":""));item->setToolTip(info.absoluteFilePath());filesList_->addItem(item);}}
void ParserPage::chooseFiles() {addFiles(QFileDialog::getOpenFileNames(this,"Выберите файлы логов",{},"Log files (*.log *.log.* *.gz *.txt);;All files (*)"));}
void ParserPage::scanFolder() {
    const auto folder=QFileDialog::getExistingDirectory(this,"Выберите папку с логами");if(folder.isEmpty())return;QStringList found;QDir dir(folder);
    for(const auto& info:dir.entryInfoList(QDir::Files,QDir::Name)){const auto name=info.fileName();if(name.endsWith(".log")||name.endsWith(".gz")||QRegularExpression("(devices_server|skills-battle-frontend)\\.log\\.\\d+$").match(name).hasMatch())found<<info.absoluteFilePath();}
    addFiles(found);QMessageBox::information(this,"Сканирование",QString("Найдено файлов: %1").arg(found.size()));
}
void ParserPage::startAnalysis() {
    if(selectedFiles_.isEmpty()){QMessageBox::warning(this,"Нет файлов","Сначала выберите файлы логов");return;}if(worker_&&worker_->isRunning())return;
    analyzeButton_->setEnabled(false);loadButton_->setEnabled(false);worker_=new AnalysisWorker(selectedFiles_,this);
    connect(worker_,&AnalysisWorker::progress,this,[this](int value,const QString& file){emit progressUpdate(value,QString("%1% — %2").arg(value).arg(QFileInfo(file).fileName()));});
    connect(worker_,&AnalysisWorker::finishedWithData,this,[this](const AnalysisData& data){analyzeButton_->setEnabled(true);loadButton_->setEnabled(true);emit progressUpdate(100,"Анализ завершён");QString title=data.size()==1?data.begin()->first:QString("Multi-device (%1 devices)").arg(data.size());addAnalysis(title,data);emit logSuccess(QString("Анализ завершён. Устройств: %1").arg(data.size()));worker_->deleteLater();});
    connect(worker_,&AnalysisWorker::failed,this,[this](const QString& error){analyzeButton_->setEnabled(true);loadButton_->setEnabled(true);QMessageBox::critical(this,"Ошибка анализа",error);emit logError(error);worker_->deleteLater();});worker_->start();
}
void ParserPage::loadAnalysis() {
    const auto files=QFileDialog::getOpenFileNames(this,"Загрузить анализ",{},"Fluxus analysis (*.anl.gz)");for(const auto& file:files){try{auto result=std::make_shared<AnalysisResult>(fluxus::AnalysisSerializer::load(file.toStdString()));AnalysisData data;data[QFileInfo(file).completeBaseName()]=result;addAnalysis(QFileInfo(file).fileName(),data);emit logSuccess("Загружен анализ: "+file);}catch(const std::exception& e){QMessageBox::critical(this,"Ошибка загрузки",e.what());}}
}
void ParserPage::openStreams(const QString& id,std::shared_ptr<AnalysisResult> result,const QString& label) {AnalysisData data;data[label.isEmpty()?id:label]=result;addAnalysis(label.isEmpty()?id:label,data);}
void ParserPage::addAnalysis(const QString& title,const AnalysisData& data) {
    Record record;record.title=title;record.data=data;record.window=new AnalysisResultWindow(data,title);records_.push_back(record);const int index=static_cast<int>(records_.size())-1;records_[index].window->show();records_[index].window->raise();records_[index].window->activateWindow();
    connect(records_[index].window,&QObject::destroyed,this,[this]{refreshRecords();});refreshRecords();
}
void ParserPage::refreshRecords() {
    while(auto* item=recordsLayout_->takeAt(0)){if(item->widget())item->widget()->deleteLater();delete item;}
    if(records_.empty()){auto* label=new QLabel("Пока нет выполненных анализов. Запустите новый анализ или загрузите .anl.gz.");label->setStyleSheet("color:#5b5f7a;font-style:italic;padding:20px");recordsLayout_->addWidget(label);return;}
    for(std::size_t i=0;i<records_.size();++i){auto* card=new QFrame;card->setObjectName("analysisItemCard");card->setFixedHeight(48);card->setStyleSheet("QFrame#analysisItemCard{background:#2a2b40;border:1px solid #34354e;border-radius:8px}");auto* row=new QHBoxLayout(card);row->setContentsMargins(8,0,8,0);
        auto* open=new QPushButton(records_[i].title);open->setFlat(true);open->setCursor(Qt::PointingHandCursor);open->setStyleSheet("text-align:left;border:none;background:transparent;font-weight:600");row->addWidget(open,1);auto* status=new QLabel(records_[i].window?"окно открыто":"окно закрыто");status->setStyleSheet(QString("color:%1").arg(records_[i].window?"#10b981":"#9ca3bf"));row->addWidget(status);auto* remove=makeButton("×");remove->setFixedSize(28,28);row->addWidget(remove);recordsLayout_->addWidget(card);
        connect(open,&QPushButton::clicked,this,[this,i]{if(!records_[i].window)records_[i].window=new AnalysisResultWindow(records_[i].data,records_[i].title);records_[i].window->show();records_[i].window->raise();records_[i].window->activateWindow();refreshRecords();});connect(remove,&QPushButton::clicked,this,[this,i]{removeRecord(static_cast<int>(i));});
    }
}
void ParserPage::removeRecord(int index) {if(index<0||index>=static_cast<int>(records_.size()))return;if(records_[index].window)records_[index].window->close();records_.erase(records_.begin()+index);refreshRecords();}
