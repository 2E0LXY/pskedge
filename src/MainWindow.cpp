#include "MainWindow.h"

#include "MacroEngine.h"
#include "SettingsDialog.h"
#include "WaterfallWidget.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMap>
#include <QMenuBar>
#include <QPair>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTabWidget>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    loadSettings();

    setWindowTitle("PSKedge v0.2.0 beta");
    resize(1480, 900);

    auto *settingsAction = new QAction("Setup", this);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    menuBar()->addMenu("File")->addAction(settingsAction);

    m_activeModel = new DecoderTableModel(DecoderTableModel::Mode::ActiveDecoders, this);
    m_sweeperModel = new DecoderTableModel(DecoderTableModel::Mode::SweeperCandidates, this);

    // CatController does blocking rigctld TCP round-trips and (on Windows)
    // spawns a PowerShell process per OmniRig poll - moved to a dedicated
    // worker thread so that I/O can't stall the GUI. No parent is passed
    // to the constructor: moveToThread() requires the object have no
    // parent living on a different thread. All commands to it go through
    // the requestCat*() signals (connected below with the default
    // auto-connection, which Qt makes queued automatically since sender
    // and receiver are on different threads) rather than direct method
    // calls, which would run unsynchronized on the calling thread.
    m_catController = new CatController();
    m_catThread = new QThread(this);
    m_catController->moveToThread(m_catThread);
    connect(m_catThread, &QThread::finished, m_catController, &QObject::deleteLater);
    connect(this, &MainWindow::requestCatConfigure, m_catController, &CatController::configure);
    connect(this, &MainWindow::requestCatStart, m_catController, &CatController::start);
    connect(this, &MainWindow::requestCatStop, m_catController, &CatController::stop);
    connect(this, &MainWindow::requestCatSetActiveRig, m_catController, &CatController::setActiveRig);
    connect(this, &MainWindow::requestCatSetFrequency, m_catController, &CatController::setFrequencyHz);
    connect(this, &MainWindow::requestCatSetPtt, m_catController, &CatController::setPtt);
    connect(m_catController, &CatController::statusChanged, this, &MainWindow::handleCatStatus);
    connect(m_catController, &CatController::snapshotChanged, this, &MainWindow::handleCatSnapshot);
    connect(m_catController, &CatController::frequencySetResult, this, &MainWindow::handleCatFrequencyResult);
    m_catThread->start();
    emit requestCatConfigure(m_config);
    m_audioEngine = new AudioEngine(this);
    connect(m_audioEngine, &AudioEngine::statusChanged, this, &MainWindow::handleAudioStatus);
    connect(m_audioEngine, &AudioEngine::rxLevelChanged, this, &MainWindow::handleRxLevel);
    connect(m_audioEngine, &AudioEngine::rxTextDecoded, this, &MainWindow::handleRxTextDecoded);
    connect(m_audioEngine, &AudioEngine::rxSignalQuality, this, &MainWindow::handleRxSignalQuality);
    connect(m_audioEngine, &AudioEngine::rxSpectrumReady, this, &MainWindow::handleRxSpectrumReady);
    connect(m_audioEngine, &AudioEngine::txStarted, this, &MainWindow::handleTxStarted);
    connect(m_audioEngine, &AudioEngine::txFinished, this, &MainWindow::handleTxFinished);
    m_audioEngine->setDevices(m_config.audio.rxInputDeviceId, m_config.audio.txOutputDeviceId);
    m_liveRxLine.channel = 1;
    m_liveRxLine.mode = "BPSK31";
    m_liveRxLine.state = "Searching";
    m_liveRxLine.metrics.audioFrequencyHz = 1000.0;
    m_liveRxLine.metrics.rfFrequencyMhz = 14.070000 + m_liveRxLine.metrics.audioFrequencyHz / 1000000.0;
    // SignalMetrics' default snrDb/qualityPercent/imdDb are leftover
    // placeholder values from before real measurement existed - override
    // them here rather than display fabricated numbers before the first
    // real reading arrives. imdDb has no real measurement implemented at
    // all (see handleRxSignalQuality) so it stays "n/a" permanently, not
    // just until first update.
    m_liveRxLine.metrics.snrDb = 0.0;
    m_liveRxLine.metrics.qualityPercent = 0;
    m_liveRxLine.metrics.imdDb = "n/a";

    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);
    mainLayout->addWidget(buildTopBar());

    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_waterfall = new WaterfallWidget(this);
    connect(m_waterfall, &WaterfallWidget::frequencyClicked, this, &MainWindow::handleWaterfallClick);
    mainSplitter->addWidget(m_waterfall);
    mainSplitter->addWidget(buildRightPanel());
    mainSplitter->setStretchFactor(0, 5);
    mainSplitter->setStretchFactor(1, 2);
    mainLayout->addWidget(mainSplitter, 1);

    // Selected QSO, RX transcript, and the TX composer stay visible at all
    // times - an operator mid-QSO should never lose the reply target or
    // composer just because they checked the Log or Signal tab. Only the
    // secondary reference info (log state, measurement windows) is tabbed.
    mainLayout->addWidget(buildRxTranscriptPanel());
    mainLayout->addWidget(buildSelectedQsoPanel());
    mainLayout->addWidget(buildTxPanel());
    mainLayout->addWidget(buildWorkflowPanel());
    setCentralWidget(central);

    setStyleSheet(R"(
        QMainWindow, QWidget { background: #0b1017; color: #d8f7ff; }
        QLabel#vfo { color: #6ee9ff; font-size: 42px; font-weight: 600; }
        QLabel#txSafe { color: #70ff9f; font-weight: 700; }
        QLabel#txInhibit { color: #ffb13e; font-weight: 700; }
        QGroupBox { border: 1px solid #294554; margin-top: 10px; padding: 8px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
        QTableView { background: #070b10; gridline-color: #1e3d49; color: #74e6f6; selection-background-color: #16495c; }
        QHeaderView::section { background: #16212b; color: #d8f7ff; padding: 4px; border: 1px solid #263642; }
        QPlainTextEdit { background: #05080c; color: #d8f7ff; border: 1px solid #2e5360; font-family: Consolas, monospace; }
        QPushButton { background: #1b2a35; color: #d8f7ff; border: 1px solid #355465; padding: 6px 10px; }
        QPushButton:hover { background: #244052; }
        QPushButton:disabled { color: #60717a; background: #101820; border-color: #1b2a35; }
    )");

    connect(m_txText, &QPlainTextEdit::textChanged, this, &MainWindow::updateTxSafety);

    m_catLabel = new QLabel("CAT Offline", this);
    m_audioLabel = new QLabel("Audio: starting", this);
    m_rxLevelLabel = new QLabel("RX level: --", this);
    statusBar()->addPermanentWidget(m_catLabel);
    statusBar()->addPermanentWidget(m_audioLabel);
    statusBar()->addPermanentWidget(m_rxLevelLabel);

    m_audioEngine->startRx();
    if (m_config.cat.autoConnect) {
        emit requestCatStart();
    }
    updateStatusLabels();
    updateTxSafety();
}

MainWindow::~MainWindow()
{
    // Stop the worker thread cleanly before the CatController it owns (or
    // this window) is destroyed - CatController is deleted via
    // deleteLater() once the thread's event loop actually finishes (see
    // the QThread::finished connection in the constructor), not here.
    if (m_catThread) {
        m_catThread->quit();
        m_catThread->wait();
    }
}

QWidget *MainWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 0, 4, 0);

    auto *setup = new QPushButton("Setup", this);
    connect(setup, &QPushButton::clicked, this, &MainWindow::openSettings);

    auto *band = new QComboBox(this);
    band->addItems({"160m", "80m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m"});
    band->setCurrentText("20m");
    band->setToolTip("Sets the active CAT rig to the normal PSK dial frequency for this band");
    connect(band, &QComboBox::currentTextChanged, this, &MainWindow::handleBandChanged);

    m_vfoLabel = new QLabel("14.070.000 MHz", this);
    m_vfoLabel->setObjectName("vfo");
    auto *mode = new QComboBox(this);
    mode->setToolTip("Roadmap of modes to implement (curated to modes still in real use - see "
                      "DM780_FEATURE_GAP.md) - only BPSK31 is actually decodable today; selecting "
                      "anything else does not change what this app can send or receive yet");
    mode->addItems({
        "PSK / BPSK31",
        "PSK / BPSK63",
        "PSK / BPSK125",
        "QPSK / QPSK31",
        "QPSK / QPSK63",
        "QPSK / QPSK125",
        "RTTY / RTTY (FSK)",
        "RTTY / RTTY (AFSK)",
        "CW / CW",
        "MFSK / MFSK8",
        "MFSK / MFSK16",
        "Olivia / OLIVIA-8-250",
        "Olivia / OLIVIA-16-500",
        "Olivia / OLIVIA-32-1000",
        "Hell / Feld Hell",
        "SSTV / Martin 1",
        "SSTV / Scottie 1",
        "SSTV / Robot 36"
    });
    mode->setCurrentText("PSK / BPSK31");
    auto *modeStatus = new QLabel("BW: 60 Hz   RX   TX/RX Locked", this);

    layout->addWidget(setup);
    layout->addWidget(band);
    layout->addStretch();
    layout->addWidget(m_vfoLabel);
    layout->addStretch();
    layout->addWidget(mode);
    layout->addWidget(modeStatus);
    return bar;
}

QWidget *MainWindow::buildRightPanel()
{
    auto *panel = new QWidget(this);
    panel->setMinimumWidth(420);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *activeBox = new QGroupBox("Active Decoders", this);
    auto *activeLayout = new QVBoxLayout(activeBox);
    activeLayout->setContentsMargins(8, 10, 8, 8);
    m_decodedLines = new DecodedLinesWidget(this);
    connect(m_decodedLines, &DecodedLinesWidget::lineClicked, this, [this](const DecodeLine &line) {
        DecodeLine selected = line;
        selected.callsign = extractCallsign(selected.text, selected.callsign);
        prepareReply(selected);
    });
    activeLayout->addWidget(m_decodedLines);

    layout->addWidget(activeBox);
    return panel;
}

QWidget *MainWindow::buildWorkflowPanel()
{
    auto *tabs = new QTabWidget(this);
    tabs->setMaximumHeight(90);

    auto *log = new QWidget(this);
    auto *logForm = new QFormLayout(log);
    logForm->addRow("Log state", new QLabel("New / not logged", this));
    logForm->addRow("ADIF", new QLabel("ADIF model pending", this));
    logForm->addRow("Transcript", new QLabel("RX/TX transcript will attach here", this));
    tabs->addTab(log, "Log");

    auto *signal = new QWidget(this);
    auto *signalForm = new QFormLayout(signal);
    signalForm->addRow("Measurement window", new QLabel("Report: 5-15 sec rolling average", this));
    signalForm->addRow("Metrics", new QLabel("SNR, IMD, drift, quality, noise floor", this));
    tabs->addTab(signal, "Signal");
    return tabs;
}

QWidget *MainWindow::buildRxTranscriptPanel()
{
    auto *box = new QGroupBox("Live RX (channel 1)", this);
    auto *layout = new QVBoxLayout(box);
    m_rxTranscript = new QPlainTextEdit(this);
    m_rxTranscript->setReadOnly(true);
    m_rxTranscript->setMaximumHeight(70);
    m_rxTranscript->setPlaceholderText(
        "Real demodulated audio appears here. Click the waterfall to tune the RX offset - "
        "there is no AFC yet, so this only locks near the clicked frequency.");
    layout->addWidget(m_rxTranscript);
    return box;
}

QWidget *MainWindow::buildSelectedQsoPanel()
{
    auto *box = new QGroupBox("Selected QSO", this);
    auto *layout = new QHBoxLayout(box);

    m_qsoCallLabel = new QLabel("Call: none", this);
    m_qsoFreqLabel = new QLabel("RF: --   Audio: --", this);
    m_qsoSignalLabel = new QLabel("SNR: --   Q: --   IMD: --", this);

    layout->addWidget(m_qsoCallLabel);
    layout->addWidget(m_qsoFreqLabel);
    layout->addWidget(m_qsoSignalLabel);
    layout->addStretch();
    return box;
}

QWidget *MainWindow::buildTxPanel()
{
    auto *box = new QGroupBox("Transmit Composer", this);
    auto *layout = new QVBoxLayout(box);

    auto *safetyRow = new QHBoxLayout();
    m_targetLabel = new QLabel("Reply target: none", this);
    m_txSafetyLabel = new QLabel("TX Inhibit: no target selected", this);
    m_txSafetyLabel->setObjectName("txInhibit");
    safetyRow->addWidget(m_targetLabel);
    safetyRow->addStretch();
    safetyRow->addWidget(m_txSafetyLabel);
    layout->addLayout(safetyRow);

    auto *macroRow = new QHBoxLayout();
    const QList<QPair<QString, QString>> macros = {
        {"CQ", "CQ CQ CQ DE {MYCALL} {MYCALL} K"},
        {"Answer", "{THEIRCALL} DE {MYCALL} "},
        {"Report", "{THEIRCALL} DE {MYCALL} UR {RST} SNR {SNR} AUDIO {AUDIO_FREQ}HZ IMD {IMD}"},
        {"Name/QTH", "NAME {NAME} QTH {QTH} LOC {LOCATOR}"},
        {"Rig", "RIG {RADIO_MODEL} PWR {PSK_POWER}W ANT {ANTENNA_NAME}"},
        {"73", "{THEIRCALL} DE {MYCALL} 73 SK"}
    };

    for (const auto &macro : macros) {
        auto *button = new QPushButton(macro.first, this);
        connect(button, &QPushButton::clicked, this, [this, macro]() { insertMacro(macro.second); });
        macroRow->addWidget(button);
    }

    m_sendButton = new QPushButton("Send", this);
    m_abortButton = new QPushButton("Abort", this);
    auto *clear = new QPushButton("Clear", this);
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::transmitComposer);
    connect(m_abortButton, &QPushButton::clicked, this, &MainWindow::abortTransmit);
    connect(clear, &QPushButton::clicked, this, [this]() { m_txText->clear(); });
    macroRow->addStretch();
    macroRow->addWidget(m_sendButton);
    macroRow->addWidget(m_abortButton);
    macroRow->addWidget(clear);
    layout->addLayout(macroRow);

    m_txText = new QPlainTextEdit(this);
    m_txText->setPlaceholderText("Click a decoded line to insert THEIRCALL DE MYCALL, then type the reply here.");
    m_txText->setMaximumHeight(110);
    layout->addWidget(m_txText);
    return box;
}

void MainWindow::configureTable(QTableView *view)
{
    view->horizontalHeader()->setStretchLastSection(true);
    view->verticalHeader()->setVisible(false);
    view->verticalHeader()->setDefaultSectionSize(24);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAlternatingRowColors(false);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view->setColumnWidth(0, 34);
    view->setColumnWidth(1, 72);
    view->setColumnWidth(2, 82);
    view->setColumnWidth(3, 62);
    view->setColumnWidth(4, 72);
    view->setColumnWidth(5, 54);
    view->setColumnWidth(6, 48);
}

void MainWindow::handleActiveDecodeClick(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    DecodeLine line = m_activeModel->lineAt(index.row());
    line.callsign = extractCallsign(line.text, line.callsign);
    prepareReply(line);
}

void MainWindow::handleSweeperClick(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    DecodeLine line = m_sweeperModel->lineAt(index.row());
    line.callsign = extractCallsign(line.text, line.callsign);
    prepareReply(line);
    m_activeModel->addOrUpdate(line);
}

void MainWindow::handleWaterfallClick(double audioHz)
{
    m_selectedLine.metrics.audioFrequencyHz = audioHz;
    m_selectedLine.metrics.rfFrequencyMhz = 14.070000 + audioHz / 1000000.0;
    m_liveRxLine.metrics.audioFrequencyHz = audioHz;
    m_liveRxLine.metrics.rfFrequencyMhz = m_selectedLine.metrics.rfFrequencyMhz;
    m_liveRxLine.state = "Searching";
    m_audioEngine->setRxTargetHz(audioHz);
    updateTxSafety();
    statusBar()->showMessage(QString("RX tracking audio frequency %1 Hz").arg(audioHz, 0, 'f', 0), 3000);
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_config, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_config = dialog.config();
        saveSettings();
        m_audioEngine->setDevices(m_config.audio.rxInputDeviceId, m_config.audio.txOutputDeviceId);
        emit requestCatConfigure(m_config);
        if (m_config.cat.autoConnect) {
            emit requestCatStart();
        } else {
            emit requestCatStop();
        }
        updateStatusLabels();
        updateTxSafety();
    }
}

void MainWindow::insertMacro(const QString &macroText)
{
    const QString expanded = MacroEngine::expand(macroText, m_config, m_selectedLine);
    m_txText->insertPlainText(expanded);
}

void MainWindow::transmitComposer()
{
    const QString text = m_txText->toPlainText().trimmed();
    if (text.isEmpty()) {
        statusBar()->showMessage("TX blocked: composer is empty", 3000);
        return;
    }

    const double audioHz = m_selectedLine.metrics.audioFrequencyHz > 0.0
                               ? m_selectedLine.metrics.audioFrequencyHz
                               : 1000.0;
    if (m_catController && m_config.cat.pttMethod.compare("CAT", Qt::CaseInsensitive) == 0) {
        emit requestCatSetPtt(true);
    }
    if (m_audioEngine->transmitBpsk31(text, audioHz)) {
        statusBar()->showMessage(QString("Transmitting BPSK31 at %1 Hz").arg(audioHz, 0, 'f', 0), 5000);
    } else if (m_catController && m_config.cat.pttMethod.compare("CAT", Qt::CaseInsensitive) == 0) {
        emit requestCatSetPtt(false);
    }
}

void MainWindow::abortTransmit()
{
    m_audioEngine->stopTx();
    if (m_catController && m_config.cat.pttMethod.compare("CAT", Qt::CaseInsensitive) == 0) {
        emit requestCatSetPtt(false);
    }
    statusBar()->showMessage("Transmit aborted", 3000);
}

void MainWindow::handleAudioStatus(const QString &status)
{
    if (m_audioLabel) {
        m_audioLabel->setText(status);
    }
}

void MainWindow::handleRxLevel(double rms, double peak)
{
    if (!m_rxLevelLabel) {
        return;
    }

    const double rmsDb = rms > 0.000001 ? 20.0 * std::log10(rms) : -120.0;
    const int peakPercent = static_cast<int>(std::round(std::clamp(peak, 0.0, 1.0) * 100.0));
    m_rxLevelLabel->setText(QString("RX level: %1 dBFS / %2%")
                                .arg(rmsDb, 0, 'f', 1)
                                .arg(peakPercent));
}

void MainWindow::handleTxStarted()
{
    if (m_sendButton) {
        m_sendButton->setEnabled(false);
    }
    if (m_abortButton) {
        m_abortButton->setEnabled(true);
    }
    if (m_txSafetyLabel) {
        m_txSafetyLabel->setObjectName("txSafe");
        m_txSafetyLabel->setText("TX Active: BPSK31 audio");
        m_txSafetyLabel->style()->unpolish(m_txSafetyLabel);
        m_txSafetyLabel->style()->polish(m_txSafetyLabel);
    }
}

void MainWindow::handleTxFinished()
{
    if (m_catController && m_config.cat.pttMethod.compare("CAT", Qt::CaseInsensitive) == 0) {
        emit requestCatSetPtt(false);
    }
    updateTxSafety();
}

void MainWindow::handleRxSignalQuality(double snrDb, double signalLevelDb, double noiseFloorDb)
{
    m_liveRxLine.metrics.snrDb = snrDb;
    m_liveRxLine.metrics.signalLevelDb = signalLevelDb;
    m_liveRxLine.metrics.noiseFloorDb = noiseFloorDb;
    // Heuristic mapping from measured SNR to a 0-100 "quality" figure for
    // the table/row-dimming logic - not itself a measured quantity. 0dB
    // (no discernible signal above the reference noise bin) maps near 0%;
    // 20dB+ (comfortably decodable) maps near 100%.
    const int quality = std::clamp(static_cast<int>(std::lround(snrDb * 5.0)), 0, 100);
    m_liveRxLine.metrics.qualityPercent = quality;
    m_activeModel->addOrUpdate(m_liveRxLine);
    refreshDecodedLines();
}

void MainWindow::handleRxSpectrumReady(const QVector<double> &levels)
{
    if (m_waterfall) {
        m_waterfall->addSpectrumLine(levels);
    }
}

void MainWindow::handleCatStatus(const QString &status)
{
    if (m_catLabel) {
        m_catLabel->setText(status);
    }
}

void MainWindow::handleCatSnapshot(const CatSnapshot &snapshot)
{
    if (!snapshot.connected) {
        return;
    }

    m_catFrequencyHz = snapshot.frequencyHz;
    if (m_vfoLabel) {
        m_vfoLabel->setText(QString("%1 MHz").arg(snapshot.frequencyHz / 1000000.0, 0, 'f', 6));
    }
    m_liveRxLine.metrics.rfFrequencyMhz = (snapshot.frequencyHz + m_liveRxLine.metrics.audioFrequencyHz) / 1000000.0;
    if (m_selectedLine.metrics.audioFrequencyHz > 0.0) {
        m_selectedLine.metrics.rfFrequencyMhz = (snapshot.frequencyHz + m_selectedLine.metrics.audioFrequencyHz) / 1000000.0;
    }
}

void MainWindow::handleBandChanged(const QString &band)
{
    const QMap<QString, double> pskDialHz = {
        {"160m", 1838000.0},
        {"80m", 3580000.0},
        {"40m", 7070000.0},
        {"30m", 10140000.0},
        {"20m", 14070000.0},
        {"17m", 18100000.0},
        {"15m", 21070000.0},
        {"12m", 24920000.0},
        {"10m", 28120000.0},
        {"6m", 50290000.0}
    };

    const double frequencyHz = pskDialHz.value(band, 0.0);
    if (frequencyHz <= 0.0 || !m_catController) {
        return;
    }

    // setFrequencyHz() used to return bool synchronously and this function
    // acted on the result immediately - that only worked because
    // CatController lived on the same thread as MainWindow. Now it lives
    // on a worker thread (see the constructor), so the request is fired
    // off here and the outcome is handled asynchronously in
    // handleCatFrequencyResult() once CatController's frequencySetResult
    // signal arrives. m_pendingBandName carries the band name across that
    // gap so the eventual status message can still name it.
    m_pendingBandName = band;
    emit requestCatSetFrequency(frequencyHz);
}

void MainWindow::handleCatFrequencyResult(bool ok, double frequencyHz)
{
    const QString band = m_pendingBandName;
    m_pendingBandName.clear();

    if (!ok) {
        statusBar()->showMessage(
            band.isEmpty() ? QString("CAT did not accept the requested frequency")
                           : QString("CAT did not accept %1 band frequency").arg(band),
            4000);
        return;
    }

    statusBar()->showMessage(
        band.isEmpty()
            ? QString("CAT set frequency to %1 MHz").arg(frequencyHz / 1000000.0, 0, 'f', 6)
            : QString("CAT set %1 to %2 MHz").arg(band).arg(frequencyHz / 1000000.0, 0, 'f', 6),
        4000);
}

void MainWindow::handleRxTextDecoded(const QString &text)
{
    if (m_rxTranscript) {
        if (text.startsWith(m_liveRxLine.text) && text.size() > m_liveRxLine.text.size()) {
            m_rxTranscript->insertPlainText(text.mid(m_liveRxLine.text.size()));
        } else if (!text.isEmpty() && text != m_liveRxLine.text) {
            // Demodulator buffer was reset (offset changed or trimmed) -
            // this is a discontinuity, not a continuation, so mark it.
            m_rxTranscript->insertPlainText("\n[--- re-sync ---]\n" + text);
        }
        QTextCursor cursor = m_rxTranscript->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_rxTranscript->setTextCursor(cursor);
    }

    m_liveRxLine.text = text;
    m_liveRxLine.state = text.isEmpty() ? "Searching" : "Locked";
    m_liveRxLine.metrics.lockQuality = m_liveRxLine.state;
    m_liveRxLine.callsign = extractCallsign(text, m_liveRxLine.callsign);
    m_activeModel->addOrUpdate(m_liveRxLine);
    refreshDecodedLines();
}

void MainWindow::refreshDecodedLines()
{
    if (m_decodedLines) {
        m_decodedLines->setLines(m_activeModel->lines());
    }
}

QString MainWindow::extractCallsign(const QString &text, const QString &fallback) const
{
    QRegularExpression dePattern("\\bDE\\s+([A-Z0-9]{1,3}[0-9][A-Z0-9]{1,4})\\b", QRegularExpression::CaseInsensitiveOption);
    auto match = dePattern.match(text);
    if (match.hasMatch()) {
        return match.captured(1).toUpper();
    }

    QRegularExpression callPattern("\\b([A-Z0-9]{1,3}[0-9][A-Z0-9]{1,4})\\b", QRegularExpression::CaseInsensitiveOption);
    match = callPattern.match(text);
    if (match.hasMatch()) {
        return match.captured(1).toUpper();
    }
    return fallback.toUpper();
}

void MainWindow::prepareReply(const DecodeLine &line)
{
    m_selectedLine = line;
    if (m_waterfall) {
        m_waterfall->setRxAudioHz(line.metrics.audioFrequencyHz);
        m_waterfall->setTxLockedToRx(true);
    }

    m_qsoCallLabel->setText(QString("Call: %1").arg(line.callsign));
    m_qsoFreqLabel->setText(QString("RF: %1 MHz   Audio: %2 Hz")
                                .arg(line.metrics.rfFrequencyMhz, 0, 'f', 6)
                                .arg(line.metrics.audioFrequencyHz, 0, 'f', 0));
    m_qsoSignalLabel->setText(QString("SNR: %1 dB   Q: %2%   IMD: %3")
                                  .arg(line.metrics.snrDb, 0, 'f', 1)
                                  .arg(line.metrics.qualityPercent)
                                  .arg(line.metrics.imdDb));
    m_targetLabel->setText(QString("Reply target: %1   Mode: %2").arg(line.callsign, line.mode));
    m_txText->setPlainText(QString("%1 DE %2 ").arg(line.callsign, m_config.station.callsign));
    m_txText->setFocus();
    QTextCursor cursor = m_txText->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_txText->setTextCursor(cursor);
    updateTxSafety();
}

void MainWindow::loadSettings()
{
    QSettings settings("2E0LXY", "PSKedge");
    m_config.station.callsign = settings.value("station/callsign", m_config.station.callsign).toString();
    m_config.station.name = settings.value("station/name", m_config.station.name).toString();
    m_config.station.qth = settings.value("station/qth", m_config.station.qth).toString();
    m_config.station.locator = settings.value("station/locator", m_config.station.locator).toString();
    m_config.audio.rxInputDeviceId = settings.value("audio/rxInputDeviceId", m_config.audio.rxInputDeviceId).toString();
    m_config.audio.txOutputDeviceId = settings.value("audio/txOutputDeviceId", m_config.audio.txOutputDeviceId).toString();
    m_config.cat.backend = settings.value("cat/backend", m_config.cat.backend).toString();
    m_config.cat.activeRig = settings.value("cat/activeRig", m_config.cat.activeRig).toInt();
    m_config.cat.pttMethod = settings.value("cat/pttMethod", m_config.cat.pttMethod).toString();
    m_config.cat.pollMs = settings.value("cat/pollMs", m_config.cat.pollMs).toInt();
    m_config.cat.autoConnect = settings.value("cat/autoConnect", m_config.cat.autoConnect).toBool();
    m_config.cat.rig1.enabled = settings.value("cat/rig1/enabled", m_config.cat.rig1.enabled).toBool();
    m_config.cat.rig1.backend = settings.value("cat/rig1/backend", m_config.cat.rig1.backend).toString();
    m_config.cat.rig1.rigSlot = settings.value("cat/rig1/slot", m_config.cat.rig1.rigSlot).toInt();
    m_config.cat.rig1.radioModel = settings.value("cat/rig1/radioModel", settings.value("cat/radioModel", m_config.cat.rig1.radioModel)).toString();
    m_config.cat.rig1.port = settings.value("cat/rig1/port", settings.value("cat/port", m_config.cat.rig1.port)).toString();
    m_config.cat.rig1.baudRate = settings.value("cat/rig1/baudRate", settings.value("cat/baudRate", m_config.cat.rig1.baudRate)).toInt();
    m_config.cat.rig1.host = settings.value("cat/rig1/host", m_config.cat.rig1.host).toString();
    m_config.cat.rig1.tcpPort = settings.value("cat/rig1/tcpPort", m_config.cat.rig1.tcpPort).toInt();
    m_config.cat.rig2.enabled = settings.value("cat/rig2/enabled", m_config.cat.rig2.enabled).toBool();
    m_config.cat.rig2.backend = settings.value("cat/rig2/backend", m_config.cat.rig2.backend).toString();
    m_config.cat.rig2.rigSlot = settings.value("cat/rig2/slot", m_config.cat.rig2.rigSlot).toInt();
    m_config.cat.rig2.radioModel = settings.value("cat/rig2/radioModel", m_config.cat.rig2.radioModel).toString();
    m_config.cat.rig2.port = settings.value("cat/rig2/port", m_config.cat.rig2.port).toString();
    m_config.cat.rig2.baudRate = settings.value("cat/rig2/baudRate", m_config.cat.rig2.baudRate).toInt();
    m_config.cat.rig2.host = settings.value("cat/rig2/host", m_config.cat.rig2.host).toString();
    m_config.cat.rig2.tcpPort = settings.value("cat/rig2/tcpPort", m_config.cat.rig2.tcpPort).toInt();
    m_config.equipment.radioMake = settings.value("equipment/radioMake", m_config.equipment.radioMake).toString();
    m_config.equipment.radioModel = settings.value("equipment/radioModel", m_config.equipment.radioModel).toString();
    m_config.equipment.pskPower = settings.value("equipment/pskPower", m_config.equipment.pskPower).toInt();
    m_config.antenna.name = settings.value("antenna/name", m_config.antenna.name).toString();
    m_config.antenna.type = settings.value("antenna/type", m_config.antenna.type).toString();
}

void MainWindow::saveSettings() const
{
    QSettings settings("2E0LXY", "PSKedge");
    settings.setValue("station/callsign", m_config.station.callsign);
    settings.setValue("station/name", m_config.station.name);
    settings.setValue("station/qth", m_config.station.qth);
    settings.setValue("station/locator", m_config.station.locator);
    settings.setValue("audio/rxInputDeviceId", m_config.audio.rxInputDeviceId);
    settings.setValue("audio/txOutputDeviceId", m_config.audio.txOutputDeviceId);
    settings.setValue("cat/backend", m_config.cat.backend);
    settings.setValue("cat/activeRig", m_config.cat.activeRig);
    settings.setValue("cat/pttMethod", m_config.cat.pttMethod);
    settings.setValue("cat/pollMs", m_config.cat.pollMs);
    settings.setValue("cat/autoConnect", m_config.cat.autoConnect);
    settings.setValue("cat/rig1/enabled", m_config.cat.rig1.enabled);
    settings.setValue("cat/rig1/backend", m_config.cat.rig1.backend);
    settings.setValue("cat/rig1/slot", m_config.cat.rig1.rigSlot);
    settings.setValue("cat/rig1/radioModel", m_config.cat.rig1.radioModel);
    settings.setValue("cat/rig1/port", m_config.cat.rig1.port);
    settings.setValue("cat/rig1/baudRate", m_config.cat.rig1.baudRate);
    settings.setValue("cat/rig1/host", m_config.cat.rig1.host);
    settings.setValue("cat/rig1/tcpPort", m_config.cat.rig1.tcpPort);
    settings.setValue("cat/rig2/enabled", m_config.cat.rig2.enabled);
    settings.setValue("cat/rig2/backend", m_config.cat.rig2.backend);
    settings.setValue("cat/rig2/slot", m_config.cat.rig2.rigSlot);
    settings.setValue("cat/rig2/radioModel", m_config.cat.rig2.radioModel);
    settings.setValue("cat/rig2/port", m_config.cat.rig2.port);
    settings.setValue("cat/rig2/baudRate", m_config.cat.rig2.baudRate);
    settings.setValue("cat/rig2/host", m_config.cat.rig2.host);
    settings.setValue("cat/rig2/tcpPort", m_config.cat.rig2.tcpPort);
    settings.setValue("equipment/radioMake", m_config.equipment.radioMake);
    settings.setValue("equipment/radioModel", m_config.equipment.radioModel);
    settings.setValue("equipment/pskPower", m_config.equipment.pskPower);
    settings.setValue("antenna/name", m_config.antenna.name);
    settings.setValue("antenna/type", m_config.antenna.type);
}

void MainWindow::updateStatusLabels()
{
    const CatRigSettings &rig = m_config.cat.activeRig == 2 ? m_config.cat.rig2 : m_config.cat.rig1;
    m_catLabel->setText(QString("CAT Rig %1: %2").arg(m_config.cat.activeRig).arg(rig.enabled ? rig.backend : "disabled"));
}

void MainWindow::updateTxSafety()
{
    const bool hasCallsign = !m_config.station.callsign.isEmpty() && m_config.station.callsign != "N0CALL";
    const bool hasTarget = !m_selectedLine.callsign.isEmpty();
    const bool hasText = m_txText && !m_txText->toPlainText().trimmed().isEmpty();
    const bool canSend = hasCallsign && hasTarget && hasText;

    if (m_sendButton) {
        m_sendButton->setEnabled(canSend);
    }
    if (m_abortButton) {
        m_abortButton->setEnabled(false);
    }
    if (!m_txSafetyLabel) {
        return;
    }

    if (canSend) {
        m_txSafetyLabel->setObjectName("txSafe");
        m_txSafetyLabel->setText(QString("TX Ready: %1 at %2 Hz").arg(m_selectedLine.callsign).arg(m_selectedLine.metrics.audioFrequencyHz, 0, 'f', 0));
    } else {
        m_txSafetyLabel->setObjectName("txInhibit");
        QStringList reasons;
        if (!hasCallsign) reasons << "set your callsign";
        if (!hasTarget) reasons << "select target";
        if (!hasText) reasons << "enter text";
        m_txSafetyLabel->setText("TX Inhibit: " + reasons.join(", "));
    }

    m_txSafetyLabel->style()->unpolish(m_txSafetyLabel);
    m_txSafetyLabel->style()->polish(m_txSafetyLabel);
}
