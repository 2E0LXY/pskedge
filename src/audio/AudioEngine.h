#pragma once

#include "dsp/Bpsk31Codec.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVector>

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

    void setDevices(const QString &rxInputDeviceId, const QString &txOutputDeviceId);
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
    // Independent of decode success - reported alongside every decode
    // attempt so the UI can show a real (if simplified) SNR reading even
    // while still hunting for lock. See Bpsk31Codec::measureSignalQuality
    // for what this is and isn't measuring.
    void rxSignalQuality(double snrDb, double signalLevelDb, double noiseFloorDb);
    void rxSpectrumReady(const QVector<double> &levels);
    void txStarted();
    void txFinished();

private slots:
    void readRxAudio();
    void handleSinkStateChanged();

private:
    QByteArray pcm16FromSamples(const std::vector<double> &samples, int channelCount) const;
    QAudioDevice selectedInputDevice() const;
    QAudioDevice selectedOutputDevice() const;
    void runRxDemodulator();
    void publishRxSpectrum();

    QAudioSink *m_sink = nullptr;
    QAudioSource *m_source = nullptr;
    QBuffer *m_txBuffer = nullptr;
    QIODevice *m_rxDevice = nullptr;
    QTimer m_rxDemodTimer;
    QElapsedTimer m_rxLevelClock;
    QElapsedTimer m_rxSpectrumClock;

    double m_rxTargetHz = 1000.0;
    double m_rxSampleRate = 8000.0;
    int m_rxChannelCount = 1;
    std::vector<double> m_rxSamples;
    std::string m_rxLastDecoded;
    bool m_rxDemodPending = false;
    QString m_rxInputDeviceId;
    QString m_txOutputDeviceId;

    // Hard cap on retained RX samples. AudioEngine re-runs
    // Bpsk31Codec::demodulateText() over the ENTIRE retained buffer on
    // every audio callback (see runRxDemodulator()), because the
    // demodulator is a batch function with no persisted state between
    // calls. That makes per-callback cost O(buffer size), not O(new
    // samples). The matched raised-cosine filter and 5-hypothesis
    // frequency acquisition added since the original 3s/10s caps were set
    // substantially raised per-sample cost (measured ~26ms/second of
    // buffer on a representative noise-input worst case), so this cap was
    // re-measured and reduced accordingly: 0.75s bounds worst case to
    // ~19ms, which is safe, but - as before - this is a cap on a real
    // cost, not a fix for it. The actual fix is making the demodulator
    // loop state (epochPos, phaseEpoch, carrier/timing integrators, and
    // now the 5 acquisition hypotheses) persist across calls so only new
    // samples are processed each time; that is a larger refactor
    // (Bpsk31Codec's demodulateBits would need to become a stateful
    // streaming object rather than a pure function) and is flagged here
    // rather than attempted blind.
    //
    // Trimming loses demodulator continuity at the trim boundary (a
    // handful of characters may be missed there) - a second known
    // limitation of the batch-recompute approach, more noticeable now
    // that the retained window is shorter.
    static constexpr std::size_t kMaxRxSamples = 36000; // 0.75s at 48kHz
};
