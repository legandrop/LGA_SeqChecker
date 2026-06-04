#include "mediatools/AppPathManager.h"
#include "mediatools/debug_flags.h"

QMap<QString, bool> AppPathManager::s_buildModeCache;
QMap<QString, QString> AppPathManager::s_pythonRuntimePaths;
QMap<QString, QString> AppPathManager::s_pythonScriptPaths;
QMap<QString, QString> AppPathManager::s_logsDirectories;
QMap<QString, QString> AppPathManager::s_logFilePaths;

void AppPathManager::logMessage(const QString& message) {
    CONDITIONAL_DEBUG("AppPathManager", message);
}

bool AppPathManager::isInBuildMode(const QString& appDir) {
    if (s_buildModeCache.contains(appDir)) {
        return s_buildModeCache[appDir];
    }
    bool isInBuild = appDir.contains("build", Qt::CaseInsensitive);
    s_buildModeCache[appDir] = isInBuild;
    return isInBuild;
}
