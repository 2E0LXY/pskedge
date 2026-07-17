#pragma once

#include "AppConfig.h"
#include "DecodedLinesWidget.h"
#include "DecoderTableModel.h"
#include "SignalTypes.h"
#include "audio/AudioEngine.h"
#include "cat/CatController.h"

#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QThread>
#include <QVector>

class WaterfallWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    // CatController lives on a worker thread (see the constructor) so its
    // blocking rigctld/OmniRig I/O doesn't stall the GUI. These are
    // emitted rather than calling CatController's slots directly, because
    // a direct method call on an object living on another thread runs on
    // the CALLING thread without any synchronization - only a signal/slot
    // connection gets Qt's automatic queued marshaling across threads.
    void requestCatConfigure(const AppConfig &config);
    void requestCatStart();
    void requestCatStop();
    void requestCatSetActiveRig(int rig);
    void requestCatSetFrequency(double frequencyHz);
    void requestCatSetPtt(bool enabled);

private slots:
    void handleActiveDecodeClick(const QModelIndex &index);
    void handleSweeperClick(const QModelIndex &index);
    void handleWaterfallClick(double audioHz);
    void openSettings();
    void insertMacro(const QString &macroText);
    void transmitComposer();
    void abortTransmit();
    void handleAudioStatus(const QString &status);
    void handleRxLevel(double rms, double peak);
    void handleTxStarted();
    void handleTxFinished();
    void updateTxSafety();
    void handleRxTextDecoded(const QString &text);
    void handleRxSignalQuality(double snrDb, double signalLevelDb, double noiseFloorDb);
    void handleRxSpectrumReady(const QVector<double> &levels);
    void handleCatStatus(const QString &status);
    void handleCatSnapshot(const CatSnapshot &snapshot);
    void handleCatFrequencyResult(bool ok, double frequencyHz);
    void handleBandChanged(const QString &band);

private:
    QString extractCallsign(const QString &text, const QString &fallback) const;
    void prepareReply(const DecodeLine &line);
    void loadSettings();
    void saveSettings() const;
    void updateStatusLabels();
    void applyDisplayedFrequency(double frequencyHz);
    QWidget *buildTopBar();
    QWidget *buildRightPanel();
    QWidget *buildWorkflowPanel();
    QWidget *buildSelectedQsoPanel();
    QWidget *buildTxPanel();
    QWidget *buildRxTranscriptPanel();
    void configureTable(QTableView *view);
    void refreshDecodedLines();

    AppConfig m_config;
    DecodeLine m_selectedLine;
    DecoderTableModel *m_activeModel = nullptr;
    DecoderTableModel *m_sweeperModel = nullptr;
    WaterfallWidget *m_waterfall = nullptr;
    DecodedLinesWidget *m_decodedLines = nullptr;
    QTableView *m_activeView = nullptr;
    QTableView *m_sweeperView = nullptr;
    QPlainTextEdit *m_txText = nullptr;
    QPlainTextEdit *m_rxTranscript = nullptr;
    QLabel *m_vfoLabel = nullptr;
    QLabel *m_targetLabel = nullptr;
    QLabel *m_catLabel = nullptr;
    QLabel *m_qsoCallLabel = nullptr;
    QLabel *m_qsoSignalLabel = nullptr;
    QLabel *m_qsoFreqLabel = nullptr;
    QLabel *m_txSafetyLabel = nullptr;
    QLabel *m_audioLabel = nullptr;
    QLabel *m_rxLevelLabel = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_abortButton = nullptr;
    AudioEngine *m_audioEngine = nullptr;
    CatController *m_catController = nullptr;
    QThread *m_catThread = nullptr;
    QString m_pendingBandName;
    DecodeLine m_liveRxLine;
    double m_catFrequencyHz = 14070000.0;
};
