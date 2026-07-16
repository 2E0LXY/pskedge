#pragma once

#include "AppConfig.h"
#include "DecoderTableModel.h"
#include "SignalTypes.h"
#include "audio/AudioEngine.h"

#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>

class WaterfallWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

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

private:
    QString extractCallsign(const QString &text, const QString &fallback) const;
    void prepareReply(const DecodeLine &line);
    void loadSettings();
    void saveSettings() const;
    void updateStatusLabels();
    QWidget *buildTopBar();
    QWidget *buildRightPanel();
    QWidget *buildWorkflowPanel();
    QWidget *buildSelectedQsoPanel();
    QWidget *buildTxPanel();
    QWidget *buildRxTranscriptPanel();
    void configureTable(QTableView *view);

    AppConfig m_config;
    DecodeLine m_selectedLine;
    DecoderTableModel *m_activeModel = nullptr;
    DecoderTableModel *m_sweeperModel = nullptr;
    WaterfallWidget *m_waterfall = nullptr;
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
    DecodeLine m_liveRxLine;
};
