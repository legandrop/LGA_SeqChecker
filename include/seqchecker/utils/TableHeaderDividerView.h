#ifndef TABLEHEADERDIVIDERVIEW_H
#define TABLEHEADERDIVIDERVIEW_H

#include <QHeaderView>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QString>
#include <QtGlobal>

inline constexpr const char *UI_TABLE_HEADER_DIVIDER_COLOR = "#323232";
inline constexpr const char *UI_TABLE_HEADER_DIVIDER_HOVER_COLOR = "#7a7a7a";
inline constexpr const char *UI_TABLE_HEADER_DIVIDER_ACTIVE_COLOR = "#7969c9";
inline constexpr int UI_TABLE_HEADER_DIVIDER_WIDTH_PX = 1;
inline constexpr int UI_TABLE_HEADER_DIVIDER_ACTIVE_WIDTH_PX = 2;
inline constexpr int UI_TABLE_HEADER_DIVIDER_HOVER_HIT_HALF_WIDTH_PX = 4;

class TableHeaderDividerView : public QHeaderView {
public:
    explicit TableHeaderDividerView(Qt::Orientation orientation,
                                    QWidget *parent = nullptr,
                                    int labelLeftPadding = -1)
        : QHeaderView(orientation, parent)
        , m_labelLeftPadding(labelLeftPadding)
    {
        setMouseTracking(true);
    }

    void setNoBorderColumns(const QSet<int> &columns) { m_noBorderColumns = columns; }
    void setLeftOnlyBorderColumns(const QSet<int> &columns) { m_leftOnlyColumns = columns; }
    void setNoRightBorderColumns(const QSet<int> &columns) { m_noRightColumns = columns; }

    static QString dividerStyleSheet()
    {
        return QStringLiteral(
            "QHeaderView::section {"
            "  border: none;"
            "}"
            "QHeaderView::section:hover {"
            "  border: none;"
            "}"
            "QHeaderView::section:pressed {"
            "  border: none;"
            "}"
        );
    }

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override
    {
        if (!painter || !rect.isValid()) {
            return;
        }

        painter->save();
        if (m_labelLeftPadding >= 0) {
            paintPaddedSection(painter, rect, logicalIndex);
        } else {
            QHeaderView::paintSection(painter, rect, logicalIndex);
        }
        painter->restore();

        const bool drawLeft = shouldDrawLeft(logicalIndex);
        const bool drawRight = shouldDrawRight(logicalIndex);

        painter->save();
        if (drawLeft) {
            paintDivider(painter, rect.left(), rect, logicalIndex, DividerSide::Left);
        }
        if (drawRight) {
            paintDivider(painter, rect.right(), rect, logicalIndex, DividerSide::Right);
        }
        painter->restore();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        updateHoveredDivider(event ? event->pos() : QPoint());
        QHeaderView::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_activeDivider = dividerAt(event ? event->pos() : QPoint());
        update(viewport()->rect());
        QHeaderView::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_activeDivider = DividerHit();
        updateHoveredDivider(event ? event->pos() : QPoint());
        QHeaderView::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hoverDivider = DividerHit();
        if (!m_activeDivider.isValid()) {
            update(viewport()->rect());
        }
        QHeaderView::leaveEvent(event);
    }

private:
    enum class DividerSide {
        Left,
        Right
    };

    struct DividerHit {
        int logicalIndex = -1;
        DividerSide side = DividerSide::Right;

        bool isValid() const { return logicalIndex >= 0; }
        bool operator==(const DividerHit &other) const
        {
            return logicalIndex == other.logicalIndex && side == other.side;
        }
        bool operator!=(const DividerHit &other) const { return !(*this == other); }
    };

    void paintPaddedSection(QPainter *painter, const QRect &rect, int logicalIndex) const
    {
        QStyleOptionHeader option;
        initStyleOptionForIndex(&option, logicalIndex);
        option.rect = rect;

        QStyleOptionHeader sectionOpt = option;
        sectionOpt.text.clear();
        sectionOpt.icon = QIcon();
        style()->drawControl(QStyle::CE_HeaderSection, &sectionOpt, painter, this);

        QStyleOptionHeader labelOpt = option;
        labelOpt.rect = rect.adjusted(m_labelLeftPadding, 0, 0, 0);
        labelOpt.textAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        style()->drawControl(QStyle::CE_HeaderLabel, &labelOpt, painter, this);
    }

    bool shouldDrawLeft(int logicalIndex) const
    {
        if (logicalIndex == 0 || m_noBorderColumns.contains(logicalIndex)) {
            return false;
        }
        return m_leftOnlyColumns.contains(logicalIndex);
    }

    bool shouldDrawRight(int logicalIndex) const
    {
        if (m_noBorderColumns.contains(logicalIndex) || m_leftOnlyColumns.contains(logicalIndex)) {
            return false;
        }
        return !m_noRightColumns.contains(logicalIndex);
    }

    void paintDivider(QPainter *painter, int x, const QRect &rect, int logicalIndex, DividerSide side) const
    {
        const DividerHit current{logicalIndex, side};
        const bool active = m_activeDivider == current;
        const bool hover = active || m_hoverDivider == current;
        const int width = active ? UI_TABLE_HEADER_DIVIDER_ACTIVE_WIDTH_PX : UI_TABLE_HEADER_DIVIDER_WIDTH_PX;
        const QColor color(active
            ? UI_TABLE_HEADER_DIVIDER_ACTIVE_COLOR
            : (hover ? UI_TABLE_HEADER_DIVIDER_HOVER_COLOR : UI_TABLE_HEADER_DIVIDER_COLOR));
        const int left = side == DividerSide::Right ? x - width + 1 : x;

        painter->fillRect(QRect(left, rect.top(), width, rect.height()), color);
    }

    DividerHit dividerAt(const QPoint &pos) const
    {
        if (pos.isNull()) {
            return DividerHit();
        }

        for (int visual = 0; visual < count(); ++visual) {
            const int logical = logicalIndex(visual);
            if (logical < 0 || isSectionHidden(logical)) {
                continue;
            }

            const int left = sectionViewportPosition(logical);
            const int right = left + sectionSize(logical) - 1;
            const int threshold = UI_TABLE_HEADER_DIVIDER_HOVER_HIT_HALF_WIDTH_PX;

            if (shouldDrawLeft(logical) && qAbs(pos.x() - left) <= threshold) {
                return DividerHit{logical, DividerSide::Left};
            }
            if (shouldDrawRight(logical) && qAbs(pos.x() - right) <= threshold) {
                return DividerHit{logical, DividerSide::Right};
            }
        }
        return DividerHit();
    }

    void updateHoveredDivider(const QPoint &pos)
    {
        const DividerHit next = dividerAt(pos);
        if (next == m_hoverDivider) {
            return;
        }

        m_hoverDivider = next;
        update(viewport()->rect());
    }

    QSet<int> m_noBorderColumns;
    QSet<int> m_leftOnlyColumns;
    QSet<int> m_noRightColumns;
    int m_labelLeftPadding = -1;
    DividerHit m_hoverDivider;
    DividerHit m_activeDivider;
};

#endif // TABLEHEADERDIVIDERVIEW_H
