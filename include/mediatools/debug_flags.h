#ifndef MEDIATOOLS_DEBUG_FLAGS_H
#define MEDIATOOLS_DEBUG_FLAGS_H

#include <QString>
#include <QHash>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QDateTime>

namespace MediaTools {

class DebugFlags {
public:
    static DebugFlags& instance();

    bool isEnabled(const QString& category) const;
    void enable(const QString& category);
    void disable(const QString& category);
    void setEnabled(const QString& category, bool enabled);
    void loadFromFile(const QString& filePath);
    void saveToFile(const QString& filePath);
    void printStatus() const;
    bool containsFilter(const QString& message) const;
    static void logStats(const QString& message);
    void loadFilters();

    static QMutex s_logMutex;

private:
    DebugFlags();
    QHash<QString, bool> flags;
    QStringList m_filters;
};

} // namespace MediaTools

#define DEBUG_ENABLED(category) MediaTools::DebugFlags::instance().isEnabled(category)

#define CONDITIONAL_DEBUG(category, message) \
    if (DEBUG_ENABLED(category)) { \
        QString buffer; \
        QDebug debug(&buffer); \
        debug << message; \
        QString cleanBuffer = buffer; \
        cleanBuffer.remove('"'); \
        cleanBuffer.remove('{'); \
        cleanBuffer.remove('}'); \
        if (MediaTools::DebugFlags::instance().containsFilter(cleanBuffer)) { \
            QMutexLocker locker(&MediaTools::DebugFlags::s_logMutex); \
            QDebug(QtDebugMsg).noquote() << message; \
        } \
    }

#define TRANSCODE_STATS_LOG(message) \
    do { \
        if (DEBUG_ENABLED("transcode_steps_stats")) { \
            QString _buf; \
            QDebug(&_buf).noquote() << message; \
            MediaTools::DebugFlags::logStats(_buf.trimmed()); \
        } \
    } while (0)

#endif // MEDIATOOLS_DEBUG_FLAGS_H
