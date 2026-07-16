#pragma once

#include "AppConfig.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const AppConfig &config, QWidget *parent = nullptr);
    AppConfig config() const;

private:
    AppConfig m_config;
    QLineEdit *m_callsign = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_qth = nullptr;
    QLineEdit *m_locator = nullptr;
    QComboBox *m_rxInputDevice = nullptr;
    QComboBox *m_txOutputDevice = nullptr;
    QComboBox *m_activeRig = nullptr;
    QCheckBox *m_catAutoConnect = nullptr;
    QSpinBox *m_catPollMs = nullptr;
    QCheckBox *m_rig1Enabled = nullptr;
    QComboBox *m_rig1Backend = nullptr;
    QWidget *m_rig1OmniRigPanel = nullptr;
    QWidget *m_rig1HamlibPanel = nullptr;
    QSpinBox *m_rig1Slot = nullptr;
    QLineEdit *m_rig1Host = nullptr;
    QSpinBox *m_rig1TcpPort = nullptr;
    QLineEdit *m_rig1Model = nullptr;
    QLineEdit *m_rig1Port = nullptr;
    QSpinBox *m_rig1Baud = nullptr;
    QCheckBox *m_rig2Enabled = nullptr;
    QComboBox *m_rig2Backend = nullptr;
    QWidget *m_rig2OmniRigPanel = nullptr;
    QWidget *m_rig2HamlibPanel = nullptr;
    QSpinBox *m_rig2Slot = nullptr;
    QLineEdit *m_rig2Host = nullptr;
    QSpinBox *m_rig2TcpPort = nullptr;
    QLineEdit *m_rig2Model = nullptr;
    QLineEdit *m_rig2Port = nullptr;
    QSpinBox *m_rig2Baud = nullptr;
    QLineEdit *m_rigMake = nullptr;
    QLineEdit *m_rigModel = nullptr;
    QSpinBox *m_pskPower = nullptr;
    QLineEdit *m_antennaName = nullptr;
    QLineEdit *m_antennaType = nullptr;
};
