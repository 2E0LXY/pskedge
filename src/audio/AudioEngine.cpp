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

    // Most consumer audio interfaces do not natively support 8 kHz capture;
    // requesting it directly and refusing to start if unsupported meant RX
    // silently failed to start on the majority of real hardware. Request
    // mono 16-bit at the device's preferred rate instead, and drive the
    // demodulator's sample rate from whatever we actually got.
    QAudioFormat format = input.preferredFormat();
    format.setSampleFormat(QAudioFormat::Int16);
    QAudioFormat monoFormat = format;
    monoFormat.setChannelCount(1);
    if (input.isFormatSupported(monoFormat)) {
        format = monoFormat;
    } else if (!input.isFormatSupported(format)) {
        emit statusChanged("RX audio: no supported 16-bit format on default input");
        return false;
    }
    // If only the multi-channel preferred format is supported, keep it and
    // downmix to mono ourselves in readRxAudio() rather than failing to start.

    m_rxSampleRate = format.sampleRate();
    m_rxChannelCount = std::max(1, format.channelCount());
    m_rxSamples.clear();
    m_rxLastDecoded.clear();

    m_source = new QAudioSource(input, format, this);
    m_rxDevice = m_source->start();
    if (!m_rxDevice) {
        emit statusChanged("RX audio: failed to start input");
        stopRx();
        return false;
    }

    connect(m_rxDevice, &QIODevice::readyRead, this, &AudioEngine::readRxAudio);
    emit statusChanged(QString("RX audio: listening at %1 Hz").arg(m_rxSampleRate, 0, 'f', 0));
    return true;
}

void AudioEngine::setRxTargetHz(double audioHz)
{
    m_rxTargetHz = audioHz;
    // Changing the tracked audio offset invalidates the in-flight
    // demodulator state (oscillator phase relative to buffer start), so
    // start clean rather than mixing bits demodulated at two frequencies.
    m_rxSamples.clear();
    m_rxLastDecoded.clear();
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

    QAudioFormat format = output.preferredFormat();
    format.setSampleFormat(QAudioFormat::Int16);
    QAudioFormat monoFormat = format;
    monoFormat.setChannelCount(1);
    if (output.isFormatSupported(monoFormat)) {
        format = monoFormat;
    } else if (!output.isFormatSupported(format)) {
        emit statusChanged("TX audio: no supported 16-bit format on default output");
        return false;
    }


    psk::dsp::Bpsk31Config config;
    config.sampleRate = format.sampleRate();
    config.carrierHz = std::clamp(audioHz, 300.0, 3000.0);
    config.amplitude = 0.55;

    psk::dsp::Bpsk31Codec codec(config);
    const std::vector<double> samples = codec.modulateText(text.toStdString());

    m_txBuffer = new QBuffer(this);
    m_txBuffer->setData(pcm16FromSamples(samples, format.channelCount()));
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
    const int rawCount = data.size() / static_cast<int>(sizeof(qint16));
    const int channels = std::max(1, m_rxChannelCount);
    const int frameCount = rawCount / channels;
    double sumSquares = 0.0;
    double peak = 0.0;

    m_rxSamples.reserve(m_rxSamples.size() + static_cast<std::size_t>(frameCount));
    for (int frame = 0; frame < frameCount; ++frame) {
        double frameSum = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            frameSum += static_cast<double>(samples[frame * channels + ch]) / 32768.0;
        }
        const double value = frameSum / channels;
        sumSquares += value * value;
        peak = std::max(peak, std::abs(value));
        m_rxSamples.push_back(value);
    }

    const double rms = std::sqrt(sumSquares / std::max(1, frameCount));
    emit rxLevelChanged(rms, peak);
    if (m_rxSamples.size() > kMaxRxSamples) {
        // Trim from the front. This resets demodulator continuity at the
        // trim point (see header comment on kMaxRxSamples) but bounds
        // memory/CPU for an unattended long-running session.
        m_rxSamples.erase(m_rxSamples.begin(),
                           m_rxSamples.begin() + static_cast<long>(m_rxSamples.size() - kMaxRxSamples / 2));
        m_rxLastDecoded.clear();
    }

    runRxDemodulator();
}

void AudioEngine::runRxDemodulator()
{
    psk::dsp::Bpsk31Config config;
    config.sampleRate = m_rxSampleRate;
    config.carrierHz = m_rxTargetHz;

    // Costas carrier PLL + Gardner symbol-timing recovery + 5-hypothesis
    // frequency acquisition (validated envelope: +/-10Hz carrier offset,
    // +/-0.1% clock drift - see Bpsk31Codec.cpp). Still not a fully robust
    // receiver against real off-air noise/QRM - see IMPROVEMENTS.md - but
    // genuinely tracks moderate real-world impairments now, not just a
    // perfectly aligned loopback.
    const psk::dsp::Bpsk31Codec codec(config);
    const std::string decoded = codec.demodulateText(m_rxSamples);

    const psk::dsp::Bpsk31SignalQuality quality = codec.measureSignalQuality(m_rxSamples);
    emit rxSignalQuality(quality.snrDb, quality.signalLevelDb, quality.noiseFloorDb);

    if (decoded != m_rxLastDecoded) {
        m_rxLastDecoded = decoded;
        emit rxTextDecoded(QString::fromStdString(decoded));
    }
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

QByteArray AudioEngine::pcm16FromSamples(const std::vector<double> &samples, int channelCount) const
{
    channelCount = std::max(1, channelCount);
    QByteArray bytes;
    bytes.resize(static_cast<qsizetype>(samples.size() * static_cast<std::size_t>(channelCount) * sizeof(qint16)));
    auto *out = reinterpret_cast<qint16 *>(bytes.data());

    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double clamped = std::clamp(samples[i], -1.0, 1.0);
        const auto value = static_cast<qint16>(std::lrint(clamped * std::numeric_limits<qint16>::max()));
        for (int ch = 0; ch < channelCount; ++ch) {
            out[i * static_cast<std::size_t>(channelCount) + static_cast<std::size_t>(ch)] = value;
        }
    }

    return bytes;
}
