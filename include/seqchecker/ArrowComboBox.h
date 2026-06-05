#pragma once

#include <QComboBox>

class ArrowComboBox : public QComboBox
{
public:
    explicit ArrowComboBox(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};
