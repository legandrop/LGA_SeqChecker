#ifndef TABLECOLUMNWIDTHHELPER_H
#define TABLECOLUMNWIDTHHELPER_H

#include <QHash>
#include <QString>
#include <QVector>

class QTableWidget;

class TableColumnWidthHelper
{
public:
    struct Config {
        QVector<int> fixedColumns;
        QVector<int> flexibleColumns;
        int priorityColumn = -1;
        QHash<int, int> minimumWidths;
        QHash<int, int> defaultWidths;
        QHash<int, double> defaultRatios;
        int contentPaddingPx = 14;
        int headerPaddingPx = 18;
        bool useDelegateSizeHintForItems = false;
        QString debugCategory;
        QString debugContext;
    };

    static bool applyContentAwareWidths(QTableWidget *table, const Config &config);
    static bool applyContentAwareWidthsIfAllFit(QTableWidget *table, const Config &config);
    static bool applyProportionalWidths(QTableWidget *table, const Config &config);
    static bool applyManualResizeRebalance(QTableWidget *table, const Config &config, int resizedColumn, int targetWidth);
};

#endif // TABLECOLUMNWIDTHHELPER_H
