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

    // When enabled, a genuine hypothesis lock (see
    // Bpsk31Codec::demodulateTextWithLock - only a real lock, not a
    // marginal noise-coincidence score, updates the target) nudges the RX
    // target frequency toward whatever the signal was actually measured
    // at, so a drifting or slightly-mistuned signal is tracked over
    // successive demod cycles instead of requiring the operator to
    // re-click the waterfall. Off by default - manual tuning stays exact
    // and predictable unless the operator opts in.
    void setAfcEnabled(bool enabled);

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
    // Emitted only when AFC (see setAfcEnabled) actually moves the RX
    // target - not on every demod cycle, and not for manual
    // setRxTargetHz() calls (the caller already knows that value).
    void rxTargetHzChanged(double audioHz);
    void txStarted();
    void txFinished();
    // Polled from the sample buffer at the sink's actual playback
    // position (see publishTxLevel()) - a real, time-varying meter of
    // what's actually being transmitted, not a static/one-shot value.
    void txLevelChanged(double rms, double peak);

private slots:
    void readRxAudio();
    void handleSinkStateChanged();

private:
    QByteArray pcm16FromSamples(const std::vector<double> &samples, int channelCount) const;
    QAudioDevice selectedInputDevice() const;
    QAudioDevice selectedOutputDevice() const;
    void runRxDemodulator();
    void publishRxSpectrum();
    void publishTxLevel();

    QAudioSink *m_sink = nullptr;
    QAudioSource *m_source = nullptr;
    QBuffer *m_txBuffer = nullptr;
    QIODevice *m_rxDevice = nullptr;
    QTimer m_rxDemodTimer;
    QElapsedTimer m_rxLevelClock;
    QElapsedTimer m_rxSpectrumClock;

    // The raw (pre-16-bit-PCM) samples being transmitted, kept so
    // publishTxLevel() can compute a real RMS/peak from whatever segment
    // is actually playing right now (via m_sink->processedUSecs()),
    // rather than a single static value computed once when TX starts.
    std::vector<double> m_txSamples;
    double m_txSampleRate = 8000.0;
    QTimer m_txLevelTimer;

    double m_rxTargetHz = 1000.0;
    bool m_afcEnabled = false;
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
    // samples).
    //
    // This was previously capped at 0.75s, which is a real bug, not just
    // a performance tradeoff: Bpsk31Config::preambleSymbols defaults to
    // 64, which at 31.25 baud takes 64/31.25 = 2.048 seconds on its own -
    // longer than the entire retained buffer. That makes it physically
    // impossible to ever see a complete preamble on continuous real
    // reception, which is exactly why real off-air testing (DigiPan
    // decoding cleanly, PSKedge producing only fragments like "en" / "ee"
    // and constantly re-syncing on the same signal) failed completely
    // despite passing every synthetic self-test - those pass complete
    // signals directly to demodulateText(), never going through this
    // trimmed rolling-buffer path at all. Raised to 10s - measured
    // worst-case demod time per cycle (10s of pure noise, forcing all 5
    // acquisition hypotheses through their full tracking pass with
    // nothing to lock onto) is 251ms, comfortably under the 750ms demod
    // timer interval (see m_rxDemodTimer) - checked directly with a
    // standalone timing test, not extrapolated from the old per-second
    // cost estimate.
    //
    // The actual, complete fix is making the demodulator loop state
    // (epochPos, phaseEpoch, carrier/timing integrators, and the 5
    // acquisition hypotheses) persist across calls so only new samples
    // are processed each time; that is a larger refactor (demodulateBits
    // would need to become a stateful streaming object rather than a
    // pure function) and is flagged here rather than attempted blind.
    // This fix (a bigger window) makes real reception actually work; it
    // does not remove the underlying O(buffer size) cost.
    //
    // Trimming still loses demodulator continuity at the trim boundary (a
    // handful of characters may be missed there) - a second known
    // limitation of the batch-recompute approach.
    static constexpr std::size_t kMaxRxSamples = 480000; // 10s at 48kHz - see comment above for why 0.75s was actually broken, not just slow
};
