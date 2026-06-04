#ifndef LOGROTATION_H
#define LOGROTATION_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QMutex>

class LogRotation
{
public:
    static bool truncateLogIfNeeded(const QString &logFilePath, qint64 maxSizeBytes = 10 * 1024 * 1024);
    static void checkAndTruncateBeforeWrite(QFile &logFile, qint64 maxSizeBytes = 10 * 1024 * 1024);

private:
    static QMutex s_mutex;
};

#endif // LOGROTATION_H
