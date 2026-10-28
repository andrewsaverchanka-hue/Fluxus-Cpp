#include "App.h"
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QTextEdit>
#include <QScrollArea>
#include <QInputDialog>
#include <QDateTime>
#include <sstream>

namespace {
using fluxus::eventString;
QString money(qint64 cents) { return QString::number(cents / 100.0, 'f', 2); }
QString eventText(const fluxus::Event& event) {
    QStringList parts;
    for (const char* key : {"event", "status", "raw", "denomination", "currency", "error", "reason", "request_id", "job_id", "command", "result", "trx", "amount", "notes"}) {
        if (!event.contains(key) || event[key].is_null()) continue;
        QString value;
        if (event[key].is_string()) value = q(event[key].get<std::string>());
        else value = q(event[key].dump());
        if (!value.isEmpty()) parts << QString("%1=%2").arg(key, value);
    }
    return parts.isEmpty() ? q(event.dump()) : parts.join("  ");
}
QTreeWidgetItem* child(QTreeWidgetItem* parent, const QStringList& values) { auto* item = new QTreeWidgetItem(parent, values); return item; }
void setupTree(QTreeWidget* tree) {
    tree->setAlternatingRowColors(true); tree->setUniformRowHeights(true); tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->header()->setStretchLastSection(true); tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
}
}

AnalysisResultWindow::AnalysisResultWindow(AnalysisData data, QString title, QWidget* parent)
    : QMainWindow(parent), data_(std::move(data)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Fluxus — " + title); resize(1250,820); setMinimumSize(900,600);
    auto* central = new QWidget; auto* root = new QVBoxLayout(central); root->setContentsMargins(15,12,15,12);
    auto* actions = new QHBoxLayout; auto* heading = new QLabel(title); heading->setStyleSheet("font-size:16pt;font-weight:700"); actions->addWidget(heading); actions->addStretch();
    auto* save = makeButton("Сохранить анализ", "save.svg", true); auto* saveAll = makeButton("Сохранить все", "save.svg"); actions->addWidget(save); actions->addWidget(saveAll); root->addLayout(actions);
    connect(save,&QPushButton::clicked,this,[this]{saveAnalysis(false);}); connect(saveAll,&QPushButton::clicked,this,[this]{saveAnalysis(true);});
    auto* devices = new QTabWidget; devices->setDocumentMode(true);
    for (const auto& [id,result] : data_) {
        auto* tabs = new QTabWidget;
        tabs->addTab(statsTab(*result), "📊 Статистика");
        if (!result->lima.sessions.empty()) tabs->addTab(lifecycleTab(*result), "💵 Жизненный цикл");
        if (!result->lima.bnvEvents.empty()) tabs->addTab(bnvTab(*result), "🧾 BNV");
        if (!result->w2Frontend.sessions.empty()) tabs->addTab(w2Tab(*result), "💳 W2 Frontend");
        if (!result->w2LowLevel.sessions.empty()) tabs->addTab(lowLevelTab(*result), "⚙ W2 Low-Level");
        if (!result->w2LowLevel.lcdmHealth.empty()) tabs->addTab(healthTab(*result), "♥ Health");
        devices->addTab(tabs,id);
    }
    root->addWidget(devices,1); setCentralWidget(central);
}
QTreeWidget* AnalysisResultWindow::makeTree(const QStringList& headers) { auto* tree = new QTreeWidget; tree->setHeaderLabels(headers); setupTree(tree); return tree; }

QWidget* AnalysisResultWindow::statsTab(const AnalysisResult& result) {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page);
    auto* text = new QTextEdit; text->setReadOnly(true); text->setLineWrapMode(QTextEdit::NoWrap);
    QString out = "ОБЩАЯ СТАТИСТИКА\n================\n\n";
    out += QString("Файлов: %1\n").arg(result.sourceFiles.size());
    out += QString("Lima: строк найдено %1, разобрано %2\n").arg(result.lima.linesFound).arg(result.lima.linesParsed);
    out += QString("W2 frontend: строк %1, совпадений %2\n").arg(result.w2Frontend.linesProcessed).arg(result.w2Frontend.linesMatched);
    out += QString("W2 low-level: строк %1, совпадений %2\n\n").arg(result.w2LowLevel.linesProcessed).arg(result.w2LowLevel.linesMatched);
    out += QString("LIMA / КУПЮРЫ\n--------------\nВсего: %1 купюр на %2\n").arg(result.lima.totalCount).arg(result.lima.totalSum,0,'f',2);
    for (const auto& [currency,denoms] : result.lima.stats) {
        out += "\n" + q(currency) + ":\n";
        for (const auto& [denom,count] : denoms) out += QString("  %1 × %2 = %3\n").arg(denom,0,'f',0).arg(count).arg(denom*count,0,'f',2);
    }
    out += QString("\nСессий приёма: %1\nИнкассаций: %2\nBNV событий: %3\n").arg(result.lima.sessions.size()).arg(result.lima.collections.size()).arg(result.lima.bnvEvents.size());
    if (!result.lima.bnvEventsCount.empty()) { out += "\nBNV по типам:\n"; for (const auto& [name,count] : result.lima.bnvEventsCount) out += QString("  %1: %2\n").arg(q(name)).arg(count); }
    const auto w2 = result.w2Frontend.getStats();
    out += QString("\nW2 FRONTEND\n-----------\nСессий: %1\nЗапрошено: %2\nВыдано: %3\nПодозрительных: %4\n").arg(w2.total).arg(money(w2.totalRequestedCents),money(w2.totalDispensedCents)).arg(w2.suspiciousTotal);
    for (const auto& [status,count] : w2.statusCounts) out += QString("  %1: %2\n").arg(q(fluxus::w2StatusDisplay(status))).arg(count);
    if (!w2.denomDispensed.empty()) { out += "Выдача по номиналам:\n"; for (const auto& [denom,count] : w2.denomDispensed) out += QString("  %1 × %2\n").arg(denom).arg(count); }
    out += QString("\nДенежные события: %1\n").arg(result.w2Frontend.cashEvents.size());
    for (const auto& event : result.w2Frontend.cashEvents) out += QString("  %1 %2 — %3: %4\n").arg(q(event.dateString()),q(event.timeString()),q(event.typeDisplay()),money(event.totalCents));
    const auto ll = result.w2LowLevel.getStats();
    out += QString("\nW2 LOW-LEVEL\n------------\nСессий: %1\nHealth: %2 (изменений %3)\n").arg(ll.total).arg(ll.healthRecords).arg(ll.healthChanges);
    for (const auto& [model,count] : ll.byModel) out += QString("  %1: %2\n").arg(q(model)).arg(count);
    for (const auto& [status,count] : ll.statusCounts) out += QString("  %1: %2\n").arg(q(status)).arg(count);
    text->setPlainText(out); layout->addWidget(text); return page;
}

QWidget* AnalysisResultWindow::lifecycleTab(const AnalysisResult& result) {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); auto* filters = new QHBoxLayout; auto* combo = new QComboBox;
    combo->addItems({"Все статусы","SUCCESS","INCOMPLETE","ERROR","SUSPICIOUS"}); filters->addWidget(new QLabel("Фильтр:")); filters->addWidget(combo); filters->addStretch(); layout->addLayout(filters);
    auto* tree = makeTree({"ID","Дата","Время","Статус","Номинал","Валюта","Устройство","Длительность"}); layout->addWidget(tree,1);
    auto refresh = [=] {
        tree->clear(); const QString filter = combo->currentIndex() ? combo->currentText() : "";
        for (const auto& session : result.lima.sessions) {
            if (!filter.isEmpty() && q(session.status) != filter) continue;
            auto* item = new QTreeWidgetItem(tree,{QString::number(session.sessionId),q(session.firstDate),q(session.firstTime),q(session.status),session.denomination?QString::number(*session.denomination):"?",session.currency?q(*session.currency):"?",q(session.deviceModel+" / "+session.deviceSerial),QString::number(session.events.size())+" событий"});
            if (session.status != "SUCCESS") item->setForeground(3,QBrush("#f59e0b"));
            for (const auto& event : session.events) child(item,{q(eventString(event,"time")),q(eventString(event,"date")),"",q(eventString(event,"event")),eventText(event)});
            if (!session.errorReason.empty()) child(item,{"","","","Причина",q(session.errorReason)});
        }
    };
    connect(combo,&QComboBox::currentTextChanged,page,[refresh](const QString&){refresh();}); refresh(); return page;
}

QWidget* AnalysisResultWindow::bnvTab(const AnalysisResult& result) {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); auto* row = new QHBoxLayout; auto* warnings = new QCheckBox("Только предупреждения и ошибки"); row->addWidget(warnings); row->addStretch(); layout->addLayout(row);
    auto* tree = makeTree({"Дата","Время","Тип","Событие","Детали"}); layout->addWidget(tree,1);
    auto refresh = [=] {
        tree->clear();
        for (const auto& event : result.lima.bnvEvents) {
            const QString name = q(eventString(event,"event",eventString(event,"type"))); const QString raw = eventText(event);
            const bool warning = raw.contains("error",Qt::CaseInsensitive)||raw.contains("reject",Qt::CaseInsensitive)||raw.contains("warning",Qt::CaseInsensitive)||raw.contains("fail",Qt::CaseInsensitive);
            if (warnings->isChecked() && !warning) continue;
            auto* item = new QTreeWidgetItem(tree,{q(eventString(event,"date")),q(eventString(event,"time")),q(eventString(event,"device_type")),name,raw});
            if (warning) item->setForeground(3,QBrush("#ef4444"));
        }
    };
    connect(warnings,&QCheckBox::toggled,page,[refresh](bool){refresh();}); refresh(); return page;
}

QWidget* AnalysisResultWindow::w2Tab(const AnalysisResult& result) {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); auto* filters = new QHBoxLayout; auto* combo = new QComboBox;
    combo->addItems({"Все сессии","Только успешные","Только ошибки","Только подозрительные"}); filters->addWidget(combo); filters->addStretch(); layout->addLayout(filters);
    auto* tree = makeTree({"TRX","Дата","Время","Статус","Запрошено","Выдано","Детали"}); layout->addWidget(tree,1);
    auto refresh = [=] {
        tree->clear(); const int mode = combo->currentIndex();
        for (const auto& session : result.w2Frontend.sessions) {
            const bool error = session->status != "SUCCESS";
            if ((mode==1&&error)||(mode==2&&!error)||(mode==3&&!session->hasSuspiciousMatch())) continue;
            auto* item = new QTreeWidgetItem(tree,{q(session->trx),q(session->startDateString),q(session->startTimeString),q(fluxus::w2StatusDisplay(session->status)),money(session->requestedAmount),money(session->dispensedAmount),QString("Повторов: %1").arg(session->retryCount)});
            if (session->hasSuspiciousMatch()) item->setForeground(3,QBrush("#ef4444")); else if(error)item->setForeground(3,QBrush("#f59e0b"));
            if (!session->userLogin.empty()) child(item,{"Пользователь","","","", "", "",q(session->userLogin)});
            if (!session->coupon.empty()) child(item,{"Купон","","","","","",q(session->coupon)});
            if (!session->errorDetail.empty()) child(item,{"Ошибка","","","","","",q(session->errorDetail)});
            for (const auto& flags : session->suspicionFlags) { QStringList text; for(const auto& flag:flags)text<<q(flag); child(item,{"⚠ Корреляция","","","","","",text.join(" | ")}); }
            for (const auto& ll : session->lowLevelSessions) {
                auto* hardware = child(item,{q(ll->idx),q(ll->startDateString),q(ll->startTimeString),q(ll->status),QString::number(ll->totalRequested()),QString::number(ll->totalDispensed()),q(ll->model)});
                for (const auto& event : ll->commands) child(hardware,{"CMD",q(eventString(event,"time")),"","", "", "",eventText(event)});
                for (const auto& event : ll->results) child(hardware,{"RESULT",q(eventString(event,"time")),"","", "", "",eventText(event)});
                for (const auto& event : ll->events) child(hardware,{"EVENT",q(eventString(event,"time")),"","", "", "",eventText(event)});
            }
            for (const auto& event : session->events) child(item,{q(eventString(event,"type")),q(eventString(event,"date")),q(eventString(event,"time")),"","","",eventText(event)});
        }
    };
    connect(combo,&QComboBox::currentTextChanged,page,[refresh](const QString&){refresh();}); refresh(); return page;
}

QWidget* AnalysisResultWindow::lowLevelTab(const AnalysisResult& result) {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); auto* combo = new QComboBox; combo->addItems({"Все сессии","Успешные","Ошибки / незавершённые"});
    auto* row = new QHBoxLayout; row->addWidget(combo); row->addStretch(); layout->addLayout(row); auto* tree = makeTree({"IDX / модель","Дата","Время","Статус","Запрошено","Выдано","Отклонено / детали"}); layout->addWidget(tree,1);
    auto refresh = [=] {
        tree->clear();
        for (const auto& session : result.w2LowLevel.sessions) {
            const bool success = session->status == "SUCCESS"; if ((combo->currentIndex()==1&&!success)||(combo->currentIndex()==2&&success))continue;
            auto* item = new QTreeWidgetItem(tree,{q(session->idx+" / "+session->model),q(session->startDateString),q(session->startTimeString),q(session->status),QString::number(session->totalRequested()),QString::number(session->totalDispensed()),QString::number(session->totalRejected())});
            if(!success)item->setForeground(3,QBrush("#ef4444"));
            if(session->lcdmStatusCode)child(item,{"LCDM status","","","", "", "",QString::number(*session->lcdmStatusCode)});
            for(const auto& event:session->commands)child(item,{"CMD",q(eventString(event,"date")),q(eventString(event,"time")),"","","",eventText(event)});
            for(const auto& event:session->results)child(item,{"RESULT",q(eventString(event,"date")),q(eventString(event,"time")),"","","",eventText(event)});
            for(const auto& event:session->events)child(item,{"EVENT",q(eventString(event,"date")),q(eventString(event,"time")),"","","",eventText(event)});
        }
    };
    connect(combo,&QComboBox::currentTextChanged,page,[refresh](const QString&){refresh();}); refresh(); return page;
}

QWidget* AnalysisResultWindow::healthTab(const AnalysisResult& result) {
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page); auto* combo = new QComboBox; combo->addItems({"Все состояния","Только изменения"}); auto* row = new QHBoxLayout; row->addWidget(combo); row->addStretch(); layout->addLayout(row);
    auto* tree = makeTree({"Тип","Дата","Время","Состояние"}); layout->addWidget(tree,1);
    auto refresh = [=] {
        tree->clear(); const auto& records = combo->currentIndex() ? result.w2LowLevel.lcdmHealthChanges : result.w2LowLevel.lcdmHealth;
        for (const auto& record : records) {
            auto* item = new QTreeWidgetItem(tree,{q(record.kind),q(eventString(record.state,"date")),q(eventString(record.state,"time")),q(record.state.dump())});
            if(combo->currentIndex())item->setForeground(0,QBrush("#f59e0b"));
        }
    };
    connect(combo,&QComboBox::currentTextChanged,page,[refresh](const QString&){refresh();}); refresh(); return page;
}

void AnalysisResultWindow::saveAnalysis(bool all) {
    if (data_.empty()) return;
    if (!all && data_.size() > 1) {
        QStringList names; for(const auto& [id,result]:data_)names<<id;
        bool ok=false; const QString selected=QInputDialog::getItem(this,"Сохранить анализ","Устройство:",names,0,false,&ok);if(!ok)return;
        const auto path=QFileDialog::getSaveFileName(this,"Сохранить .anl.gz",selected+".anl.gz","Fluxus analysis (*.anl.gz)");if(path.isEmpty())return;
        try{fluxus::AnalysisSerializer::save(path.toStdString(),*data_.at(selected));QMessageBox::information(this,"Сохранено","Анализ сохранён:\n"+path);}catch(const std::exception& e){QMessageBox::critical(this,"Ошибка",e.what());}return;
    }
    if (data_.size()==1) {
        const auto& [id,result]=*data_.begin();const auto path=QFileDialog::getSaveFileName(this,"Сохранить .anl.gz",id+".anl.gz","Fluxus analysis (*.anl.gz)");if(path.isEmpty())return;
        try{fluxus::AnalysisSerializer::save(path.toStdString(),*result);QMessageBox::information(this,"Сохранено","Анализ сохранён:\n"+path);}catch(const std::exception& e){QMessageBox::critical(this,"Ошибка",e.what());}return;
    }
    const auto folder=QFileDialog::getExistingDirectory(this,"Папка для сохранения анализов");if(folder.isEmpty())return;
    int saved=0;try{for(const auto& [id,result]:data_){fluxus::AnalysisSerializer::save((folder+"/"+id+".anl.gz").toStdString(),*result);++saved;}QMessageBox::information(this,"Сохранено",QString("Сохранено анализов: %1").arg(saved));}catch(const std::exception& e){QMessageBox::critical(this,"Ошибка",e.what());}
}
