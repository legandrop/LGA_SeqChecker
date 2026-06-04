#include "seqchecker/mainwindow.h"
#include "mediatools/debug_flags.h"
#include "mediatools/AppPathManager.h"
#include "mediatools/utils/LogRotation.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <QIcon>
#include <QTime>
#include <QFontDatabase>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    QApplication::setApplicationName("LGA SeqChecker");
    QApplication::setApplicationVersion(SEQCHECKER_VERSION);
    QApplication::setOrganizationName("LGA");

    // Resolver rutas de logs
    QString appPath = QCoreApplication::applicationDirPath();
    QString logDir = AppPathManager::getLogsDirectory(appPath);
    QDir().mkpath(logDir);

    QString mainLogPath = QDir(logDir).filePath("Debug.log");
    if (QFile::exists(mainLogPath)) {
        QFile::remove(mainLogPath);
    }

    // Message handler con rotación
    static QString staticLogPath = mainLogPath;
    static const qint64 MAX_LOG_SIZE = 10 * 1024 * 1024;
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        static QFile logFile(staticLogPath);
        if (!logFile.isOpen()) {
            if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
                return;
        }
        LogRotation::checkAndTruncateBeforeWrite(logFile, MAX_LOG_SIZE);
        QTextStream stream(&logFile);
        stream << QTime::currentTime().toString("HH:mm:ss.zzz") << " ";
        switch (type) {
            case QtInfoMsg:     stream << "INFO: ";     break;
            case QtWarningMsg:  stream << "WARNING: ";  break;
            case QtCriticalMsg: stream << "CRITICAL: "; break;
            case QtFatalMsg:    stream << "FATAL: ";    break;
            default: break;
        }
        stream << msg << "\n";
        stream.flush();
    });

    CONDITIONAL_DEBUG("core", "________________________________________________________");
    CONDITIONAL_DEBUG("core", "LGA SeqChecker - Iniciando");
    CONDITIONAL_DEBUG("core", "________________________________________________________");

    // Cargar fuentes
    QStringList fontFiles = {
        ":/fonts/Inter_18pt-Regular.ttf",
        ":/fonts/Inter_18pt-Medium.ttf",
        ":/fonts/Inter_18pt-Bold.ttf",
        ":/fonts/Inter_18pt-SemiBold.ttf",
        ":/fonts/Inter_18pt-Light.ttf",
        ":/fonts/Inter-Regular.ttf",
        ":/fonts/Inter-Medium.ttf",
        ":/fonts/Inter-Bold.ttf",
        ":/fonts/Roboto-Regular.ttf",
        ":/fonts/Roboto-Medium.ttf",
        ":/fonts/Roboto-Bold.ttf",
        ":/fonts/georgia.ttf",
    };
    for (const QString& fontFile : fontFiles) {
        QFontDatabase::addApplicationFont(fontFile);
    }

    QIcon appIcon(":/icons/SeqChecker.svg");
    app.setWindowIcon(appIcon);

    MainWindow w;
    w.show();

    return app.exec();
}
