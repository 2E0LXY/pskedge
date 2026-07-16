#pragma once

#include "SignalTypes.h"

#include <QVector>
#include <QWidget>

class DecodedLinesWidget : public QWidget {
    Q_OBJECT

public:
    explicit DecodedLinesWidget(QWidget *parent = nullptr);

    void setLines(const QVector<DecodeLine> &lines);

signals:
    void lineClicked(const DecodeLine &line);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    int yForAudio(double audioHz) const;
    QRect barRectForLine(const DecodeLine &line) const;
    QString displayText(const DecodeLine &line) const;

    QVector<DecodeLine> m_lines;
};
