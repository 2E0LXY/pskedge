#include "WaterfallWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtMath>
#include <algorithm>

WaterfallWidget::WaterfallWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(520, 320);
}

void WaterfallWidget::paintEvent(QPaintEvent *)
{
    constexpr int kScaleWidth = 64;

    QPainter painter(this);
    painter.fillRect(rect(), QColor(4, 8, 18));
    painter.drawImage(rect(), m_image);

    painter.fillRect(0, 0, kScaleWidth, height(), QColor(2, 6, 12, 210));
    painter.setPen(QColor(120, 220, 235, 210));
    for (int i = 0; i <= 9; ++i) {
        const double hz = 300.0 + i * 300.0;
        const int y = yForAudio(hz);
        painter.drawLine(kScaleWidth, y, width(), y);
        const QRect labelRect(4, qBound(2, y - 9, qMax(2, height() - 18)), kScaleWidth - 8, 18);
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, QString::number(hz, 'f', 0) + " Hz");
    }
    painter.setPen(QColor(160, 245, 255, 230));
    painter.drawLine(kScaleWidth, 0, kScaleWidth, height());

    const int rxY = yForAudio(m_rxAudioHz);
    painter.setPen(QPen(QColor(98, 235, 255), 2));
    painter.drawLine(kScaleWidth, rxY, width(), rxY);
    painter.drawText(width() - 110, qBound(14, rxY - 4, qMax(14, height() - 22)),
                     QString("RX %1 Hz").arg(m_rxAudioHz, 0, 'f', 0));

    const int txY = yForAudio(m_txAudioHz);
    painter.setPen(QPen(m_txLockedToRx ? QColor(105, 255, 150) : QColor(255, 177, 62), 2, Qt::DashLine));
    painter.drawLine(kScaleWidth, txY, width(), txY);
    painter.drawText(width() - 110, qBound(14, txY + 16, qMax(14, height() - 4)),
                     QString("TX %1 Hz").arg(m_txAudioHz, 0, 'f', 0));
}

void WaterfallWidget::resizeEvent(QResizeEvent *)
{
    if (width() <= 0 || height() <= 0) {
        return;
    }
    QImage newImage(size(), QImage::Format_RGB32);
    newImage.fill(QColor(4, 8, 18));
    if (!m_image.isNull()) {
        QPainter painter(&newImage);
        painter.drawImage(0, 0, m_image.scaled(size()));
    }
    m_image = newImage;
}

void WaterfallWidget::mousePressEvent(QMouseEvent *event)
{
    const double audioHz = audioForY(event->position().y());
    setRxAudioHz(audioHz);
    emit frequencyClicked(audioHz);
}

void WaterfallWidget::addSpectrumLine(const QVector<double> &levels)
{
    if (m_image.size() != size()) {
        m_image = QImage(size(), QImage::Format_RGB32);
        m_image.fill(QColor(4, 8, 18));
    }

    QPainter painter(&m_image);
    painter.drawImage(QPoint(-2, 0), m_image);

    for (int y = 0; y < height(); ++y) {
        const double audioHz = audioForY(y);
        const double normalized = (audioHz - 300.0) / 2700.0;
        const int bin = qBound(0, static_cast<int>(normalized * levels.size()), qMax(0, levels.size() - 1));
        const double level = levels.isEmpty() ? 0.0 : levels.at(bin);

        painter.setPen(colorForLevel(qBound(0.0, level, 1.0)));
        painter.drawPoint(width() - 2, y);
        painter.drawPoint(width() - 1, y);
    }

    update();
}

QColor WaterfallWidget::colorForLevel(double value) const
{
    if (value < 0.20) return QColor(5, 10, 45 + int(value * 120));
    if (value < 0.45) return QColor(0, int(90 + value * 220), int(160 + value * 100));
    if (value < 0.70) return QColor(int(value * 180), 220, 40);
    if (value < 0.90) return QColor(255, int(190 - value * 70), 20);
    return QColor(255, 245, 210);
}

void WaterfallWidget::setRxAudioHz(double audioHz)
{
    m_rxAudioHz = qBound(300.0, audioHz, 3000.0);
    if (m_txLockedToRx) {
        m_txAudioHz = m_rxAudioHz;
    }
    update();
}

void WaterfallWidget::setTxAudioHz(double audioHz)
{
    m_txAudioHz = qBound(300.0, audioHz, 3000.0);
    update();
}

void WaterfallWidget::setTxLockedToRx(bool locked)
{
    m_txLockedToRx = locked;
    if (m_txLockedToRx) {
        m_txAudioHz = m_rxAudioHz;
    }
    update();
}

int WaterfallWidget::yForAudio(double audioHz) const
{
    const double normalized = (audioHz - 300.0) / 2700.0;
    return qBound(0, int((1.0 - normalized) * height()), qMax(0, height() - 1));
}

double WaterfallWidget::audioForY(double y) const
{
    return 300.0 + (1.0 - y / qMax(1, height())) * 2700.0;
}
