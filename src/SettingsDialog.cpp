#include "SettingsDialog.h"

#include <QAudioDevice>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QMediaDevices>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

QComboBox *backendCombo(const QString &current, QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->addItems({"Hamlib rigctld", "OmniRig", "Manual/no CAT"});
    const int index = combo->findText(current, Qt::MatchFixedString);
    combo->setCurrentIndex(index >= 0 ? index : 0);
    return combo;
}

QSpinBox *rigSlotSpin(int value, QWidget *parent)
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(1, 2);
    spin->setValue(value);
    return spin;
}

QSpinBox *tcpPortSpin(int value, QWidget *parent)
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(1, 65535);
    spin->setValue(value);
    return spin;
}

QSpinBox *baudSpin(int value, QWidget *parent)
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(1200, 921600);
    spin->setValue(value);
    return spin;
}

void populateAudioDevices(QComboBox *combo, const QList<QAudioDevice> &devices, const QString &selectedId)
{
    combo->addItem("System default", QString());
    for (const QAudioDevice &device : devices) {
        combo->addItem(device.description(), QString::fromUtf8(device.id()));
    }

    const int selectedIndex = combo->findData(selectedId);
    if (selectedIndex >= 0) {
        combo->setCurrentIndex(selectedIndex);
    }
}

bool backendIsOmniRig(const QString &backend)
{
    return backend.contains("omni", Qt::CaseInsensitive);
}

bool backendIsHamlib(const QString &backend)
{
    return backend.contains("hamlib", Qt::CaseInsensitive) || backend.contains("rigctld", Qt::CaseInsensitive);
}

void updateBackendPanels(QComboBox *backend, QWidget *omniRigPanel, QWidget *hamlibPanel)
{
    const QString value = backend->currentText();
    omniRigPanel->setVisible(backendIsOmniRig(value));
    hamlibPanel->setVisible(backendIsHamlib(value));
}

} // namespace

SettingsDialog::SettingsDialog(const AppConfig &config, QWidget *parent)
    : QDialog(parent), m_config(config)
{
    setWindowTitle("Setup");
    resize(640, 460);

    auto *tabs = new QTabWidget(this);

    auto *stationPage = new QWidget(this);
    auto *stationForm = new QFormLayout(stationPage);
    m_callsign = new QLineEdit(config.station.callsign);
    m_name = new QLineEdit(config.station.name);
    m_qth = new QLineEdit(config.station.qth);
    m_locator = new QLineEdit(config.station.locator);
    stationForm->addRow("Callsign", m_callsign);
    stationForm->addRow("Name", m_name);
    stationForm->addRow("QTH", m_qth);
    stationForm->addRow("Locator", m_locator);
    tabs->addTab(stationPage, "Station");

    auto *audioPage = new QWidget(this);
    auto *audioForm = new QFormLayout(audioPage);
    m_rxInputDevice = new QComboBox(this);
    m_txOutputDevice = new QComboBox(this);
    populateAudioDevices(m_rxInputDevice, QMediaDevices::audioInputs(), config.audio.rxInputDeviceId);
    populateAudioDevices(m_txOutputDevice, QMediaDevices::audioOutputs(), config.audio.txOutputDeviceId);
    audioForm->addRow("RX input soundcard", m_rxInputDevice);
    audioForm->addRow("TX output soundcard", m_txOutputDevice);
    tabs->addTab(audioPage, "Audio");

    auto *catPage = new QWidget(this);
    auto *catLayout = new QVBoxLayout(catPage);
    auto *catForm = new QFormLayout();
    m_activeRig = new QComboBox(this);
    m_activeRig->addItems({"Rig 1", "Rig 2"});
    m_activeRig->setCurrentIndex(config.cat.activeRig == 2 ? 1 : 0);
    m_catAutoConnect = new QCheckBox(this);
    m_catAutoConnect->setChecked(config.cat.autoConnect);
    m_catPollMs = new QSpinBox(this);
    m_catPollMs->setRange(250, 10000);
    m_catPollMs->setValue(config.cat.pollMs);
    catForm->addRow("Active rig", m_activeRig);
    catForm->addRow("Connect on startup", m_catAutoConnect);
    catForm->addRow("Poll interval ms", m_catPollMs);
    catLayout->addLayout(catForm);

    auto *rig1Box = new QGroupBox("Rig 1", this);
    auto *rig1Form = new QFormLayout(rig1Box);
    m_rig1Enabled = new QCheckBox(this);
    m_rig1Enabled->setChecked(config.cat.rig1.enabled);
    m_rig1Backend = backendCombo(config.cat.rig1.backend, this);
    m_rig1Slot = rigSlotSpin(config.cat.rig1.rigSlot, this);
    m_rig1Host = new QLineEdit(config.cat.rig1.host, this);
    m_rig1TcpPort = tcpPortSpin(config.cat.rig1.tcpPort, this);
    m_rig1Model = new QLineEdit(config.cat.rig1.radioModel, this);
    m_rig1Port = new QLineEdit(config.cat.rig1.port, this);
    m_rig1Baud = baudSpin(config.cat.rig1.baudRate, this);
    rig1Form->addRow("Enabled", m_rig1Enabled);
    rig1Form->addRow("Backend", m_rig1Backend);
    m_rig1OmniRigPanel = new QWidget(this);
    auto *rig1OmniForm = new QFormLayout(m_rig1OmniRigPanel);
    rig1OmniForm->setContentsMargins(0, 0, 0, 0);
    rig1OmniForm->addRow("OmniRig rig", m_rig1Slot);
    rig1Form->addRow("OmniRig", m_rig1OmniRigPanel);
    m_rig1HamlibPanel = new QWidget(this);
    auto *rig1HamlibForm = new QFormLayout(m_rig1HamlibPanel);
    rig1HamlibForm->setContentsMargins(0, 0, 0, 0);
    rig1HamlibForm->addRow("rigctld host", m_rig1Host);
    rig1HamlibForm->addRow("rigctld port", m_rig1TcpPort);
    rig1HamlibForm->addRow("Radio model/id", m_rig1Model);
    rig1HamlibForm->addRow("Serial/USB port", m_rig1Port);
    rig1HamlibForm->addRow("Baud", m_rig1Baud);
    rig1Form->addRow("Hamlib", m_rig1HamlibPanel);
    catLayout->addWidget(rig1Box);

    auto *rig2Box = new QGroupBox("Rig 2", this);
    auto *rig2Form = new QFormLayout(rig2Box);
    m_rig2Enabled = new QCheckBox(this);
    m_rig2Enabled->setChecked(config.cat.rig2.enabled);
    m_rig2Backend = backendCombo(config.cat.rig2.backend, this);
    m_rig2Slot = rigSlotSpin(config.cat.rig2.rigSlot == 1 ? 2 : config.cat.rig2.rigSlot, this);
    m_rig2Host = new QLineEdit(config.cat.rig2.host, this);
    m_rig2TcpPort = tcpPortSpin(config.cat.rig2.tcpPort == 4532 ? 4533 : config.cat.rig2.tcpPort, this);
    m_rig2Model = new QLineEdit(config.cat.rig2.radioModel, this);
    m_rig2Port = new QLineEdit(config.cat.rig2.port, this);
    m_rig2Baud = baudSpin(config.cat.rig2.baudRate, this);
    rig2Form->addRow("Enabled", m_rig2Enabled);
    rig2Form->addRow("Backend", m_rig2Backend);
    m_rig2OmniRigPanel = new QWidget(this);
    auto *rig2OmniForm = new QFormLayout(m_rig2OmniRigPanel);
    rig2OmniForm->setContentsMargins(0, 0, 0, 0);
    rig2OmniForm->addRow("OmniRig rig", m_rig2Slot);
    rig2Form->addRow("OmniRig", m_rig2OmniRigPanel);
    m_rig2HamlibPanel = new QWidget(this);
    auto *rig2HamlibForm = new QFormLayout(m_rig2HamlibPanel);
    rig2HamlibForm->setContentsMargins(0, 0, 0, 0);
    rig2HamlibForm->addRow("rigctld host", m_rig2Host);
    rig2HamlibForm->addRow("rigctld port", m_rig2TcpPort);
    rig2HamlibForm->addRow("Radio model/id", m_rig2Model);
    rig2HamlibForm->addRow("Serial/USB port", m_rig2Port);
    rig2HamlibForm->addRow("Baud", m_rig2Baud);
    rig2Form->addRow("Hamlib", m_rig2HamlibPanel);
    catLayout->addWidget(rig2Box);
    connect(m_rig1Backend, &QComboBox::currentTextChanged, this, [this]() {
        updateBackendPanels(m_rig1Backend, m_rig1OmniRigPanel, m_rig1HamlibPanel);
    });
    connect(m_rig2Backend, &QComboBox::currentTextChanged, this, [this]() {
        updateBackendPanels(m_rig2Backend, m_rig2OmniRigPanel, m_rig2HamlibPanel);
    });
    updateBackendPanels(m_rig1Backend, m_rig1OmniRigPanel, m_rig1HamlibPanel);
    updateBackendPanels(m_rig2Backend, m_rig2OmniRigPanel, m_rig2HamlibPanel);
    catLayout->addStretch();
    tabs->addTab(catPage, "CAT/PTT");

    auto *radioPage = new QWidget(this);
    auto *radioForm = new QFormLayout(radioPage);
    m_rigMake = new QLineEdit(config.equipment.radioMake);
    m_rigModel = new QLineEdit(config.equipment.radioModel);
    m_pskPower = new QSpinBox();
    m_pskPower->setRange(1, 1500);
    m_pskPower->setValue(config.equipment.pskPower);
    radioForm->addRow("Radio make", m_rigMake);
    radioForm->addRow("Radio model", m_rigModel);
    radioForm->addRow("Normal PSK power", m_pskPower);
    tabs->addTab(radioPage, "Radio");

    auto *antennaPage = new QWidget(this);
    auto *antennaForm = new QFormLayout(antennaPage);
    m_antennaName = new QLineEdit(config.antenna.name);
    m_antennaType = new QLineEdit(config.antenna.type);
    antennaForm->addRow("Antenna name", m_antennaName);
    antennaForm->addRow("Antenna type", m_antennaType);
    tabs->addTab(antennaPage, "Antenna");

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

AppConfig SettingsDialog::config() const
{
    AppConfig updated = m_config;
    updated.station.callsign = m_callsign->text().trimmed().toUpper();
    updated.station.name = m_name->text().trimmed();
    updated.station.qth = m_qth->text().trimmed();
    updated.station.locator = m_locator->text().trimmed().toUpper();
    updated.audio.rxInputDeviceId = m_rxInputDevice->currentData().toString();
    updated.audio.txOutputDeviceId = m_txOutputDevice->currentData().toString();
    updated.cat.activeRig = m_activeRig->currentIndex() + 1;
    updated.cat.autoConnect = m_catAutoConnect->isChecked();
    updated.cat.pollMs = m_catPollMs->value();
    updated.cat.backend = m_activeRig->currentIndex() == 0 ? m_rig1Backend->currentText() : m_rig2Backend->currentText();
    updated.cat.rig1.enabled = m_rig1Enabled->isChecked();
    updated.cat.rig1.backend = m_rig1Backend->currentText();
    updated.cat.rig1.rigSlot = m_rig1Slot->value();
    updated.cat.rig1.host = m_rig1Host->text().trimmed();
    updated.cat.rig1.tcpPort = m_rig1TcpPort->value();
    updated.cat.rig1.radioModel = m_rig1Model->text().trimmed();
    updated.cat.rig1.port = m_rig1Port->text().trimmed();
    updated.cat.rig1.baudRate = m_rig1Baud->value();
    updated.cat.rig2.enabled = m_rig2Enabled->isChecked();
    updated.cat.rig2.backend = m_rig2Backend->currentText();
    updated.cat.rig2.rigSlot = m_rig2Slot->value();
    updated.cat.rig2.host = m_rig2Host->text().trimmed();
    updated.cat.rig2.tcpPort = m_rig2TcpPort->value();
    updated.cat.rig2.radioModel = m_rig2Model->text().trimmed();
    updated.cat.rig2.port = m_rig2Port->text().trimmed();
    updated.cat.rig2.baudRate = m_rig2Baud->value();
    updated.equipment.radioMake = m_rigMake->text().trimmed();
    updated.equipment.radioModel = m_rigModel->text().trimmed();
    updated.equipment.pskPower = m_pskPower->value();
    updated.antenna.name = m_antennaName->text().trimmed();
    updated.antenna.type = m_antennaType->text().trimmed();
    return updated;
}
