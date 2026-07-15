#include "AudioEngine.h"

#include "dsp/Bpsk31Codec.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QBuffer>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine()
{
    stopTx();
    stopRx();
}

bool AudioEngine::startRx()
{
    stopRx();

    const QAudioDevice input = QMediaDevices::defaultAudioInput();
    if (input.isNull()) {
        emit statusChanged("RX audio: no input device");
        return false;
    }

    const QAudioFormat format = pskAudioFormat();
    if (!input.isFormatSupported(format)) {
        emit statusChanged("RX audio: default input does not support 8 kHz mono PCM");
        return false;
    }

    m_source = new QAudioSource(input, format, this);
    m_rxDevice = m_source->start();
    if (!m_rxDevice) {
        emit statusChanged("RX audio: failed to start input");
        stopRx();
        return false;
    }

    connect(m_rxDevice, &QIODevice::readyRead, this, &AudioEngine::readRxAudio);
    emit statusChanged("RX audio: listening");
    return true;
}

void AudioEngine::stopRx()
{
    if (m_source) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_rxDevice = nullptr;
}

bool AudioEngine::transmitBpsk31(const QString &text, double audioHz)
{
    stopTx();

    const QAudioDevice output = QMediaDevices::defaultAudioOutput();
    if (output.isNull()) {
        emit statusChanged("TX audio: no output device");
        return false;
    }

    const QAudioFormat format = pskAudioFormat();
    if (!output.isFormatSupported(format)) {
        emit statusChanged("TX audio: default output does not support 8 kHz mono PCM");
        return false;
    }

    psk::dsp::Bpsk31Config config;
    config.sampleRate = format.sampleRate();
    config.carrierHz = std::clamp(audioHz, 300.0, 3000.0);
    config.amplitude = 0.55;

    psk::dsp::Bpsk31Codec codec(config);
    const std::vector<double> samples = codec.modulateText(text.toStdString());

    m_txBuffer = new QBuffer(this);
    m_txBuffer->setData(pcm16FromSamples(samples));
    if (!m_txBuffer->open(QIODevice::ReadOnly)) {
        emit statusChanged("TX audio: failed to open sample buffer");
        stopTx();
        return false;
    }

    m_sink = new QAudioSink(output, format, this);
    connect(m_sink, &QAudioSink::stateChanged, this, &AudioEngine::handleSinkStateChanged);
    m_sink->start(m_txBuffer);
    emit txStarted();
    emit statusChanged(QString("TX audio: BPSK31 at %1 Hz").arg(config.carrierHz, 0, 'f', 0));
    return true;
}

void AudioEngine::stopTx()
{
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (m_txBuffer) {
        m_txBuffer->close();
        m_txBuffer->deleteLater();
        m_txBuffer = nullptr;
    }
}

void AudioEngine::readRxAudio()
{
    if (!m_rxDevice) {
        return;
    }

    const QByteArray data = m_rxDevice->readAll();
    if (data.size() < 2) {
        return;
    }

    const auto *samples = reinterpret_cast<const qint16 *>(data.constData());
    const int sampleCount = data.size() / static_cast<int>(sizeof(qint16));
    double sumSquares = 0.0;
    double peak = 0.0;

    for (int i = 0; i < sampleCount; ++i) {
        const double value = static_cast<double>(samples[i]) / 32768.0;
        sumSquares += value * value;
        peak = std::max(peak, std::abs(value));
    }

    const double rms = std::sqrt(sumSquares / std::max(1, sampleCount));
    emit rxLevelChanged(rms, peak);
}

void AudioEngine::handleSinkStateChanged()
{
    if (!m_sink) {
        return;
    }

    if (m_sink->state() == QAudio::IdleState || m_sink->state() == QAudio::StoppedState) {
        stopTx();
        emit txFinished();
        emit statusChanged("TX audio: idle");
    }
}

QAudioFormat AudioEngine::pskAudioFormat() const
{
    QAudioFormat format;
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

QByteArray AudioEngine::pcm16FromSamples(const std::vector<double> &samples) const
{
    QByteArray bytes;
    bytes.resize(static_cast<qsizetype>(samples.size() * sizeof(qint16)));
    auto *out = reinterpret_cast<qint16 *>(bytes.data());

    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double clamped = std::clamp(samples[i], -1.0, 1.0);
        out[i] = static_cast<qint16>(std::lrint(clamped * std::numeric_limits<qint16>::max()));
    }

    return bytes;
}
