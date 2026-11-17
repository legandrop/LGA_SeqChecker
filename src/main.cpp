#include "seqchecker/mainwindow.h"
#include "mediatools/debug_flags.h"
#include "mediatools/AppPathManager.h"
#include "mediatools/BuildTree.h"
#include "mediatools/LgaHeadlessGuard.h"
#include "mediatools/RunMode.h"
#include "mediatools/utils/LogRotation.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPixmap>
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <QIcon>
#include <QTime>
#include <QFontDatabase>
#include <QCoreApplication>

#include <cstdio>
#include <utility>

namespace {

// --ui-shot <salida.png>: dibuja la ventana principal REAL a un PNG sin mostrarla, a 1100 x 640
// (el tamano inicial de la ventana), y lista la fuente pedida -> resuelta de cada texto.
// Sale con 0 si guardo, 1 si no pudo capturar o guardar, 2 si los argumentos no valen. La salida
// tiene que ser un .png nuevo en una carpeta que exista: un archivo existente no se pisa.
// Es un modo de captura: main() ya puso LgaRunMode::setCaptureMode(true), asi que no se guarda
// geometria ni ninguna otra config (ver RunMode.h).
int runUiShot(const QStringList &args, int idx)
{
    if (idx + 1 >= args.size()) {
        std::fprintf(stderr, "usage: --ui-shot <out.png>\n");
        return 2;
    }
    const QString outPath = args.at(idx + 1);
    const QFileInfo outInfo(outPath);
    if (!outPath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive) || outInfo.exists()
        || !outInfo.absoluteDir().exists()) {
        std::fprintf(stderr, "ui-shot: output must be a new .png in an existing folder\n");
        return 2;
    }

    MainWindow window;
    // Tamano fijo: la geometria restaurada de la config del usuario haria que dos capturas no
    // fueran comparables.
    window.resize(1100, 640);
    window.ensurePolished();
    QApplication::processEvents();
    const QPixmap shot = window.grab();
    if (shot.isNull() || !shot.save(outPath, "PNG")) {
        std::fprintf(stderr, "ui-shot: could not save %s\n", qPrintable(outPath));
        return 1;
    }

    // Fuente EFECTIVA de cada texto: haber registrado un .ttf no dice con que fuente se dibujo.
    // Una linea por combinacion pedida -> resuelta (familia, peso, tamano), por tipo de control.
    QStringList fonts;
    const QList<QWidget *> widgets = window.findChildren<QWidget *>();
    for (const QWidget *widget : widgets) {
        const char *kind = qobject_cast<const QLabel *>(widget)            ? "label"
                           : qobject_cast<const QAbstractButton *>(widget) ? "button"
                           : qobject_cast<const QHeaderView *>(widget)     ? "header"
                                                                           : nullptr;
        if (!kind) {
            continue;
        }
        const QFont asked = widget->font();
        const QFontInfo got(asked);
        const QString line = QStringLiteral("%1 %2/%3/%4px/%5pt -> %6/%7 (%8)")
                                 .arg(QLatin1String(kind))
                                 .arg(asked.family())
                                 .arg(asked.weight())
                                 .arg(asked.pixelSize())
                                 .arg(asked.pointSizeF())
                                 .arg(got.family())
                                 .arg(got.weight())
                                 .arg(got.styleName());
        if (!fonts.contains(line)) {
            fonts.append(line);
        }
    }
    fonts.sort();
    for (const QString &line : std::as_const(fonts)) {
        std::fprintf(stdout, "ui-shot font %s\n", qPrintable(line));
    }
    std::fprintf(stdout, "ui-shot ok %s size=%dx%d dpr=%.2f platform=%s persist=%d\n",
                 qPrintable(outPath), shot.width(), shot.height(), shot.devicePixelRatio(),
                 qPrintable(QGuiApplication::platformName()),
                 LgaRunMode::canPersistConfig() ? 1 : 0);
    std::fflush(stdout);

    // Cerrar dispara closeEvent(), que guarda la geometria: con el modo de captura puesto no
    // escribe nada.
    window.close();
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    // ANTES de QApplication: si se pidio una plataforma de Qt (QT_QPA_PLATFORM=offscreen, como en
    // toda corrida sin pantalla) y falta su plugin, Qt abre un cartel modal desde adentro del
    // constructor. El guard sale por stderr con un codigo; sin plataforma pedida no decide nada.
    if (const int guard = LgaHeadlessGuard::check(argc, argv)) {
        return guard;
    }
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    QApplication::setApplicationName("LGA SeqChecker");
    QApplication::setApplicationVersion(SEQCHECKER_VERSION);
    QApplication::setOrganizationName("LGA");

    // Modo de captura. Se reconoce ACA, antes de cualquier cosa que pueda escribir config: desde
    // esta linea LgaRunMode::canPersistConfig() ya contesta "no" para toda la corrida.
    const QStringList args = app.arguments();
    const int uiShotIdx = args.indexOf(QStringLiteral("--ui-shot"));
    if (uiShotIdx >= 0) {
        LgaRunMode::setCaptureMode(true);
    }

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
        // En el heap y sin destruir, a proposito: Qt sigue mandando mensajes DESPUES de que
        // main() devuelve (la descarga de plugins, por ejemplo), cuando un `static QFile` de esta
        // funcion ya fue destruido. Escribir sobre el objeto destruido terminaba la app con una
        // violacion de acceso al salir. El sistema cierra el archivo al terminar el proceso.
        static QFile *const logFilePtr = new QFile(staticLogPath);
        QFile &logFile = *logFilePtr;
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
    // La primera consulta a LgaBuildTree (la carpeta de logs, arriba) ocurre antes del handler, asi
    // que el aviso de un LGA_BUILD_TREE invalido no llego al log. Se repite aca, ya con el handler.
    LgaBuildTree::warnIfInvalidOverride();
    // Una corrida que no persiste lo dice UNA vez, con el motivo. Va aca, ya con el handler.
    LgaRunMode::logIfNotPersisting();

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
    };
    for (const QString& fontFile : fontFiles) {
        QFontDatabase::addApplicationFont(fontFile);
    }

    QIcon appIcon(":/icons/SeqChecker.svg");
    app.setWindowIcon(appIcon);

    if (uiShotIdx >= 0) {
        return runUiShot(args, uiShotIdx);
    }

    MainWindow w;
    w.show();

    return app.exec();
}
