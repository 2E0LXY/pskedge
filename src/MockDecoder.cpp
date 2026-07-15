#include "MockDecoder.h"

#include <QStringList>

MockDecoder::MockDecoder(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(1400);
    connect(&m_timer, &QTimer::timeout, this, &MockDecoder::emitLine);
}

void MockDecoder::start()
{
    m_timer.start();
}

void MockDecoder::stop()
{
    m_timer.stop();
}

void MockDecoder::emitLine()
{
    static const QStringList calls = {"K7ABC", "W1AW", "G0ABC", "DL1XYZ", "M7TEST", "VE3HAM"};
    static const QStringList texts = {
        "CQ DX DE %1 %1 K",
        "%1 DE W1AW UR 599 599 IN CT",
        "%1 SIGNAL OK SNR +12DB IMD -24DB",
        "DE %1 NAME TOM QTH TESTVILLE",
        "%1 LOGGING QSO 73",
        "CQ CQ DE %1 PSK31"
    };

    DecodeLine line;
    line.channel = (m_index % 16) + 1;
    line.callsign = calls.at(m_index % calls.size());
    line.mode = (m_index % 5 == 0) ? "QPSK31" : "BPSK31";
    line.text = texts.at(m_index % texts.size()).arg(line.callsign);
    line.metrics.audioFrequencyHz = 550.0 + line.channel * 135.0;
    line.metrics.rfFrequencyMhz = 14.070000 + line.metrics.audioFrequencyHz / 1000000.0;
    line.metrics.snrDb = 5.0 + (m_index % 12);
    line.metrics.qualityPercent = 70 + (m_index % 28);

    ++m_index;
    emit decoded(line);
}
