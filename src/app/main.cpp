#include "App.h"
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Fluxus");
    QApplication::setApplicationVersion(appVersion());
    QApplication::setOrganizationName("Fluxus");
    applyDarkTheme(app);
    qRegisterMetaType<std::shared_ptr<AnalysisResult>>();
    qRegisterMetaType<AnalysisData>();
    try {
        auto& config = fluxus::Config::instance();
        config.setDataDirectory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString());
        QDir().mkpath(q(config.dataDirectory().string()));
        config.load();
        MainWindow window;
        window.show();
        return QApplication::exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, "Fluxus", error.what());
        return 1;
    }
}
