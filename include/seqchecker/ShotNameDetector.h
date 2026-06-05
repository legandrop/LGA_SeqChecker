#ifndef SHOTNAMEDETECTOR_H
#define SHOTNAMEDETECTOR_H

#include <QString>
#include <QVector>

class ShotNameDetector
{
public:
    struct ItemInput {
        QString displayName;
        QString baseName;
        QString absolutePath;
        bool isSequence = false;
    };

    struct Detection {
        QString shotName;
        double confidence = 0.0;
        QString strategy;
        QString reason;
        bool strong = false;
        bool usedConsensus = false;

        bool isUnknown() const;
    };

    struct BatchResult {
        QVector<Detection> perItem;
        QString consensusShot;
        double consensusSupport = 0.0;
        bool consensusAccepted = false;
    };

    inline static constexpr const char *SHOT_COLOR_PRIMARY = "#B56AB5";
    inline static constexpr const char *SHOT_COLOR_ALT = "#6AB5CA";
    inline static constexpr const char *UNKNOWN_SHOT = "Unknown";

    static Detection detectSingle(const ItemInput &item, const QString &context = QString());
    static BatchResult detectBatch(const QVector<ItemInput> &items, const QString &context = QString());

    static QString shotColorForBlock(int blockIndex);

private:
    static QString cleanBaseName(const ItemInput &item);
    static Detection detectFromFilename(const ItemInput &item, const QString &cleanBase);
    static Detection detectFromGenericTokens(const ItemInput &item, const QString &cleanBase);
    static Detection detectFromPathHint(const ItemInput &item);

    static QString normalizeShotCandidate(const QString &candidate);
    static bool isVersionToken(const QString &token);
    static bool isGenericPathToken(const QString &token);
    static bool looksStrongProjectToken(const QString &token);
};

#endif // SHOTNAMEDETECTOR_H
