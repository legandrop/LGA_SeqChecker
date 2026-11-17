#include "mediatools/RunMode.h"

#include <QDebug>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QString>

#include <atomic>

// Copia de LGA_Base_QT_C_Py/src/utils/RunMode.cpp. Criterio y comentarios en RunMode.h.

namespace {
std::atomic_bool g_captureMode{false};
}

namespace LgaRunMode {

void setCaptureMode(bool on)
{
    g_captureMode.store(on);
}

bool isCaptureMode()
{
    return g_captureMode.load();
}

bool isHeadlessPlatform()
{
    // platformName() es estatica y da vacio mientras no exista la QGuiApplication (o en un
    // proceso que solo tiene QCoreApplication): ahi manda la variable de entorno.
    QString name = QGuiApplication::platformName();
    if (name.isEmpty()) {
        name = qEnvironmentVariable("QT_QPA_PLATFORM");
    }
    // QT_QPA_PLATFORM admite parametros despues de ':' ("offscreen:size=1920x1080").
    name = name.section(QLatin1Char(':'), 0, 0).trimmed().toLower();
    return name == QLatin1String("offscreen") || name == QLatin1String("minimal");
}

bool canPersistSystemState()
{
    return !isCaptureMode() && !isHeadlessPlatform();
}

bool canPersistConfig()
{
    if (QStandardPaths::isTestModeEnabled()) {
        return true;
    }
    return canPersistSystemState();
}

QString nonPersistReason()
{
    if (canPersistConfig()) {
        return QString();
    }
    if (isCaptureMode()) {
        return QStringLiteral("es un modo de QA o de captura");
    }
    return QStringLiteral("la plataforma de Qt no tiene pantalla (%1)")
        .arg(QGuiApplication::platformName().isEmpty() ? qEnvironmentVariable("QT_QPA_PLATFORM")
                                                       : QGuiApplication::platformName());
}

void logIfNotPersisting()
{
    static std::atomic_bool logged{false};
    const QString reason = nonPersistReason();
    if (reason.isEmpty() || logged.exchange(true)) {
        return;
    }
    qInfo().noquote() << QString("[RunMode] Esta corrida NO guarda configuracion (%1): geometria "
                                 "de ventana, settings y registro compartido quedan como estaban.")
                             .arg(reason);
}

} // namespace LgaRunMode
