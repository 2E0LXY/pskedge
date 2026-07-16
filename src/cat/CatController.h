#pragma once

#include "AppConfig.h"

#include <QObject>
#include <QTimer>

struct CatSnapshot {
    int rig = 1;
    bool connected = false;
    double frequencyHz = 0.0;
    QString mode;
    QString backend;
    QString status;
};

Q_DECLARE_METATYPE(CatSnapshot)

class CatController : public QObject {
    Q_OBJECT

public:
    explicit CatController(QObject *parent = nullptr);

public slots:
    // These run blocking network/process I/O (rigctld TCP round-trips,
    // OmniRig via a spawned PowerShell process). CatController is designed
    // to live on a worker thread (see MainWindow, which moveToThread()s it)
    // specifically so these blocks don't freeze the GUI thread - calling
    // them directly from another thread instead of via a queued
    // signal/slot connection would defeat that and cause a data race on
    // m_config/m_activeRig, so they're slots (invokable via queued
    // connection), not plain public methods.
    void configure(const AppConfig &config);
    void start();
    void stop();
    void setActiveRig(int rig);
    void setFrequencyHz(double frequencyHz);
    void setPtt(bool enabled);

signals:
    void statusChanged(const QString &status);
    void snapshotChanged(const CatSnapshot &snapshot);
    // setFrequencyHz() used to return bool synchronously - that only
    // worked because the caller and callee were on the same thread. Moved
    // to an async result signal so callers on another thread (the normal
    // case now) get the outcome without a blocking cross-thread call.
    void frequencySetResult(bool ok, double frequencyHz);

private slots:
    void poll();

private:
    const CatRigSettings &settingsForRig(int rig) const;
    CatSnapshot pollRigctld(int rig, const CatRigSettings &settings) const;
    CatSnapshot pollOmniRig(int rig, const CatRigSettings &settings) const;
    bool setRigctldPtt(const CatRigSettings &settings, bool enabled) const;
    bool setOmniRigPtt(int rig, bool enabled) const;
    bool setRigctldFrequency(const CatRigSettings &settings, double frequencyHz) const;
    bool setOmniRigFrequency(int rig, double frequencyHz) const;
    QString rigctldCommand(const CatRigSettings &settings, const QString &command, int timeoutMs = 500) const;
    QString runPowerShell(const QString &script, int timeoutMs = 1500) const;
    void writeCatLog(const QString &message) const;
    static bool isRigctldBackend(const QString &backend);
    static bool isOmniRigBackend(const QString &backend);

    AppConfig m_config;
    QTimer m_pollTimer;
    int m_activeRig = 1;
    bool m_running = false;
};
