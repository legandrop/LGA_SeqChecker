#ifndef APPPATHMANAGER_H
#define APPPATHMANAGER_H

#include <QString>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QMap>

class AppPathManager {
public:
    static QString getPythonExecutableName() {
#ifdef Q_OS_WIN
        return "python.exe";
#else
        return "python3";
#endif
    }

    static QString getPythonRuntimePath(const QString& appDir) {
        if (s_pythonRuntimePaths.contains(appDir)) {
            return s_pythonRuntimePaths[appDir];
        }

#ifdef Q_OS_WIN
        QString osFolder = "windows";
        QString pythonExe = getPythonExecutableName();
#else
        QString osFolder = "macos/python3";
        QString pythonExe = getPythonExecutableName();
#endif

        bool isInBuild = isInBuildMode(appDir);
        QStringList possiblePaths;

#ifdef Q_OS_WIN
        if (isInBuild) {
            possiblePaths << QDir::cleanPath(QDir(appDir).absoluteFilePath("../python_runtime/" + osFolder + "/" + pythonExe));
            possiblePaths << QDir::cleanPath(QDir(appDir).absoluteFilePath("../python_runtime/" + pythonExe));
            possiblePaths << QDir(appDir).filePath("python_runtime/" + osFolder + "/" + pythonExe);
        } else {
            possiblePaths << QDir(appDir).filePath("python_runtime/" + osFolder + "/" + pythonExe);
            possiblePaths << QDir(appDir).filePath("python_runtime/" + pythonExe);
            possiblePaths << QDir::cleanPath(QDir(appDir).absoluteFilePath("../python_runtime/" + osFolder + "/" + pythonExe));
        }
#endif

        for (const QString &path : possiblePaths) {
            if (QFileInfo(path).exists()) {
                QString nativePath = QDir::toNativeSeparators(path);
                s_pythonRuntimePaths[appDir] = nativePath;
                return nativePath;
            }
        }

        QString fallback = QDir::toNativeSeparators(possiblePaths.first());
        s_pythonRuntimePaths[appDir] = fallback;
        return fallback;
    }

    static QString getPythonScriptPath(const QString& appDir, const QString& scriptName) {
        QString cacheKey = appDir + "|" + scriptName;
        if (s_pythonScriptPaths.contains(cacheKey)) {
            return s_pythonScriptPaths[cacheKey];
        }

        bool isInBuild = isInBuildMode(appDir);
        QStringList possiblePaths;

        if (isInBuild) {
            possiblePaths << QDir::cleanPath(QDir(appDir).absoluteFilePath("../py_scr/" + scriptName));
            possiblePaths << QDir(appDir).filePath("py_scr/" + scriptName);
        } else {
            possiblePaths << QDir(appDir).filePath("py_scr/" + scriptName);
            possiblePaths << QDir::cleanPath(QDir(appDir).absoluteFilePath("../py_scr/" + scriptName));
        }

        for (const QString &path : possiblePaths) {
            if (QFileInfo(path).exists()) {
                QString nativePath = QDir::toNativeSeparators(path);
                s_pythonScriptPaths[cacheKey] = nativePath;
                return nativePath;
            }
        }

        QString fallback = QDir::toNativeSeparators(possiblePaths.first());
        s_pythonScriptPaths[cacheKey] = fallback;
        return fallback;
    }

    static QString getLogsDirectory(const QString& appDir = QString()) {
        QString baseDir = appDir.isEmpty() ? QCoreApplication::applicationDirPath() : appDir;
        if (s_logsDirectories.contains(baseDir)) {
            return s_logsDirectories[baseDir];
        }

        QString logsPath;
        bool isInBuild = isInBuildMode(baseDir);
        if (isInBuild) {
            logsPath = QDir::cleanPath(QDir(baseDir).absoluteFilePath("../logs"));
        } else {
            logsPath = QDir(baseDir).filePath("logs");
        }

        QString nativePath = QDir::toNativeSeparators(logsPath);
        s_logsDirectories[baseDir] = nativePath;
        return nativePath;
    }

    static QString getLogFilePath(const QString& appDir = QString(), const QString& logFileName = QString()) {
        QString fileName = logFileName.isEmpty() ? "Debug.log" : logFileName;
        QString cacheKey = appDir + "|" + fileName;
        if (s_logFilePaths.contains(cacheKey)) {
            return s_logFilePaths[cacheKey];
        }

        QString logsDir = getLogsDirectory(appDir);
        QString logPath = QDir::toNativeSeparators(QDir(logsDir).filePath(fileName));
        s_logFilePaths[cacheKey] = logPath;
        return logPath;
    }

    // Obtiene la ruta al oiiotool.exe (para metadata de EXR)
    static QString getOiioToolPath(const QString& appDir) {
        bool isInBuild = isInBuildMode(appDir);
        QStringList possiblePaths;

        if (isInBuild) {
            possiblePaths << QDir::cleanPath(QDir(appDir).absoluteFilePath("../thirdparty/win/OIIO/oiiotool.exe"));
        } else {
            possiblePaths << QDir(appDir).filePath("thirdparty/win/OIIO/oiiotool.exe");
        }

        for (const QString &path : possiblePaths) {
            if (QFileInfo(path).exists()) {
                return QDir::toNativeSeparators(path);
            }
        }
        return QDir::toNativeSeparators(possiblePaths.first());
    }

    // Obtiene la ruta al ffprobe.exe
    static QString getFfprobePath(const QString& appDir) {
        bool isInBuild = isInBuildMode(appDir);
        QStringList possiblePaths;

        if (isInBuild) {
            possiblePaths << QDir::cleanPath(QDir(appDir).absoluteFilePath("../thirdparty/win/FFmpeg/bin/ffprobe.exe"));
            possiblePaths << QDir::cleanPath(QDir(appDir).absoluteFilePath("../thirdparty/win/FFmpeg/ffprobe.exe"));
        } else {
            possiblePaths << QDir(appDir).filePath("thirdparty/win/FFmpeg/bin/ffprobe.exe");
            possiblePaths << QDir(appDir).filePath("thirdparty/win/FFmpeg/ffprobe.exe");
        }

        for (const QString &path : possiblePaths) {
            if (QFileInfo(path).exists()) {
                return QDir::toNativeSeparators(path);
            }
        }
        return QDir::toNativeSeparators(possiblePaths.first());
    }

    static void clearCache() {
        s_buildModeCache.clear();
        s_pythonRuntimePaths.clear();
        s_pythonScriptPaths.clear();
        s_logsDirectories.clear();
        s_logFilePaths.clear();
    }

private:
    static void logMessage(const QString& message);
    static bool isInBuildMode(const QString& appDir);

    static QMap<QString, bool> s_buildModeCache;
    static QMap<QString, QString> s_pythonRuntimePaths;
    static QMap<QString, QString> s_pythonScriptPaths;
    static QMap<QString, QString> s_logsDirectories;
    static QMap<QString, QString> s_logFilePaths;
};

#endif // APPPATHMANAGER_H
