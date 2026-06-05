#include "seqchecker/ShotNameDetector.h"
#include "mediatools/debug_flags.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

constexpr double kConsensusSupportThreshold = 0.60;
constexpr double kConsensusMarginThreshold = 0.20;
constexpr int kConsensusMinItems = 2;

struct CandidateScore {
    double weight = 0.0;
    int count = 0;
};

bool startsWithDigit(const QString &token)
{
    return !token.isEmpty() && token.at(0).isDigit();
}

QString detectionInputName(const ShotNameDetector::ItemInput &item)
{
    if (!item.displayName.trimmed().isEmpty()) {
        return item.displayName.trimmed();
    }
    if (!item.baseName.trimmed().isEmpty()) {
        return item.baseName.trimmed();
    }
    if (!item.absolutePath.trimmed().isEmpty()) {
        return QFileInfo(item.absolutePath).fileName();
    }
    return QString();
}

ShotNameDetector::Detection makeUnknown(const QString &reason)
{
    ShotNameDetector::Detection out;
    out.shotName = QString::fromLatin1(ShotNameDetector::UNKNOWN_SHOT);
    out.confidence = 0.0;
    out.strategy = QStringLiteral("unknown");
    out.reason = reason;
    out.strong = false;
    out.usedConsensus = false;
    return out;
}

} // namespace

bool ShotNameDetector::Detection::isUnknown() const
{
    const QString trimmed = shotName.trimmed();
    return trimmed.isEmpty()
        || trimmed.compare(QString::fromLatin1(ShotNameDetector::UNKNOWN_SHOT), Qt::CaseInsensitive) == 0;
}

QString ShotNameDetector::shotColorForBlock(int blockIndex)
{
    return (blockIndex % 2 == 0)
        ? QString::fromLatin1(SHOT_COLOR_PRIMARY)
        : QString::fromLatin1(SHOT_COLOR_ALT);
}

ShotNameDetector::Detection ShotNameDetector::detectSingle(const ItemInput &item, const QString &context)
{
    Q_UNUSED(context);

    const QString cleanBase = cleanBaseName(item);
    if (cleanBase.isEmpty()) {
        return makeUnknown(QStringLiteral("empty_clean_base"));
    }

    const Detection filenameResult = detectFromFilename(item, cleanBase);
    if (!filenameResult.isUnknown()) {
        return filenameResult;
    }

    const Detection genericResult = detectFromGenericTokens(item, cleanBase);
    if (!genericResult.isUnknown()) {
        return genericResult;
    }

    const Detection pathResult = detectFromPathHint(item);
    if (!pathResult.isUnknown()) {
        return pathResult;
    }

    return makeUnknown(QStringLiteral("no_rule_matched"));
}

ShotNameDetector::BatchResult ShotNameDetector::detectBatch(const QVector<ItemInput> &items, const QString &context)
{
    BatchResult out;
    out.perItem.reserve(items.size());

    const QString debugContext = context.trimmed().isEmpty()
        ? QStringLiteral("ShotNameDetector")
        : context.trimmed();

    for (int i = 0; i < items.size(); ++i) {
        Detection det = detectSingle(items.at(i), debugContext);
        out.perItem.push_back(det);

        CONDITIONAL_DEBUG(
            "shotname",
            "[ShotNameDetector]"
                << debugContext
                << "item" << i
                << "input=" << detectionInputName(items.at(i))
                << "shot=" << det.shotName
                << "rule=" << det.strategy
                << "conf=" << QString::number(det.confidence, 'f', 2)
                << "reason=" << det.reason
        );
    }

    QMap<QString, CandidateScore> scores;
    double totalWeight = 0.0;

    for (int i = 0; i < out.perItem.size(); ++i) {
        const Detection &det = out.perItem.at(i);
        if (det.isUnknown()) {
            continue;
        }
        const double typeWeight = items.at(i).isSequence ? 1.0 : 0.6;
        const double confidence = std::max(0.0, std::min(1.0, det.confidence));
        const double contribution = typeWeight * confidence;
        if (contribution <= 0.0) {
            continue;
        }
        CandidateScore &entry = scores[det.shotName];
        entry.weight += contribution;
        entry.count += 1;
        totalWeight += contribution;
    }

    if (scores.isEmpty() || totalWeight <= 0.0) {
        CONDITIONAL_DEBUG("shotname", "[ShotNameDetector]" << debugContext << "batch_consensus=none (no_candidates)");
        return out;
    }

    QVector<QPair<QString, CandidateScore>> ranking;
    ranking.reserve(scores.size());
    for (auto it = scores.constBegin(); it != scores.constEnd(); ++it) {
        ranking.push_back(qMakePair(it.key(), it.value()));
    }
    std::sort(ranking.begin(), ranking.end(), [](const auto &a, const auto &b) {
        if (a.second.weight == b.second.weight) {
            return a.second.count > b.second.count;
        }
        return a.second.weight > b.second.weight;
    });

    const QString topShot = ranking.first().first;
    const CandidateScore top = ranking.first().second;
    const double secondWeight = (ranking.size() > 1) ? ranking.at(1).second.weight : 0.0;
    const double support = top.weight / totalWeight;
    const double margin = (top.weight - secondWeight) / totalWeight;
    const bool accepted =
        (top.count >= kConsensusMinItems)
        && (support >= kConsensusSupportThreshold)
        && (margin >= kConsensusMarginThreshold);

    out.consensusShot = topShot;
    out.consensusSupport = support;
    out.consensusAccepted = accepted;

    QStringList scoreRows;
    scoreRows.reserve(ranking.size());
    for (const auto &entry : ranking) {
        scoreRows << QString("%1:%2(c=%3)")
                         .arg(entry.first)
                         .arg(QString::number(entry.second.weight, 'f', 2))
                         .arg(entry.second.count);
    }

    CONDITIONAL_DEBUG(
        "shotname",
        "[ShotNameDetector]"
            << debugContext
            << "batch_scores=" << scoreRows.join(", ")
            << "winner=" << topShot
            << "support=" << QString::number(support, 'f', 2)
            << "margin=" << QString::number(margin, 'f', 2)
            << "accepted=" << accepted
    );

    if (!accepted) {
        return out;
    }

    for (int i = 0; i < out.perItem.size(); ++i) {
        Detection &det = out.perItem[i];
        if (det.shotName == topShot) {
            continue;
        }
        if (!det.isUnknown() && det.strong) {
            CONDITIONAL_DEBUG(
                "shotname",
                "[ShotNameDetector]"
                    << debugContext
                    << "item" << i
                    << "keep_strong_candidate=" << det.shotName
                    << "winner=" << topShot
            );
            continue;
        }
        if (det.isUnknown() || det.confidence < 0.85) {
            const bool wasUnknown = det.isUnknown();
            const QString previousShot = det.shotName;
            det.shotName = topShot;
            det.strategy = QStringLiteral("batch_consensus");
            det.reason = wasUnknown
                ? QStringLiteral("fill_unknown_from_batch")
                : QStringLiteral("override_weak_candidate_from_batch");
            det.confidence = std::max(det.confidence, std::min(0.89, support));
            det.strong = false;
            det.usedConsensus = true;
            CONDITIONAL_DEBUG(
                "shotname",
                "[ShotNameDetector]"
                    << debugContext
                    << "item" << i
                    << "override_with_batch previous=" << previousShot
                    << "new=" << det.shotName
            );
        }
    }

    return out;
}

QString ShotNameDetector::cleanBaseName(const ItemInput &item)
{
    QString base = detectionInputName(item);
    base = QFileInfo(base).fileName();
    if (base.isEmpty()) {
        return QString();
    }

    static const QRegularExpression reHashSeq(
        R"(([_\.])#+\.(exr|png|dpx|tif|tiff)$)",
        QRegularExpression::CaseInsensitiveOption
    );
    static const QRegularExpression reNumericSeq(
        R"(([_\.])\d{3,8}\.(exr|png|dpx|tif|tiff)$)",
        QRegularExpression::CaseInsensitiveOption
    );
    static const QRegularExpression rePrintfSeq(
        R"(([_\.])%0?\d+d\.(exr|png|dpx|tif|tiff)$)",
        QRegularExpression::CaseInsensitiveOption
    );
    static const QRegularExpression reCommonExt(
        R"(\.(exr|png|dpx|tif|tiff|mov|mxf|mp4|avi)$)",
        QRegularExpression::CaseInsensitiveOption
    );
    static const QRegularExpression reVersionSuffix(
        R"(_v\d+$)",
        QRegularExpression::CaseInsensitiveOption
    );
    static const QRegularExpression reTrailSep(R"([_.\-]+$)");

    base.replace(reHashSeq, QString());
    base.replace(reNumericSeq, QString());
    base.replace(rePrintfSeq, QString());
    base.replace(reCommonExt, QString());
    base.replace(reVersionSuffix, QString());
    base.replace(reTrailSep, QString());

    return base.trimmed();
}

ShotNameDetector::Detection ShotNameDetector::detectFromFilename(const ItemInput &item, const QString &cleanBase)
{
    QStringList tokens = cleanBase.split('_', Qt::SkipEmptyParts);
    if (tokens.size() < 3) {
        return makeUnknown(QStringLiteral("filename_too_short"));
    }

    while (!tokens.isEmpty() && isVersionToken(tokens.last())) {
        tokens.removeLast();
    }
    if (tokens.size() < 3) {
        return makeUnknown(QStringLiteral("filename_tokens_after_version"));
    }

    const bool isSeries = tokens.size() >= 4
        && startsWithDigit(tokens.at(1))
        && startsWithDigit(tokens.at(2))
        && startsWithDigit(tokens.at(3));
    const bool isStandardNumeric = startsWithDigit(tokens.at(1)) && startsWithDigit(tokens.at(2));
    if (!looksStrongProjectToken(tokens.first()) || (!isSeries && !isStandardNumeric)) {
        return makeUnknown(QStringLiteral("filename_not_strong"));
    }

    const int baseCount = isSeries ? 4 : 3;
    const bool hasDescription = tokens.size() >= (baseCount + 2);
    const int targetCount = std::min(static_cast<int>(tokens.size()), baseCount + (hasDescription ? 2 : 0));
    const QString candidate = normalizeShotCandidate(tokens.mid(0, targetCount).join('_'));
    if (candidate.isEmpty()) {
        return makeUnknown(QStringLiteral("filename_candidate_empty"));
    }

    Detection out;
    out.shotName = candidate;
    out.confidence = item.isSequence ? 0.93 : 0.86;
    out.strategy = item.isSequence
        ? QStringLiteral("filename_exr_pattern")
        : QStringLiteral("filename_media_pattern");
    out.reason = isSeries
        ? QStringLiteral("series_blocks")
        : (hasDescription ? QStringLiteral("standard_desc_blocks") : QStringLiteral("standard_blocks"));
    out.strong = true;
    return out;
}

ShotNameDetector::Detection ShotNameDetector::detectFromGenericTokens(const ItemInput &item, const QString &cleanBase)
{
    QStringList tokens = cleanBase.split('_', Qt::SkipEmptyParts);
    while (!tokens.isEmpty() && isVersionToken(tokens.last())) {
        tokens.removeLast();
    }
    if (tokens.size() < 2) {
        return makeUnknown(QStringLiteral("generic_too_short"));
    }

    static const QSet<QString> stopTokens = {
        "comp", "roto", "cleanup", "dmp", "plate", "plates", "aplate",
        "editref", "ref", "refs", "review", "proxy", "temp", "tmp",
        "final", "main", "src", "source", "mov", "mxf", "mp4", "avi",
        "exr", "png", "dpx", "tif", "tiff"
    };

    QStringList picked;
    for (const QString &token : tokens) {
        if (token.isEmpty()) {
            continue;
        }
        const QString lower = token.toLower();
        if (picked.size() >= 2 && (isVersionToken(token) || stopTokens.contains(lower))) {
            break;
        }
        picked.push_back(token);
        if (picked.size() >= 4) {
            break;
        }
    }

    if (picked.size() < 2) {
        return makeUnknown(QStringLiteral("generic_no_prefix"));
    }
    if (isGenericPathToken(picked.first())) {
        return makeUnknown(QStringLiteral("generic_prefix_is_generic_folder"));
    }

    int takeCount = (picked.size() >= 3) ? 3 : picked.size();
    if (picked.size() >= 4 && startsWithDigit(picked.at(1)) && startsWithDigit(picked.at(2))) {
        takeCount = 4;
    }

    const QString candidate = normalizeShotCandidate(picked.mid(0, takeCount).join('_'));
    if (candidate.isEmpty()) {
        return makeUnknown(QStringLiteral("generic_candidate_empty"));
    }

    Detection out;
    out.shotName = candidate;
    out.confidence = item.isSequence ? 0.68 : 0.58;
    if (picked.size() >= 3 && startsWithDigit(picked.at(1)) && startsWithDigit(picked.at(2))) {
        out.confidence += 0.07;
    }
    out.strategy = QStringLiteral("filename_generic_tokens");
    out.reason = QStringLiteral("first_tokens_prefix");
    out.strong = false;
    return out;
}

ShotNameDetector::Detection ShotNameDetector::detectFromPathHint(const ItemInput &item)
{
    if (item.absolutePath.trimmed().isEmpty()) {
        return makeUnknown(QStringLiteral("path_hint_empty"));
    }

    const QString cleanPath = QDir::cleanPath(item.absolutePath);
    QFileInfo info(cleanPath);
    QString hint;
    if (info.isFile()) {
        hint = info.absoluteDir().dirName();
    } else {
        hint = info.fileName();
    }
    if (hint.isEmpty()) {
        hint = QDir(cleanPath).dirName();
    }
    hint = hint.trimmed();
    if (hint.isEmpty() || isGenericPathToken(hint)) {
        return makeUnknown(QStringLiteral("path_hint_generic"));
    }

    QStringList tokens = hint.split('_', Qt::SkipEmptyParts);
    while (!tokens.isEmpty() && isVersionToken(tokens.last())) {
        tokens.removeLast();
    }
    QStringList picked;
    for (const QString &token : tokens) {
        if (token.isEmpty()) {
            continue;
        }
        if (picked.isEmpty() && isGenericPathToken(token)) {
            continue;
        }
        picked.push_back(token);
        if (picked.size() >= 3) {
            break;
        }
    }

    const QString rawCandidate = picked.isEmpty() ? hint : picked.join('_');
    const QString candidate = normalizeShotCandidate(rawCandidate);
    if (candidate.isEmpty() || isGenericPathToken(candidate)) {
        return makeUnknown(QStringLiteral("path_hint_candidate_empty"));
    }

    Detection out;
    out.shotName = candidate;
    out.confidence = 0.36;
    out.strategy = QStringLiteral("path_hint");
    out.reason = QStringLiteral("folder_basename_hint");
    out.strong = false;
    return out;
}

QString ShotNameDetector::normalizeShotCandidate(const QString &candidate)
{
    QString normalized = candidate.trimmed();
    normalized.replace('\\', '_');
    normalized.replace('/', '_');
    normalized.replace(' ', '_');
    normalized.replace(QRegularExpression(R"(_{2,})"), "_");
    normalized.replace(QRegularExpression(R"(^[_.\-]+|[_.\-]+$)"), QString());
    if (normalized.isEmpty()) {
        return QString();
    }
    if (normalized.size() < 3) {
        return QString();
    }
    if (isGenericPathToken(normalized)) {
        return QString();
    }
    return normalized;
}

bool ShotNameDetector::isVersionToken(const QString &token)
{
    static const QRegularExpression reVersion(
        R"(^v\d+$)",
        QRegularExpression::CaseInsensitiveOption
    );
    return reVersion.match(token.trimmed()).hasMatch();
}

bool ShotNameDetector::isGenericPathToken(const QString &token)
{
    const QString lower = token.trimmed().toLower();
    if (lower.isEmpty()) {
        return true;
    }

    static const QSet<QString> generic = {
        "input", "_input", "publish", "4_publish", "plates", "plate",
        "refs", "ref", "references", "reference", "originals", "original",
        "converted", "convert", "cache", "tmp", "temp", "renders", "render",
        "media", "footage", "images", "img"
    };
    return generic.contains(lower);
}

bool ShotNameDetector::looksStrongProjectToken(const QString &token)
{
    if (token.isEmpty()) {
        return false;
    }
    if (isVersionToken(token) || isGenericPathToken(token)) {
        return false;
    }
    return token.at(0).isLetter();
}
