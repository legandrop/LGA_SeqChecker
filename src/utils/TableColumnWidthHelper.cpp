#include "seqchecker/utils/TableColumnWidthHelper.h"
#include "mediatools/debug_flags.h"

#include <QAbstractItemModel>
#include <QAbstractItemDelegate>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextDocument>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

bool isValidColumnIndex(const QTableWidget *table, int column)
{
    return table && column >= 0 && column < table->columnCount();
}

int widthForColumn(const QHash<int, int> &values, int column, int fallback)
{
    const int value = values.value(column, fallback);
    return value > 0 ? value : fallback;
}

QString visibleText(const QString &rawText)
{
    if (rawText.isEmpty()) {
        return QString();
    }

    QString normalized = rawText;
    if (Qt::mightBeRichText(normalized)) {
        QTextDocument doc;
        doc.setHtml(normalized);
        normalized = doc.toPlainText();
    }

    normalized.replace('\n', ' ');
    normalized.replace('\r', ' ');
    return normalized;
}

int measureCellWidth(const QTableWidget *table, int row, int column, const TableColumnWidthHelper::Config &config);

bool isDebugEnabled(const TableColumnWidthHelper::Config &config)
{
    const QString category = config.debugCategory.trimmed();
    return !category.isEmpty() && DEBUG_ENABLED(category);
}

QString headerTextForColumn(const QTableWidget *table, int column)
{
    if (!table || column < 0 || column >= table->columnCount()) {
        return QString();
    }
    const QAbstractItemModel *model = table->model();
    const QString text = model
        ? model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString()
        : QString();
    return visibleText(text);
}

QString cellSampleText(const QTableWidget *table, int row, int column)
{
    if (!table || row < 0 || row >= table->rowCount() || column < 0 || column >= table->columnCount()) {
        return QString();
    }

    if (const QWidget *cellWidget = table->cellWidget(row, column)) {
        if (const QLabel *label = qobject_cast<const QLabel *>(cellWidget)) {
            const QString text = visibleText(label->text()).trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
        const QList<QLabel *> labels = cellWidget->findChildren<QLabel *>();
        for (const QLabel *label : labels) {
            const QString text = visibleText(label->text()).trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
    }

    if (const QTableWidgetItem *item = table->item(row, column)) {
        const QString text = visibleText(item->data(Qt::DisplayRole).toString()).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return QString();
}

QString compactSample(const QString &text, int maxLen = 90)
{
    if (text.size() <= maxLen) {
        return text;
    }
    return text.left(maxLen - 3) + "...";
}

void logVisibleOverflowSnapshot(const QTableWidget *table,
                                const QVector<int> &columns,
                                const QVector<int> &targetWidths,
                                const TableColumnWidthHelper::Config &config,
                                const QString &stage)
{
    if (!isDebugEnabled(config) || !table) {
        return;
    }

    CONDITIONAL_DEBUG(config.debugCategory,
                      "[TableColumnWidthHelper][" << stage << "][visible] ctx=" << config.debugContext);

    for (int i = 0; i < columns.size(); ++i) {
        const int col = columns[i];
        const int appliedWidth = (col >= 0 && col < table->columnCount()) ? table->columnWidth(col) : -1;
        const int targetWidth = (i >= 0 && i < targetWidths.size()) ? targetWidths[i] : -1;

        int maxMeasured = 0;
        int overflowCount = 0;
        int rowsMeasured = 0;
        QString firstOverflow;
        int firstOverflowMeasured = 0;
        int firstOverflowRow = -1;

        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->isRowHidden(row)) {
                continue;
            }
            const int measured = measureCellWidth(table, row, col, config);
            if (measured <= 0) {
                continue;
            }
            ++rowsMeasured;
            maxMeasured = std::max(maxMeasured, measured);
            if (appliedWidth > 0 && measured > appliedWidth) {
                ++overflowCount;
                if (firstOverflow.isEmpty()) {
                    firstOverflow = compactSample(cellSampleText(table, row, col));
                    firstOverflowMeasured = measured;
                    firstOverflowRow = row;
                }
            }
        }

        const QString header = headerTextForColumn(table, col);
        CONDITIONAL_DEBUG(
            config.debugCategory,
            "[TableColumnWidthHelper][" << stage << "][col]"
            << " ctx=" << config.debugContext
            << " col=" << col
            << " header=" << header
            << " target=" << targetWidth
            << " applied=" << appliedWidth
            << " maxMeasured=" << maxMeasured
            << " rowsMeasured=" << rowsMeasured
            << " overflowCount=" << overflowCount
            << " firstOverflowRow=" << firstOverflowRow
            << " firstOverflowMeasured=" << firstOverflowMeasured
            << " firstOverflowSample=" << firstOverflow
        );
    }
}

QVector<int> distributeExtra(int extra, const QVector<double> &weightsIn)
{
    const int n = weightsIn.size();
    QVector<int> alloc(n, 0);
    if (n <= 0 || extra <= 0) {
        return alloc;
    }

    QVector<double> weights = weightsIn;
    double weightSum = 0.0;
    for (double &w : weights) {
        if (w < 0.0) {
            w = 0.0;
        }
        weightSum += w;
    }
    if (weightSum <= 0.0) {
        weights.fill(1.0);
        weightSum = static_cast<double>(n);
    }

    QVector<double> remainders(n, 0.0);
    int assigned = 0;
    for (int i = 0; i < n; ++i) {
        const double raw = static_cast<double>(extra) * weights[i] / weightSum;
        const int whole = static_cast<int>(std::floor(raw));
        alloc[i] = whole;
        remainders[i] = raw - static_cast<double>(whole);
        assigned += whole;
    }

    int remaining = extra - assigned;
    if (remaining <= 0) {
        return alloc;
    }

    QVector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&remainders, &weights](int a, int b) {
        if (remainders[a] == remainders[b]) {
            return weights[a] > weights[b];
        }
        return remainders[a] > remainders[b];
    });

    int orderPos = 0;
    while (remaining > 0) {
        const int idx = order[orderPos % n];
        ++alloc[idx];
        --remaining;
        ++orderPos;
    }
    return alloc;
}

QVector<int> distributeExtraWithCaps(int extra, const QVector<double> &weightsIn, const QVector<int> &caps)
{
    const int n = caps.size();
    QVector<int> alloc(n, 0);
    if (n <= 0 || extra <= 0) {
        return alloc;
    }

    int remaining = extra;
    while (remaining > 0) {
        QVector<int> active;
        active.reserve(n);
        for (int i = 0; i < n; ++i) {
            if (caps[i] > alloc[i]) {
                active.push_back(i);
            }
        }
        if (active.isEmpty()) {
            break;
        }

        double weightSum = 0.0;
        for (int idx : active) {
            weightSum += std::max(0.0, weightsIn.value(idx, 0.0));
        }
        const bool useUniform = (weightSum <= 0.0);
        if (useUniform) {
            weightSum = static_cast<double>(active.size());
        }

        QVector<double> remainders(n, -1.0);
        int used = 0;
        for (int idx : active) {
            const int capLeft = caps[idx] - alloc[idx];
            if (capLeft <= 0) {
                continue;
            }
            const double w = useUniform ? 1.0 : std::max(0.0, weightsIn.value(idx, 0.0));
            const double raw = static_cast<double>(remaining) * w / weightSum;
            const int whole = static_cast<int>(std::floor(raw));
            const int give = std::min(capLeft, whole);
            if (give > 0) {
                alloc[idx] += give;
                used += give;
            }
            remainders[idx] = raw - static_cast<double>(whole);
        }

        int leftover = remaining - used;
        if (leftover > 0) {
            std::sort(active.begin(), active.end(), [&remainders, &weightsIn](int a, int b) {
                if (remainders[a] == remainders[b]) {
                    return weightsIn.value(a, 0.0) > weightsIn.value(b, 0.0);
                }
                return remainders[a] > remainders[b];
            });

            for (int idx : active) {
                if (leftover <= 0) {
                    break;
                }
                if (caps[idx] <= alloc[idx]) {
                    continue;
                }
                ++alloc[idx];
                ++used;
                --leftover;
            }
        }

        if (used <= 0) {
            int bestIdx = -1;
            for (int idx : active) {
                if (caps[idx] <= alloc[idx]) {
                    continue;
                }
                if (bestIdx < 0 || weightsIn.value(idx, 0.0) > weightsIn.value(bestIdx, 0.0)) {
                    bestIdx = idx;
                }
            }
            if (bestIdx < 0) {
                break;
            }
            ++alloc[bestIdx];
            used = 1;
        }

        remaining -= used;
    }

    return alloc;
}

QVector<int> distributeByBasis(const QVector<int> &basis, const QVector<int> &mins, int totalWidth)
{
    const int n = basis.size();
    QVector<int> result(n, 0);
    if (n <= 0 || mins.size() != n) {
        return result;
    }

    const int minSum = std::accumulate(mins.begin(), mins.end(), 0);
    if (totalWidth <= minSum) {
        return mins;
    }

    const int extra = totalWidth - minSum;
    QVector<double> weights(n, 0.0);
    for (int i = 0; i < n; ++i) {
        weights[i] = std::max(0, basis[i] - mins[i]);
    }

    const QVector<int> alloc = distributeExtra(extra, weights);
    for (int i = 0; i < n; ++i) {
        result[i] = mins[i] + alloc[i];
    }
    return result;
}

QVector<int> minimumWidthsForColumns(const QVector<int> &columns, const QHash<int, int> &minimums)
{
    QVector<int> mins;
    mins.reserve(columns.size());
    for (int col : columns) {
        mins.push_back(widthForColumn(minimums, col, 80));
    }
    return mins;
}

QVector<int> defaultWidthsForColumns(const QVector<int> &columns, const QHash<int, int> &defaults, const QVector<int> &mins)
{
    QVector<int> out;
    out.reserve(columns.size());
    for (int i = 0; i < columns.size(); ++i) {
        out.push_back(widthForColumn(defaults, columns[i], mins.value(i, 80)));
    }
    return out;
}

QVector<double> ratioWeightsForColumns(const QVector<int> &columns,
                                       const QHash<int, double> &ratios,
                                       const QVector<int> &defaults)
{
    QVector<double> weights;
    weights.reserve(columns.size());
    for (int i = 0; i < columns.size(); ++i) {
        const int col = columns[i];
        const double ratio = ratios.value(col, -1.0);
        if (ratio > 0.0) {
            weights.push_back(ratio);
            continue;
        }
        const int fallback = defaults.value(i, 1);
        weights.push_back(static_cast<double>(std::max(1, fallback)));
    }
    return weights;
}

int availableFlexibleWidth(const QTableWidget *table,
                           const QVector<int> &fixedColumns,
                           const QVector<int> &minimums)
{
    if (!table || !table->viewport()) {
        return -1;
    }

    const int viewportWidth = table->viewport()->width();
    if (viewportWidth <= 0) {
        return -1;
    }

    int fixedWidthSum = 0;
    for (int col : fixedColumns) {
        if (!isValidColumnIndex(table, col)) {
            continue;
        }
        fixedWidthSum += table->columnWidth(col);
    }

    const int rawAvailable = viewportWidth - fixedWidthSum;
    const int minSum = std::accumulate(minimums.begin(), minimums.end(), 0);
    return std::max(rawAvailable, minSum);
}

int measureHeaderWidth(const QTableWidget *table, int column, const TableColumnWidthHelper::Config &config)
{
    if (!table || !isValidColumnIndex(table, column)) {
        return 0;
    }

    const QHeaderView *header = table->horizontalHeader();
    if (!header) {
        return 0;
    }

    const QAbstractItemModel *model = table->model();
    const QString text = visibleText(
        model ? model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString() : QString()
    );
    const QString safeText = text.isEmpty() ? QStringLiteral(" ") : text;
    const QFontMetrics fm(header->font());
    int width = fm.horizontalAdvance(safeText);
    const int margin = header->style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, header);
    width += (margin > 0 ? margin * 2 : 8);
    width += config.headerPaddingPx;
    return width;
}

int measureLabelTextWidth(const QLabel *label)
{
    if (!label) {
        return 0;
    }

    const QString text = visibleText(label->text());
    if (text.isEmpty()) {
        return 0;
    }
    const QFontMetrics fm(label->font());
    return fm.horizontalAdvance(text);
}

int measureCellWidth(const QTableWidget *table, int row, int column, const TableColumnWidthHelper::Config &config)
{
    if (!table || !isValidColumnIndex(table, column) || row < 0 || row >= table->rowCount()) {
        return 0;
    }

    int width = 0;

    QWidget *cellWidget = table->cellWidget(row, column);
    if (cellWidget) {
        if (const QLabel *label = qobject_cast<QLabel *>(cellWidget)) {
            width = std::max(width, measureLabelTextWidth(label));
            const QMargins margins = label->contentsMargins();
            width += margins.left() + margins.right();
        } else {
            const QList<QLabel *> labels = cellWidget->findChildren<QLabel *>();
            for (const QLabel *label : labels) {
                width = std::max(width, measureLabelTextWidth(label));
            }
            width = std::max(width, cellWidget->sizeHint().width());
            if (QLayout *layout = cellWidget->layout()) {
                const QMargins margins = layout->contentsMargins();
                width += margins.left() + margins.right();
            } else {
                const QMargins margins = cellWidget->contentsMargins();
                width += margins.left() + margins.right();
            }
        }
    }

    const QTableWidgetItem *item = table->item(row, column);
    if (item) {
        const QString text = visibleText(item->data(Qt::DisplayRole).toString());
        if (!text.isEmpty()) {
            QFont itemFont = item->font();
            if (itemFont.family().isEmpty()) {
                itemFont = table->font();
            }
            const QFontMetrics fm(itemFont);
            width = std::max(width, fm.horizontalAdvance(text));

            if (config.useDelegateSizeHintForItems) {
                const QModelIndex idx = table->model()->index(row, column);
                QStyleOptionViewItem opt;
                opt.initFrom(table);
                opt.font = itemFont;
                opt.features = QStyleOptionViewItem::HasDisplay;
                if (!item->icon().isNull()) {
                    opt.features |= QStyleOptionViewItem::HasDecoration;
                }
                opt.text = text;

                const QAbstractItemDelegate *delegate = table->itemDelegateForColumn(column);
                if (!delegate) {
                    delegate = table->itemDelegate();
                }
                if (delegate) {
                    width = std::max(width, delegate->sizeHint(opt, idx).width());
                }
            }
        }
    }

    if (width <= 0) {
        return 0;
    }

    const int stylePadding = table->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, table);
    return width + config.contentPaddingPx + (stylePadding > 0 ? stylePadding * 2 : 6);
}

QVector<int> idealWidthsFromVisibleContent(const QTableWidget *table,
                                           const QVector<int> &columns,
                                           const QVector<int> &minimums,
                                           const TableColumnWidthHelper::Config &config)
{
    QVector<int> ideals;
    ideals.reserve(columns.size());
    for (int i = 0; i < columns.size(); ++i) {
        const int col = columns[i];
        int ideal = minimums.value(i, 80);
        ideal = std::max(ideal, measureHeaderWidth(table, col, config));
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->isRowHidden(row)) {
                continue;
            }
            ideal = std::max(ideal, measureCellWidth(table, row, col, config));
        }
        ideals.push_back(ideal);
    }
    return ideals;
}

QVector<int> resolveContentAwareWidths(int availableWidth,
                                       const QVector<int> &minimums,
                                       const QVector<int> &ideals,
                                       const QVector<double> &ratioWeights,
                                       int priorityIndex)
{
    const int n = ideals.size();
    QVector<int> widths = minimums;
    if (n <= 0 || minimums.size() != n || ratioWeights.size() != n) {
        return widths;
    }

    const int minSum = std::accumulate(minimums.begin(), minimums.end(), 0);
    if (availableWidth <= minSum) {
        return minimums;
    }

    const int idealSum = std::accumulate(ideals.begin(), ideals.end(), 0);
    if (idealSum <= availableWidth) {
        widths = ideals;
        const int leftover = availableWidth - idealSum;
        if (leftover > 0) {
            const QVector<int> extraAlloc = distributeExtra(leftover, ratioWeights);
            for (int i = 0; i < n; ++i) {
                widths[i] += extraAlloc.value(i, 0);
            }
        }
        return widths;
    }

    int remaining = availableWidth - minSum;
    if (remaining <= 0) {
        return widths;
    }

    if (priorityIndex >= 0 && priorityIndex < n) {
        const int priorityNeed = std::max(0, ideals[priorityIndex] - minimums[priorityIndex]);
        const int priorityGrant = std::min(priorityNeed, remaining);
        widths[priorityIndex] += priorityGrant;
        remaining -= priorityGrant;
    }

    if (remaining > 0) {
        QVector<int> caps;
        QVector<double> needsWeights;
        QVector<int> indexMap;
        caps.reserve(n);
        needsWeights.reserve(n);
        indexMap.reserve(n);

        for (int i = 0; i < n; ++i) {
            if (i == priorityIndex) {
                continue;
            }
            const int cap = std::max(0, ideals[i] - widths[i]);
            caps.push_back(cap);
            needsWeights.push_back(cap > 0 ? static_cast<double>(cap) : ratioWeights[i]);
            indexMap.push_back(i);
        }

        const QVector<int> capAlloc = distributeExtraWithCaps(remaining, needsWeights, caps);
        int used = 0;
        for (int i = 0; i < indexMap.size(); ++i) {
            const int delta = capAlloc.value(i, 0);
            widths[indexMap[i]] += delta;
            used += delta;
        }
        remaining -= used;
    }

    if (remaining > 0) {
        const QVector<int> extraAlloc = distributeExtra(remaining, ratioWeights);
        for (int i = 0; i < n; ++i) {
            widths[i] += extraAlloc.value(i, 0);
        }
    }

    return widths;
}

bool applyFlexibleWidths(QTableWidget *table, const QVector<int> &columns, const QVector<int> &widths)
{
    if (!table || columns.size() != widths.size()) {
        return false;
    }

    for (int i = 0; i < columns.size(); ++i) {
        const int col = columns[i];
        if (!isValidColumnIndex(table, col)) {
            continue;
        }
        table->setColumnWidth(col, widths[i]);
    }
    return true;
}

bool applyContentAwareInternal(QTableWidget *table,
                               const TableColumnWidthHelper::Config &config,
                               bool onlyIfAllFit)
{
    if (!table || !table->viewport() || config.flexibleColumns.isEmpty()) {
        return false;
    }

    const QVector<int> mins = minimumWidthsForColumns(config.flexibleColumns, config.minimumWidths);
    const QVector<int> defaults = defaultWidthsForColumns(config.flexibleColumns, config.defaultWidths, mins);
    const QVector<double> ratios = ratioWeightsForColumns(config.flexibleColumns, config.defaultRatios, defaults);
    QVector<int> currentWidths;
    currentWidths.reserve(config.flexibleColumns.size());
    for (int col : config.flexibleColumns) {
        currentWidths.push_back(isValidColumnIndex(table, col) ? table->columnWidth(col) : -1);
    }
    const int available = availableFlexibleWidth(table, config.fixedColumns, mins);
    if (available <= 0) {
        if (isDebugEnabled(config)) {
            CONDITIONAL_DEBUG(config.debugCategory,
                              "[TableColumnWidthHelper][contentAware] ctx=" << config.debugContext
                              << " aborted=available<=0");
        }
        return false;
    }

    const QVector<int> ideals = idealWidthsFromVisibleContent(table, config.flexibleColumns, mins, config);
    const int idealSum = std::accumulate(ideals.begin(), ideals.end(), 0);
    if (isDebugEnabled(config)) {
        const QString stage = onlyIfAllFit ? "contentAwareIfAllFit" : "contentAware";
        CONDITIONAL_DEBUG(config.debugCategory,
                          "[TableColumnWidthHelper][" << stage << "]"
                          << " ctx=" << config.debugContext
                          << " viewport=" << table->viewport()->width()
                          << " availableFlexible=" << available
                          << " idealSum=" << idealSum
                          << " rowCount=" << table->rowCount());
        for (int i = 0; i < config.flexibleColumns.size(); ++i) {
            const int col = config.flexibleColumns[i];
            const QString header = headerTextForColumn(table, col);
            CONDITIONAL_DEBUG(
                config.debugCategory,
                "[TableColumnWidthHelper][" << stage << "][input]"
                << " ctx=" << config.debugContext
                << " col=" << col
                << " header=" << header
                << " current=" << currentWidths.value(i)
                << " min=" << mins.value(i)
                << " default=" << defaults.value(i)
                << " ratio=" << ratios.value(i)
                << " ideal=" << ideals.value(i)
            );
        }
    }
    if (onlyIfAllFit && idealSum > available) {
        if (isDebugEnabled(config)) {
            CONDITIONAL_DEBUG(config.debugCategory,
                              "[TableColumnWidthHelper][contentAwareIfAllFit] ctx=" << config.debugContext
                              << " skipped=idealSum>available");
        }
        return false;
    }

    const int priorityIndex = config.flexibleColumns.indexOf(config.priorityColumn);
    const QVector<int> widths = resolveContentAwareWidths(available, mins, ideals, ratios, priorityIndex);
    const bool applied = applyFlexibleWidths(table, config.flexibleColumns, widths);
    if (isDebugEnabled(config)) {
        const QString stage = onlyIfAllFit ? "contentAwareIfAllFit" : "contentAware";
        for (int i = 0; i < config.flexibleColumns.size(); ++i) {
            const int col = config.flexibleColumns[i];
            CONDITIONAL_DEBUG(config.debugCategory,
                              "[TableColumnWidthHelper][" << stage << "][result]"
                              << " ctx=" << config.debugContext
                              << " col=" << col
                              << " target=" << widths.value(i)
                              << " applied=" << (isValidColumnIndex(table, col) ? table->columnWidth(col) : -1));
        }
        logVisibleOverflowSnapshot(table, config.flexibleColumns, widths, config, stage);
    }
    return applied;
}

} // namespace

bool TableColumnWidthHelper::applyContentAwareWidths(QTableWidget *table, const Config &config)
{
    return applyContentAwareInternal(table, config, false);
}

bool TableColumnWidthHelper::applyContentAwareWidthsIfAllFit(QTableWidget *table, const Config &config)
{
    return applyContentAwareInternal(table, config, true);
}

bool TableColumnWidthHelper::applyProportionalWidths(QTableWidget *table, const Config &config)
{
    if (!table || !table->viewport() || config.flexibleColumns.isEmpty()) {
        return false;
    }

    const QVector<int> mins = minimumWidthsForColumns(config.flexibleColumns, config.minimumWidths);
    const QVector<int> defaults = defaultWidthsForColumns(config.flexibleColumns, config.defaultWidths, mins);
    const int available = availableFlexibleWidth(table, config.fixedColumns, mins);
    if (available <= 0) {
        if (isDebugEnabled(config)) {
            CONDITIONAL_DEBUG(config.debugCategory,
                              "[TableColumnWidthHelper][proportional] ctx=" << config.debugContext
                              << " aborted=available<=0");
        }
        return false;
    }

    QVector<int> basis;
    basis.reserve(config.flexibleColumns.size());
    for (int i = 0; i < config.flexibleColumns.size(); ++i) {
        const int col = config.flexibleColumns[i];
        const int currentWidth = isValidColumnIndex(table, col) ? table->columnWidth(col) : 0;
        const int fallback = defaults.value(i, mins.value(i, 80));
        basis.push_back(currentWidth > 0 ? std::max(currentWidth, mins[i]) : fallback);
    }

    const QVector<int> widths = distributeByBasis(basis, mins, available);
    const bool applied = applyFlexibleWidths(table, config.flexibleColumns, widths);
    if (isDebugEnabled(config)) {
        CONDITIONAL_DEBUG(config.debugCategory,
                          "[TableColumnWidthHelper][proportional]"
                          << " ctx=" << config.debugContext
                          << " viewport=" << table->viewport()->width()
                          << " availableFlexible=" << available);
        for (int i = 0; i < config.flexibleColumns.size(); ++i) {
            const int col = config.flexibleColumns[i];
            CONDITIONAL_DEBUG(
                config.debugCategory,
                "[TableColumnWidthHelper][proportional][input]"
                << " ctx=" << config.debugContext
                << " col=" << col
                << " header=" << headerTextForColumn(table, col)
                << " basis=" << basis.value(i)
                << " min=" << mins.value(i)
                << " default=" << defaults.value(i)
            );
            CONDITIONAL_DEBUG(
                config.debugCategory,
                "[TableColumnWidthHelper][proportional][result]"
                << " ctx=" << config.debugContext
                << " col=" << col
                << " target=" << widths.value(i)
                << " applied=" << (isValidColumnIndex(table, col) ? table->columnWidth(col) : -1)
            );
        }
        logVisibleOverflowSnapshot(table, config.flexibleColumns, widths, config, "proportional");
    }
    return applied;
}

bool TableColumnWidthHelper::applyManualResizeRebalance(QTableWidget *table,
                                                        const Config &config,
                                                        int resizedColumn,
                                                        int targetWidth)
{
    if (!table || !table->viewport() || config.flexibleColumns.isEmpty()) {
        return false;
    }

    const int resizedIdx = config.flexibleColumns.indexOf(resizedColumn);
    if (resizedIdx < 0) {
        return false;
    }

    const QVector<int> mins = minimumWidthsForColumns(config.flexibleColumns, config.minimumWidths);
    const int available = availableFlexibleWidth(table, config.fixedColumns, mins);
    if (available <= 0) {
        if (isDebugEnabled(config)) {
            CONDITIONAL_DEBUG(config.debugCategory,
                              "[TableColumnWidthHelper][manualRebalance] ctx=" << config.debugContext
                              << " aborted=available<=0");
        }
        return false;
    }

    int minOthers = 0;
    for (int i = 0; i < mins.size(); ++i) {
        if (i == resizedIdx) {
            continue;
        }
        minOthers += mins[i];
    }

    const int maxTarget = std::max(mins[resizedIdx], available - minOthers);
    const int clampedTarget = std::clamp(targetWidth, mins[resizedIdx], maxTarget);
    const int remaining = available - clampedTarget;

    QVector<int> otherColumns;
    QVector<int> otherMins;
    QVector<int> otherBasis;
    for (int i = 0; i < config.flexibleColumns.size(); ++i) {
        if (i == resizedIdx) {
            continue;
        }
        const int col = config.flexibleColumns[i];
        otherColumns.push_back(col);
        otherMins.push_back(mins[i]);
        const int currentWidth = isValidColumnIndex(table, col) ? table->columnWidth(col) : mins[i];
        otherBasis.push_back(std::max(currentWidth, mins[i]));
    }

    const QVector<int> otherWidths = distributeByBasis(otherBasis, otherMins, remaining);
    table->setColumnWidth(resizedColumn, clampedTarget);
    const bool applied = applyFlexibleWidths(table, otherColumns, otherWidths);
    if (isDebugEnabled(config)) {
        CONDITIONAL_DEBUG(config.debugCategory,
                          "[TableColumnWidthHelper][manualRebalance]"
                          << " ctx=" << config.debugContext
                          << " resizedColumn=" << resizedColumn
                          << " targetWidth=" << targetWidth
                          << " clampedTarget=" << clampedTarget
                          << " availableFlexible=" << available
                          << " remainingForOthers=" << remaining);
        for (int i = 0; i < otherColumns.size(); ++i) {
            CONDITIONAL_DEBUG(config.debugCategory,
                              "[TableColumnWidthHelper][manualRebalance][other]"
                              << " ctx=" << config.debugContext
                              << " col=" << otherColumns.value(i)
                              << " basis=" << otherBasis.value(i)
                              << " min=" << otherMins.value(i)
                              << " target=" << otherWidths.value(i)
                              << " applied=" << (isValidColumnIndex(table, otherColumns.value(i))
                                                  ? table->columnWidth(otherColumns.value(i))
                                                  : -1));
        }
        QVector<int> mergedTargets;
        mergedTargets.reserve(config.flexibleColumns.size());
        for (int col : config.flexibleColumns) {
            if (col == resizedColumn) {
                mergedTargets.push_back(clampedTarget);
                continue;
            }
            const int idx = otherColumns.indexOf(col);
            mergedTargets.push_back(idx >= 0 ? otherWidths.value(idx) : -1);
        }
        logVisibleOverflowSnapshot(table, config.flexibleColumns, mergedTargets, config, "manualRebalance");
    }
    return applied;
}
