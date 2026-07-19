#pragma once

#include "dsp/Bpsk31Codec.h"
#include "dsp/Bpsk31StreamDecoder.h"
#include "dsp/CwCodec.h"
#include "dsp/Qpsk31Codec.h"

#include <memory>

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
    // Only two real, working modes - everything else the UI ever
    // referenced was a roadmap placeholder that never actually decoded
    // (see FEATURE_ROADMAP.md history), removed per explicit request
    // rather than left implying capability that didn't exist.
    enum class OperatingMode { Bpsk31, Qpsk31, Cw };

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    void setDevices(const QString &rxInputDeviceId, const QString &txOutputDeviceId);
    void setMode(OperatingMode mode);
    OperatingMode mode() const;
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
    OperatingMode m_mode = OperatingMode::Bpsk31;
    double m_rxSampleRate = 8000.0;
    int m_rxChannelCount = 1;
    std::vector<double> m_rxSamples;
    // Separate from m_rxSamples (which is trimmed/retained for the
    // waterfall spectrum display and shouldn't be repurposed for this).
    // Accumulates audio not yet handed to m_streamDecoder; drained and
    // cleared every time runRxDemodulator() successfully feeds it in.
    std::vector<double> m_newRxSamplesForDecoder;
    // Persistent across calls - see Bpsk31StreamDecoder's own comment for
    // why this replaced re-running Bpsk31Codec::demodulateText() over the
    // whole retained buffer every callback (confirmed against real
    // off-air-style recordings to be the actual reason continuous real
    // transmissions weren't decoding, not a tuning/SNR/pulse-shape
    // problem - see git history). unique_ptr rather than a value member
    // because it needs to be reset (destroyed and reconstructed with a
    // fresh carrierHz) whenever the operator manually re-tunes or the
    // sample rate changes, not just have its fields cleared - a stale
    // Bpsk31TrackState from a previous frequency should never bleed into
    // a new one.
    std::unique_ptr<psk::dsp::Bpsk31StreamDecoder> m_streamDecoder;
    std::string m_rxLastDecoded;
    bool m_rxDemodPending = false;
    QString m_rxInputDeviceId;
    QString m_txOutputDeviceId;

    // Hard cap on retained RX samples for the waterfall spectrum display
    // (see publishRxSpectrum()) and for CW (which is still a batch
    // decoder - CwCodec has no streaming equivalent yet, unlike BPSK31).
    //
    // This buffer is NOT what BPSK31 decoding reads from any more - see
    // m_streamDecoder and m_newRxSamplesForDecoder below. It used to be:
    // every audio callback re-ran Bpsk31Codec::demodulateText() over this
    // entire buffer, since the demodulator was a stateless batch function
    // with no persisted state between calls. That was capped at 0.75s at
    // one point, purely as a performance tradeoff - which turned out to
    // be a real bug, not just slow: Bpsk31Config::preambleSymbols
    // defaults to 64, which at 31.25 baud takes 2.048s on its own -
    // longer than that buffer, making it physically impossible to ever
    // see a complete preamble. Raising the cap to 10s (this constant)
    // was a real, measured improvement at the time, but was still only a
    // workaround: even a 10s batch window fundamentally cannot decode a
    // continuous real transmission once its one-and-only preamble (real
    // transmissions don't repeat it) scrolls past the window - confirmed
    // against real off-air-style recordings (see git history), not
    // theorised.
    //
    // The actual, complete fix is Bpsk31StreamDecoder: the Costas/Gardner
    // loop state (epochPos, phaseEpoch, carrier/timing integrators)
    // persists across calls via Bpsk31TrackState once a lock is
    // acquired, so decoding continues indefinitely from new audio without
    // ever needing another preamble - this was flagged as a larger
    // refactor "not attempted blind" in earlier revisions of this
    // comment, and has since been done and verified against real
    // recordings (one decoded a repeating test message continuously and
    // completely for the file's full 40s duration, versus fragments
    // before).
    static constexpr std::size_t kMaxRxSamples = 480000; // 10s at 48kHz
};
