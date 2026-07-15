#include "WaterfallWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

WaterfallWidget::WaterfallWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(520, 320);
    m_timer.setInterval(45);
    connect(&m_timer, &QTimer::timeout, this, &WaterfallWidget::advanceFrame);
    m_timer.start();
}

void WaterfallWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(4, 8, 18));
    painter.drawImage(rect(), m_image);

    painter.setPen(QColor(90, 120, 145, 160));
    for (int i = 0; i <= 8; ++i) {
        const int x = i * width() / 8;
        painter.drawLine(x, 0, x, height());
        painter.drawText(x + 4, 16, QString::number(1000 + i * 250) + " Hz");
    }

    painter.setPen(QColor(0, 220, 255));
    for (int lane = 1; lane <= 16; ++lane) {
        const int x = lane * width() / 17;
        painter.drawLine(x, 24, x, height());
    }

    const int rxX = xForAudio(m_rxAudioHz);
    painter.setPen(QPen(QColor(98, 235, 255), 2));
    painter.drawLine(rxX, 0, rxX, height());
    painter.drawText(rxX + 4, height() - 28, QString("RX %1 Hz").arg(m_rxAudioHz, 0, 'f', 0));

    const int txX = xForAudio(m_txAudioHz);
    painter.setPen(QPen(m_txLockedToRx ? QColor(105, 255, 150) : QColor(255, 177, 62), 2, Qt::DashLine));
    painter.drawLine(txX, 0, txX, height());
    painter.drawText(txX + 4, height() - 10, QString("TX %1 Hz").arg(m_txAudioHz, 0, 'f', 0));
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
    const double audioHz = audioForX(event->position().x());
    setRxAudioHz(audioHz);
    emit frequencyClicked(audioHz);
}

void WaterfallWidget::advanceFrame()
{
    if (m_image.size() != size()) {
        m_image = QImage(size(), QImage::Format_RGB32);
        m_image.fill(QColor(4, 8, 18));
    }

    QPainter painter(&m_image);
    painter.drawImage(QPoint(-2, 0), m_image);

    for (int y = 0; y < height(); ++y) {
        const double yf = static_cast<double>(y) / qMax(1, height());
        double level = 0.08 + 0.09 * qSin((m_tick + y) * 0.03);

        const double traces[] = {0.18, 0.32, 0.46, 0.63, 0.78};
        for (double trace : traces) {
            const double distance = qAbs(yf - trace);
            level += qExp(-distance * distance * 900.0) * (0.55 + 0.35 * qSin(m_tick * 0.08 + trace * 20.0));
        }

        if ((m_tick / 12 + y) % 97 < 3) {
            level += 0.75;
        }

        painter.setPen(colorForLevel(qBound(0.0, level, 1.0)));
        painter.drawPoint(width() - 2, y);
        painter.drawPoint(width() - 1, y);
    }

    ++m_tick;
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

int WaterfallWidget::xForAudio(double audioHz) const
{
    const double normalized = (audioHz - 300.0) / 2700.0;
    return qBound(0, int(normalized * width()), qMax(0, width() - 1));
}

double WaterfallWidget::audioForX(double x) const
{
    return 300.0 + (x / qMax(1, width())) * 2700.0;
}
