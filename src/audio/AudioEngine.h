#pragma once

#include "dsp/Bpsk31Codec.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>

#include <string>
#include <vector>

class QAudioSink;
class QAudioSource;
class QBuffer;
class QIODevice;

class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool startRx();
    void stopRx();
    bool transmitBpsk31(const QString &text, double audioHz);
    void stopTx();

    // Sets the audio offset (Hz) the RX demodulator tracks. Real signal
    // acquisition requires this to be close to the transmitting station's
    // actual audio offset; there is no AFC/frequency search yet.
    void setRxTargetHz(double audioHz);

signals:
    void statusChanged(const QString &status);
    void rxLevelChanged(double rms, double peak);
    // Emitted with the full accumulated decode buffer each time new text is
    // recognised, so the UI layer decides how to diff/display it.
    void rxTextDecoded(const QString &text);
    void txStarted();
    void txFinished();

private slots:
    void readRxAudio();
    void handleSinkStateChanged();

private:
    QByteArray pcm16FromSamples(const std::vector<double> &samples, int channelCount) const;
    void runRxDemodulator();

    QAudioSink *m_sink = nullptr;
    QAudioSource *m_source = nullptr;
    QBuffer *m_txBuffer = nullptr;
    QIODevice *m_rxDevice = nullptr;

    double m_rxTargetHz = 1000.0;
    double m_rxSampleRate = 8000.0;
    int m_rxChannelCount = 1;
    std::vector<double> m_rxSamples;
    std::string m_rxLastDecoded;

    // Hard cap on retained RX samples so a long unattended session cannot
    // grow this without bound. Trimming loses demodulator continuity at the
    // trim boundary (a handful of characters may be missed there) - this is
    // a known limitation of the non-streaming reference demodulator.
    static constexpr std::size_t kMaxRxSamples = 480000; // 10s at 48kHz
};
