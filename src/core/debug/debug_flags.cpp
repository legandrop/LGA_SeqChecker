#include "mediatools/debug_flags.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

namespace MediaTools {

QMutex DebugFlags::s_logMutex;

DebugFlags& DebugFlags::instance() {
    static DebugFlags instance;
    return instance;
}

DebugFlags::DebugFlags() {
    // -- Core --
    flags["error"] = true;
    flags["warning"] = true;
    flags["core"] = false;
    flags["AppPathManager"] = false;

    // -- Import / Drag & Drop --
    flags["import"] = false;

    // -- UI --
    flags["ui"] = false;
    flags["fonts"] = false;
    flags["WindowResize"] = false;

    // -- Rename Tab --
    flags["rename"] = false;
    flags["rename_table"] = false;
    flags["rename_preview"] = false;
    flags["rename_ops"] = false;
    flags["folder_mismatch"] = false;

    // -- Transcode Tab --
    flags["transcode"] = false;
    flags["transcode_manifest"] = false;
    flags["transcode_progress"] = false;
    flags["transcode_table_widths"] = false;
    flags["transcode_expansion"] = false;
    flags["transcode_mov"] = false;
    flags["transcode_timeout"] = false;
    flags["transcode_steps_stats"] = false;

    // -- Transcode Queue Tab --
    flags["queue_table_widths"] = false;
    flags["queue_render"] = false;

    // -- App lifecycle / crash diagnostics --
    flags["app_lifecycle"] = false;

    // -- Shot detection --
    flags["shotname"] = false;

    // -- Widgets --
    flags["drag_number_edit"] = false;

    // -- Python Runner --
    flags["python_runner"] = false;
    flags["python_runner_verbose"] = false;

    // -- Custom Tooltip --
    flags["custom_tooltip"] = false;

    // Cargar desde archivo si existe
    QString appPath = QCoreApplication::applicationDirPath();
    bool isInBuild = appPath.contains("build", Qt::CaseInsensitive);

    QString configPath;
    if (isInBuild) {
        configPath = QDir::cleanPath(QDir(appPath).absoluteFilePath("../config/debug_flags.txt"));
    } else {
        configPath = QDir(appPath).filePath("config/debug_flags.txt");
    }
    configPath = QDir::toNativeSeparators(configPath);

    QFile file(configPath);
    if (file.exists()) {
        loadFromFile(configPath);
    } else {
        qDebug() << "ADVERTENCIA: No se encontró debug_flags.txt en:" << configPath;
    }

    loadFilters();
    qDebug() << "DebugFlags inicializado.";
}

bool DebugFlags::isEnabled(const QString& category) const {
    return flags.value(category, false);
}

void DebugFlags::enable(const QString& category) {
    flags[category] = true;
}

void DebugFlags::disable(const QString& category) {
    flags[category] = false;
}

void DebugFlags::setEnabled(const QString& category, bool enabled) {
    flags[category] = enabled;
}

void DebugFlags::loadFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "No se pudo abrir debug_flags.txt:" << filePath;
        return;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;

        int commentPos = line.indexOf('#');
        if (commentPos != -1) line = line.left(commentPos).trimmed();

        QStringList parts = line.split('=');
        if (parts.size() == 2) {
            QString category = parts[0].trimmed();
            QString value = parts[1].trimmed().toLower().split(' ')[0].trimmed();
            bool enabled = (value == "true" || value == "1" || value == "yes");
            flags[category] = enabled;
        }
    }
    file.close();
}

void DebugFlags::saveToFile(const QString& filePath) {
    QDir dir = QFileInfo(filePath).dir();
    if (!dir.exists()) dir.mkpath(".");

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "No se pudo guardar debug_flags.txt:" << filePath;
        return;
    }

    QTextStream out(&file);
    out << "# Flags de depuración para LGA Media Tools\n";
    out << "# Formato: categoria=true/false\n\n";

    QHashIterator<QString, bool> i(flags);
    while (i.hasNext()) {
        i.next();
        out << i.key() << "=" << (i.value() ? "true" : "false") << "\n";
    }
    file.close();
}

void DebugFlags::printStatus() const {}

void DebugFlags::logStats(const QString& message) {
    if (!instance().isEnabled("transcode_steps_stats")) {
        return;
    }
    QMutexLocker locker(&s_logMutex);

    QString appPath = QCoreApplication::applicationDirPath();
    bool isInBuild = appPath.contains("build", Qt::CaseInsensitive);
    QString logDir;
    if (isInBuild) {
        logDir = QDir::cleanPath(QDir(appPath).absoluteFilePath("../logs"));
    } else {
        logDir = QDir(appPath).filePath("logs");
    }
    QDir().mkpath(logDir);
    QString logPath = QDir(logDir).filePath("debug_transcode_steps_stats.log");

    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") << " | " << message << "\n";
    stream.flush();
    file.close();
}

void DebugFlags::loadFilters() {
    QString appPath = QCoreApplication::applicationDirPath();
    bool isInBuild = appPath.contains("build", Qt::CaseInsensitive);

    QString configPath;
    if (isInBuild) {
        configPath = QDir::cleanPath(QDir(appPath).absoluteFilePath("../config/debug_filters.txt"));
    } else {
        configPath = QDir(appPath).filePath("config/debug_filters.txt");
    }
    configPath = QDir::toNativeSeparators(configPath);

    QFile file(configPath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty() && !line.startsWith("#")) {
                m_filters.append(line);
            }
        }
        file.close();
    }
}

bool DebugFlags::containsFilter(const QString& message) const {
    if (m_filters.isEmpty()) return true;
    for (const QString& filter : m_filters) {
        if (message.contains(filter)) return true;
    }
    return false;
}

} // namespace MediaTools
