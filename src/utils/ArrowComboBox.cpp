#include "seqchecker/ArrowComboBox.h"

#include <QApplication>
#include <QListView>
#include <QPainter>
#include <QPainterPath>

ArrowComboBox::ArrowComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setView(new QListView(this));
}

void ArrowComboBox::paintEvent(QPaintEvent *event)
{
    QComboBox::paintEvent(event);

    if (property("filtered").toBool()) {
        return;
    }

    QColor arrowColor("#7b7b7b");
    if (qApp) {
        const QString configuredColor = qApp->property("txt_input").toString();
        if (!configuredColor.isEmpty() && QColor::isValidColor(configuredColor)) {
            arrowColor = QColor(configuredColor);
        }
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();
    const int cx = r.right() - 10;
    const int cy = r.center().y() + 1;

    QPainterPath path;
    path.moveTo(cx - 4, cy - 2);
    path.lineTo(cx + 4, cy - 2);
    path.lineTo(cx, cy + 3);
    path.closeSubpath();

    painter.fillPath(path, arrowColor);
}
