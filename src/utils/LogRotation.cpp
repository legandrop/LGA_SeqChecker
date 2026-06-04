#include "mediatools/utils/LogRotation.h"
#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>

QMutex LogRotation::s_mutex;

bool LogRotation::truncateLogIfNeeded(const QString &logFilePath, qint64 maxSizeBytes)
{
    QMutexLocker locker(&s_mutex);

    QFileInfo fileInfo(logFilePath);
    if (!fileInfo.exists() || fileInfo.size() <= maxSizeBytes) {
        return false;
    }

    QFile logFile(logFilePath);
    if (!logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&logFile);
    QStringList lines;
    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    logFile.close();

    qint64 targetSize = static_cast<qint64>(maxSizeBytes * 0.9);
    qint64 currentSize = 0;
    QStringList linesToKeep;

    for (int i = lines.size() - 1; i >= 0; --i) {
        qint64 lineSize = lines[i].toUtf8().size() + 1;
        if (currentSize + lineSize > targetSize) break;
        linesToKeep.prepend(lines[i]);
        currentSize += lineSize;
    }

    QString timestamp = QTime::currentTime().toString("HH:mm:ss.zzz");
    QString truncateMsg = QString("%1 [INFO] Log truncado - mantenidas las últimas líneas (límite: %2 MB)")
                              .arg(timestamp)
                              .arg(maxSizeBytes / (1024.0 * 1024.0), 0, 'f', 1);
    linesToKeep.prepend(truncateMsg);

    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&logFile);
    for (const QString &line : linesToKeep) {
        out << line << "\n";
    }
    out.flush();
    logFile.close();
    return true;
}

void LogRotation::checkAndTruncateBeforeWrite(QFile &logFile, qint64 maxSizeBytes)
{
    if (logFile.isOpen() && logFile.size() > maxSizeBytes) {
        QString filePath = logFile.fileName();
        logFile.close();
        truncateLogIfNeeded(filePath, maxSizeBytes);
        logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
}
