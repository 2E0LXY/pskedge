#include "MainWindow.h"

#include "MacroEngine.h"
#include "SettingsDialog.h"
#include "WaterfallWidget.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
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

    setWindowTitle("PSK Analyzer Pro - v1.0.0 beta");
    resize(1480, 900);

    auto *settingsAction = new QAction("Setup", this);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    menuBar()->addMenu("File")->addAction(settingsAction);

    m_activeModel = new DecoderTableModel(DecoderTableModel::Mode::ActiveDecoders, this);
    m_sweeperModel = new DecoderTableModel(DecoderTableModel::Mode::SweeperCandidates, this);
    m_audioEngine = new AudioEngine(this);
    connect(m_audioEngine, &AudioEngine::statusChanged, this, &MainWindow::handleAudioStatus);
    connect(m_audioEngine, &AudioEngine::rxLevelChanged, this, &MainWindow::handleRxLevel);
    connect(m_audioEngine, &AudioEngine::txStarted, this, &MainWindow::handleTxStarted);
    connect(m_audioEngine, &AudioEngine::txFinished, this, &MainWindow::handleTxFinished);

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
    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 2);
    mainLayout->addWidget(mainSplitter, 1);
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

    connect(&m_decoder, &MockDecoder::decoded, this, &MainWindow::addDecodedLine);
    connect(m_txText, &QPlainTextEdit::textChanged, this, &MainWindow::updateTxSafety);
    m_decoder.start();
    m_audioEngine->startRx();
    updateStatusLabels();
    updateTxSafety();
}

QWidget *MainWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 0, 4, 0);

    auto *setup = new QPushButton("Setup", this);
    connect(setup, &QPushButton::clicked, this, &MainWindow::openSettings);

    m_catLabel = new QLabel("CAT Offline", this);
    m_audioLabel = new QLabel("Audio: starting", this);
    m_rxLevelLabel = new QLabel("RX level: --", this);
    m_vfoLabel = new QLabel("14.070.000 MHz", this);
    m_vfoLabel->setObjectName("vfo");
    auto *mode = new QLabel("Mode: BPSK31   BW: 60 Hz   RX   TX/RX Locked", this);

    layout->addWidget(setup);
    layout->addWidget(m_catLabel);
    layout->addWidget(m_audioLabel);
    layout->addWidget(m_rxLevelLabel);
    layout->addStretch();
    layout->addWidget(m_vfoLabel);
    layout->addStretch();
    layout->addWidget(mode);
    return bar;
}

QWidget *MainWindow::buildRightPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *activeBox = new QGroupBox("Active Decoders", this);
    auto *activeLayout = new QVBoxLayout(activeBox);
    m_activeView = new QTableView(this);
    m_activeView->setModel(m_activeModel);
    configureTable(m_activeView);
    connect(m_activeView, &QTableView::clicked, this, &MainWindow::handleActiveDecodeClick);
    activeLayout->addWidget(m_activeView);

    auto *sweeperBox = new QGroupBox("Band Sweeper Candidates", this);
    auto *sweeperLayout = new QVBoxLayout(sweeperBox);
    m_sweeperView = new QTableView(this);
    m_sweeperView->setModel(m_sweeperModel);
    configureTable(m_sweeperView);
    connect(m_sweeperView, &QTableView::clicked, this, &MainWindow::handleSweeperClick);
    sweeperLayout->addWidget(m_sweeperView);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(activeBox);
    splitter->addWidget(sweeperBox);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter);
    return panel;
}

QWidget *MainWindow::buildWorkflowPanel()
{
    auto *tabs = new QTabWidget(this);

    auto *reply = new QWidget(this);
    auto *replyLayout = new QVBoxLayout(reply);
    replyLayout->addWidget(buildSelectedQsoPanel());
    replyLayout->addWidget(buildTxPanel());
    tabs->addTab(reply, "Reply");

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
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAlternatingRowColors(false);
    view->setColumnWidth(0, 34);
    view->setColumnWidth(1, 72);
    view->setColumnWidth(2, 82);
    view->setColumnWidth(3, 62);
    view->setColumnWidth(4, 72);
    view->setColumnWidth(5, 54);
    view->setColumnWidth(6, 48);
}

void MainWindow::addDecodedLine(const DecodeLine &line)
{
    DecodeLine active = line;
    active.metrics.lockQuality = line.metrics.qualityPercent > 78 ? "Locked" : "Marginal";
    m_activeModel->addOrUpdate(active);

    DecodeLine candidate = line;
    candidate.metrics.lockQuality = line.text.contains("CQ", Qt::CaseInsensitive) ? "CQ" : "Candidate";
    m_sweeperModel->addOrUpdate(candidate);
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
    updateTxSafety();
    statusBar()->showMessage(QString("Selected RX audio frequency %1 Hz").arg(audioHz, 0, 'f', 0), 3000);
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_config, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_config = dialog.config();
        saveSettings();
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
    if (m_audioEngine->transmitBpsk31(text, audioHz)) {
        statusBar()->showMessage(QString("Transmitting BPSK31 at %1 Hz").arg(audioHz, 0, 'f', 0), 5000);
    }
}

void MainWindow::abortTransmit()
{
    m_audioEngine->stopTx();
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
    updateTxSafety();
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
    m_qsoSignalLabel->setText(QString("SNR: %1 dB   Q: %2%   IMD: %3 dB")
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
    QSettings settings("OpenAI-Codex", "PSKAnalyzer");
    m_config.station.callsign = settings.value("station/callsign", m_config.station.callsign).toString();
    m_config.station.name = settings.value("station/name", m_config.station.name).toString();
    m_config.station.qth = settings.value("station/qth", m_config.station.qth).toString();
    m_config.station.locator = settings.value("station/locator", m_config.station.locator).toString();
    m_config.cat.backend = settings.value("cat/backend", m_config.cat.backend).toString();
    m_config.cat.radioModel = settings.value("cat/radioModel", m_config.cat.radioModel).toString();
    m_config.cat.port = settings.value("cat/port", m_config.cat.port).toString();
    m_config.cat.baudRate = settings.value("cat/baudRate", m_config.cat.baudRate).toInt();
    m_config.equipment.radioMake = settings.value("equipment/radioMake", m_config.equipment.radioMake).toString();
    m_config.equipment.radioModel = settings.value("equipment/radioModel", m_config.equipment.radioModel).toString();
    m_config.equipment.pskPower = settings.value("equipment/pskPower", m_config.equipment.pskPower).toInt();
    m_config.antenna.name = settings.value("antenna/name", m_config.antenna.name).toString();
    m_config.antenna.type = settings.value("antenna/type", m_config.antenna.type).toString();
}

void MainWindow::saveSettings() const
{
    QSettings settings("OpenAI-Codex", "PSKAnalyzer");
    settings.setValue("station/callsign", m_config.station.callsign);
    settings.setValue("station/name", m_config.station.name);
    settings.setValue("station/qth", m_config.station.qth);
    settings.setValue("station/locator", m_config.station.locator);
    settings.setValue("cat/backend", m_config.cat.backend);
    settings.setValue("cat/radioModel", m_config.cat.radioModel);
    settings.setValue("cat/port", m_config.cat.port);
    settings.setValue("cat/baudRate", m_config.cat.baudRate);
    settings.setValue("equipment/radioMake", m_config.equipment.radioMake);
    settings.setValue("equipment/radioModel", m_config.equipment.radioModel);
    settings.setValue("equipment/pskPower", m_config.equipment.pskPower);
    settings.setValue("antenna/name", m_config.antenna.name);
    settings.setValue("antenna/type", m_config.antenna.type);
}

void MainWindow::updateStatusLabels()
{
    m_catLabel->setText(QString("CAT: %1").arg(m_config.cat.backend));
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
