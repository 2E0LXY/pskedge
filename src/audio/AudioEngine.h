#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>

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

signals:
    void statusChanged(const QString &status);
    void rxLevelChanged(double rms, double peak);
    void txStarted();
    void txFinished();

private slots:
    void readRxAudio();
    void handleSinkStateChanged();

private:
    QAudioFormat pskAudioFormat() const;
    QByteArray pcm16FromSamples(const std::vector<double> &samples) const;

    QAudioSink *m_sink = nullptr;
    QAudioSource *m_source = nullptr;
    QBuffer *m_txBuffer = nullptr;
    QIODevice *m_rxDevice = nullptr;
};
