#include "DecodedLinesWidget.h"

#include <QMouseEvent>
#include <QPainter>

DecodedLinesWidget::DecodedLinesWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(420, 320);
}

void DecodedLinesWidget::setLines(const QVector<DecodeLine> &lines)
{
    m_lines = lines;
    update();
}

void DecodedLinesWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0));

    painter.setPen(QColor(38, 72, 86));
    for (int i = 0; i <= 9; ++i) {
        const double hz = 300.0 + i * 300.0;
        const int y = yForAudio(hz);
        painter.drawLine(0, y, width(), y);
    }

    painter.setPen(QColor(55, 84, 94));
    for (int ch = 1; ch <= 16; ++ch) {
        const int y = (ch * height()) / 17;
        painter.drawText(6, y + 4, QString::number(ch));
        painter.drawLine(34, y, width(), y);
    }

    for (const DecodeLine &line : m_lines) {
        const QRect rect = barRectForLine(line);
        const bool locked = line.state.compare("Locked", Qt::CaseInsensitive) == 0;
        const QColor fill = locked ? QColor(126, 9, 9) : QColor(65, 45, 10);
        const QColor edge = locked ? QColor(235, 170, 160) : QColor(230, 190, 90);

        painter.fillRect(rect, fill);
        painter.setPen(edge);
        painter.drawRect(rect.adjusted(0, 0, -1, -1));

        painter.setPen(QColor(255, 235, 225));
        painter.drawText(rect.adjusted(6, 0, -6, 0),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         displayText(line));
    }
}

void DecodedLinesWidget::mousePressEvent(QMouseEvent *event)
{
    for (const DecodeLine &line : m_lines) {
        if (barRectForLine(line).contains(event->position().toPoint())) {
            emit lineClicked(line);
            return;
        }
    }
}

int DecodedLinesWidget::yForAudio(double audioHz) const
{
    const double normalized = (audioHz - 300.0) / 2700.0;
    return qBound(0, int((1.0 - normalized) * height()), qMax(0, height() - 1));
}

QRect DecodedLinesWidget::barRectForLine(const DecodeLine &line) const
{
    const int y = yForAudio(line.metrics.audioFrequencyHz);
    const int barHeight = 18;
    return QRect(42, qBound(0, y - barHeight / 2, qMax(0, height() - barHeight)),
                 qMax(20, width() - 48), barHeight);
}

QString DecodedLinesWidget::displayText(const DecodeLine &line) const
{
    const QString prefix = QString("Ch%1 %2 %3Hz ")
                               .arg(line.channel)
                               .arg(line.callsign.isEmpty() ? line.state : line.callsign)
                               .arg(line.metrics.audioFrequencyHz, 0, 'f', 0);
    return prefix + line.text;
}
