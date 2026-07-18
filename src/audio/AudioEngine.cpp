#include "AudioEngine.h"

#include "dsp/Bpsk31Codec.h"
#include "dsp/SimpleFFT.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QBuffer>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
    m_rxDemodTimer.setInterval(750);
    connect(&m_rxDemodTimer, &QTimer::timeout, this, &AudioEngine::runRxDemodulator);
    m_txLevelTimer.setInterval(80);
    connect(&m_txLevelTimer, &QTimer::timeout, this, &AudioEngine::publishTxLevel);
}

AudioEngine::~AudioEngine()
{
    stopTx();
    stopRx();
}

void AudioEngine::setDevices(const QString &rxInputDeviceId, const QString &txOutputDeviceId)
{
    const bool rxChanged = m_rxInputDeviceId != rxInputDeviceId;
    m_rxInputDeviceId = rxInputDeviceId;
    m_txOutputDeviceId = txOutputDeviceId;
    if (rxChanged && m_source) {
        startRx();
    }
}

bool AudioEngine::startRx()
{
    stopRx();

    const QAudioDevice input = selectedInputDevice();
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
        emit statusChanged(QString("RX audio: no supported 16-bit format on %1").arg(input.description()));
        return false;
    }
    // If only the multi-channel preferred format is supported, keep it and
    // downmix to mono ourselves in readRxAudio() rather than failing to start.

    m_rxSampleRate = format.sampleRate();
    m_rxChannelCount = std::max(1, format.channelCount());
    m_rxSamples.clear();
    m_rxLastDecoded.clear();
    m_rxDemodPending = false;
    m_rxLevelClock.restart();
    m_rxSpectrumClock.restart();

    m_source = new QAudioSource(input, format, this);
    m_rxDevice = m_source->start();
    if (!m_rxDevice) {
        emit statusChanged("RX audio: failed to start input");
        stopRx();
        return false;
    }

    connect(m_rxDevice, &QIODevice::readyRead, this, &AudioEngine::readRxAudio);
    m_rxDemodTimer.start();
    emit statusChanged(QString("RX audio: %1 at %2 Hz").arg(input.description()).arg(m_rxSampleRate, 0, 'f', 0));
    return true;
}

void AudioEngine::setAfcEnabled(bool enabled)
{
    m_afcEnabled = enabled;
}

void AudioEngine::setMode(OperatingMode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    // Same reasoning as setRxTargetHz(): switching modes invalidates
    // in-flight demodulator state, which is specific to whichever
    // codec was tracking it.
    m_rxSamples.clear();
    m_rxLastDecoded.clear();
    m_rxDemodPending = false;
}

AudioEngine::OperatingMode AudioEngine::mode() const
{
    return m_mode;
}

void AudioEngine::setRxTargetHz(double audioHz)
{
    m_rxTargetHz = audioHz;
    // Changing the tracked audio offset invalidates the in-flight
    // demodulator state (oscillator phase relative to buffer start), so
    // start clean rather than mixing bits demodulated at two frequencies.
    m_rxSamples.clear();
    m_rxLastDecoded.clear();
    m_rxDemodPending = false;
}

void AudioEngine::stopRx()
{
    m_rxDemodTimer.stop();
    if (m_source) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_rxDevice = nullptr;
    m_rxDemodPending = false;
}

bool AudioEngine::transmitBpsk31(const QString &text, double audioHz)
{
    stopTx();

    const QAudioDevice output = selectedOutputDevice();
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
        emit statusChanged(QString("TX audio: no supported 16-bit format on %1").arg(output.description()));
        return false;
    }


    std::vector<double> samples;
    if (m_mode == OperatingMode::Cw) {
        psk::dsp::CwConfig cwConfig;
        cwConfig.sampleRate = format.sampleRate();
        cwConfig.toneHz = std::clamp(audioHz, 300.0, 3000.0);
        cwConfig.wpm = 18.0; // standard, unhurried hand-sending speed - not yet operator-configurable
        const psk::dsp::CwCodec cwCodec(cwConfig);
        samples = cwCodec.modulateText(text.toStdString());
    } else {
        psk::dsp::Bpsk31Config config;
        config.sampleRate = format.sampleRate();
        config.carrierHz = std::clamp(audioHz, 300.0, 3000.0);
        config.amplitude = 0.55;
        const psk::dsp::Bpsk31Codec codec(config);
        samples = codec.modulateText(text.toStdString());
    }
    m_txSamples = samples;
    m_txSampleRate = format.sampleRate();

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
    m_txLevelTimer.start();
    emit txStarted();
    emit statusChanged(QString("TX audio: %1 %2 at %3 Hz")
                            .arg(output.description())
                            .arg(m_mode == OperatingMode::Cw ? "CW" : "BPSK31")
                            .arg(std::clamp(audioHz, 300.0, 3000.0), 0, 'f', 0));
    return true;
}

void AudioEngine::stopTx()
{
    m_txLevelTimer.stop();
    m_txSamples.clear();
    emit txLevelChanged(0.0, 0.0);
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

void AudioEngine::publishTxLevel()
{
    if (m_txSamples.empty() || !m_sink) {
        return;
    }

    // processedUSecs() is the sink's actual elapsed playback time - map
    // that to a sample index in m_txSamples so this reflects what's
    // genuinely playing right now, not just "some window of the buffer".
    const qint64 processedUsecs = m_sink->processedUSecs();
    const auto centerIndex = static_cast<std::size_t>(
        std::max<qint64>(0, processedUsecs) * m_txSampleRate / 1'000'000.0);

    constexpr std::size_t kWindow = 400; // ~9ms at 44.1kHz - short enough to track fast level changes
    const std::size_t start = centerIndex > kWindow / 2 ? centerIndex - kWindow / 2 : 0;
    const std::size_t end = std::min(m_txSamples.size(), start + kWindow);
    if (start >= end) {
        return;
    }

    double sumSquares = 0.0;
    double peak = 0.0;
    for (std::size_t i = start; i < end; ++i) {
        const double sample = m_txSamples[i];
        sumSquares += sample * sample;
        peak = std::max(peak, std::abs(sample));
    }
    const double rms = std::sqrt(sumSquares / static_cast<double>(end - start));
    emit txLevelChanged(rms, peak);
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

    if (!m_rxLevelClock.isValid() || m_rxLevelClock.elapsed() >= 100) {
        const double rms = std::sqrt(sumSquares / std::max(1, frameCount));
        emit rxLevelChanged(rms, peak);
        m_rxLevelClock.restart();
    }

    if (!m_rxSpectrumClock.isValid() || m_rxSpectrumClock.elapsed() >= 80) {
        publishRxSpectrum();
        m_rxSpectrumClock.restart();
    }

    if (m_rxSamples.size() > kMaxRxSamples) {
        // Trim from the front. This resets demodulator continuity at the
        // trim point (see header comment on kMaxRxSamples) but bounds
        // memory/CPU for an unattended long-running session.
        m_rxSamples.erase(m_rxSamples.begin(),
                           m_rxSamples.begin() + static_cast<long>(m_rxSamples.size() - kMaxRxSamples / 2));
        m_rxLastDecoded.clear();
    }

    m_rxDemodPending = true;
}

void AudioEngine::publishRxSpectrum()
{
    constexpr std::size_t kFftSize = 2048;
    constexpr int kOutputBins = 256;
    constexpr double kMinHz = 300.0;
    constexpr double kMaxHz = 3000.0;
    constexpr double kPi = 3.141592653589793238462643383279502884;

    QVector<double> levels(kOutputBins, 0.0);
    if (m_rxSamples.size() < kFftSize || m_rxSampleRate <= 0.0) {
        emit rxSpectrumReady(levels);
        return;
    }

    std::vector<std::complex<double>> fft(kFftSize);
    const std::size_t start = m_rxSamples.size() - kFftSize;
    for (std::size_t i = 0; i < kFftSize; ++i) {
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(kFftSize - 1));
        fft[i] = {m_rxSamples[start + i] * window, 0.0};
    }

    psk::dsp::SimpleFFT::forward(fft);

    for (int out = 0; out < kOutputBins; ++out) {
        const double binStartHz = kMinHz + (kMaxHz - kMinHz) * static_cast<double>(out) / kOutputBins;
        const double binEndHz = kMinHz + (kMaxHz - kMinHz) * static_cast<double>(out + 1) / kOutputBins;
        const int firstBin = std::max(1, static_cast<int>(std::floor(binStartHz * static_cast<double>(kFftSize) / m_rxSampleRate)));
        const int lastBin = std::min(static_cast<int>(kFftSize / 2 - 1),
                                     static_cast<int>(std::ceil(binEndHz * static_cast<double>(kFftSize) / m_rxSampleRate)));

        double peak = 0.0;
        for (int bin = firstBin; bin <= lastBin; ++bin) {
            peak = std::max(peak, std::abs(fft[static_cast<std::size_t>(bin)]));
        }

        const double db = peak > 1.0e-12 ? 20.0 * std::log10(peak / static_cast<double>(kFftSize)) : -120.0;
        levels[out] = std::clamp((db + 95.0) / 65.0, 0.0, 1.0);
    }

    emit rxSpectrumReady(levels);
}

void AudioEngine::runRxDemodulator()
{
    if (!m_rxDemodPending || m_rxSamples.empty()) {
        return;
    }
    m_rxDemodPending = false;

    std::string decoded;
    double snrDb = 0.0;
    double signalLevelDb = 0.0;
    double noiseFloorDb = 0.0;

    if (m_mode == OperatingMode::Cw) {
        // No AFC/acquisition-offset tracking for CW yet - CwCodec's
        // envelope detector doesn't track carrier phase the way the
        // Costas loop does, so there's no equivalent "locked offset" to
        // report. The operator needs to be reasonably close to the
        // actual tone, same as BPSK31 with AFC off.
        psk::dsp::CwConfig cwConfig;
        cwConfig.sampleRate = m_rxSampleRate;
        cwConfig.toneHz = m_rxTargetHz;
        const psk::dsp::CwCodec cwCodec(cwConfig);
        decoded = cwCodec.demodulateText(m_rxSamples);
        // CW has no equivalent to Bpsk31Codec::measureSignalQuality yet -
        // left at 0 rather than fabricating a reading for a measurement
        // that doesn't exist for this mode.
    } else {
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
        if (m_afcEnabled) {
            const psk::dsp::Bpsk31DemodResult result = codec.demodulateTextWithLock(m_rxSamples);
            decoded = result.text;
            // Only trust a genuine lock (see Bpsk31DemodResult::hasLock) to
            // move the target - nudging based on whichever hypothesis merely
            // scored highest on noise would walk the target frequency around
            // randomly with nothing actually being received, which is worse
            // than staying put.
            if (result.hasLock && std::abs(result.lockedOffsetHz) > 0.01) {
                m_rxTargetHz = std::clamp(m_rxTargetHz + result.lockedOffsetHz, 300.0, 3000.0);
                emit rxTargetHzChanged(m_rxTargetHz);
            }
        } else {
            decoded = codec.demodulateText(m_rxSamples);
        }
        const psk::dsp::Bpsk31SignalQuality quality = codec.measureSignalQuality(m_rxSamples);
        snrDb = quality.snrDb;
        signalLevelDb = quality.signalLevelDb;
        noiseFloorDb = quality.noiseFloorDb;
    }

    emit rxSignalQuality(snrDb, signalLevelDb, noiseFloorDb);

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

QAudioDevice AudioEngine::selectedInputDevice() const
{
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    if (!m_rxInputDeviceId.isEmpty()) {
        const QByteArray wanted = m_rxInputDeviceId.toUtf8();
        for (const QAudioDevice &device : devices) {
            if (device.id() == wanted) {
                return device;
            }
        }
    }
    return QMediaDevices::defaultAudioInput();
}

QAudioDevice AudioEngine::selectedOutputDevice() const
{
    const QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
    if (!m_txOutputDeviceId.isEmpty()) {
        const QByteArray wanted = m_txOutputDeviceId.toUtf8();
        for (const QAudioDevice &device : devices) {
            if (device.id() == wanted) {
                return device;
            }
        }
    }
    return QMediaDevices::defaultAudioOutput();
}
