#include "CatController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QTcpSocket>

#include <algorithm>
#include <cmath>

CatController::CatController(QObject *parent)
    : QObject(parent)
{
    connect(&m_pollTimer, &QTimer::timeout, this, &CatController::poll);
}

void CatController::configure(const AppConfig &config)
{
    m_config = config;
    m_activeRig = std::clamp(config.cat.activeRig, 1, 2);
    m_pollTimer.setInterval(std::max(250, config.cat.pollMs));
    if (m_running) {
        poll();
    }
}

void CatController::start()
{
    m_running = true;
    m_pollTimer.start(std::max(250, m_config.cat.pollMs));
    poll();
}

void CatController::stop()
{
    m_pollTimer.stop();
    m_running = false;
    emit statusChanged("CAT Offline");
}

void CatController::setActiveRig(int rig)
{
    m_activeRig = std::clamp(rig, 1, 2);
    m_config.cat.activeRig = m_activeRig;
    if (m_running) {
        poll();
    }
}

void CatController::setFrequencyHz(double frequencyHz)
{
    const CatRigSettings &settings = settingsForRig(m_activeRig);
    if (!settings.enabled) {
        emit statusChanged(QString("CAT Rig %1 disabled").arg(m_activeRig));
        emit frequencySetResult(false, frequencyHz);
        return;
    }

    bool ok = false;
    if (isRigctldBackend(settings.backend)) {
        ok = setRigctldFrequency(settings, frequencyHz);
    } else if (isOmniRigBackend(settings.backend)) {
        ok = setOmniRigFrequency(settings.rigSlot, frequencyHz);
    }

    emit statusChanged(ok ? QString("CAT Rig %1 set %2 MHz").arg(m_activeRig).arg(frequencyHz / 1000000.0, 0, 'f', 6)
                          : QString("CAT Rig %1 frequency set failed").arg(m_activeRig));
    emit frequencySetResult(ok, frequencyHz);
    if (ok) {
        poll();
    }
}

void CatController::setPtt(bool enabled)
{
    const CatRigSettings &settings = settingsForRig(m_activeRig);
    bool ok = false;
    if (isRigctldBackend(settings.backend)) {
        ok = setRigctldPtt(settings, enabled);
    } else if (isOmniRigBackend(settings.backend)) {
        ok = setOmniRigPtt(settings.rigSlot, enabled);
    }
    emit statusChanged(ok ? QString("CAT Rig %1 PTT %2").arg(m_activeRig).arg(enabled ? "TX" : "RX")
                          : QString("CAT Rig %1 PTT failed").arg(m_activeRig));
}

void CatController::poll()
{
    const CatRigSettings &settings = settingsForRig(m_activeRig);
    CatSnapshot snapshot;
    snapshot.rig = m_activeRig;
    snapshot.backend = settings.backend;

    if (!settings.enabled) {
        snapshot.status = QString("CAT Rig %1 disabled").arg(m_activeRig);
        writeCatLog(snapshot.status);
        emit snapshotChanged(snapshot);
        emit statusChanged(snapshot.status);
        return;
    }

    if (isRigctldBackend(settings.backend)) {
        snapshot = pollRigctld(m_activeRig, settings);
    } else if (isOmniRigBackend(settings.backend)) {
        snapshot = pollOmniRig(m_activeRig, settings);
    } else {
        snapshot.status = QString("CAT Rig %1 unsupported backend").arg(m_activeRig);
    }

    writeCatLog(snapshot.status);
    emit snapshotChanged(snapshot);
    emit statusChanged(snapshot.status);
}

const CatRigSettings &CatController::settingsForRig(int rig) const
{
    return rig == 2 ? m_config.cat.rig2 : m_config.cat.rig1;
}

CatSnapshot CatController::pollRigctld(int rig, const CatRigSettings &settings) const
{
    CatSnapshot snapshot;
    snapshot.rig = rig;
    snapshot.backend = settings.backend;

    const QString freqReply = rigctldCommand(settings, "f");
    writeCatLog(QString("Rig %1 rigctld f -> %2").arg(rig).arg(freqReply));
    bool freqOk = false;
    const double frequency = freqReply.section('\n', 0, 0).trimmed().toDouble(&freqOk);
    if (!freqOk) {
        snapshot.status = QString("CAT Rig %1 rigctld offline").arg(rig);
        return snapshot;
    }

    const QString modeReply = rigctldCommand(settings, "m");
    writeCatLog(QString("Rig %1 rigctld m -> %2").arg(rig).arg(modeReply));
    snapshot.connected = true;
    snapshot.frequencyHz = frequency;
    snapshot.mode = modeReply.section('\n', 0, 0).trimmed();
    snapshot.status = QString("CAT Rig %1 %2 %3 MHz %4")
                          .arg(rig)
                          .arg(settings.backend)
                          .arg(frequency / 1000000.0, 0, 'f', 6)
                          .arg(snapshot.mode);
    return snapshot;
}

CatSnapshot CatController::pollOmniRig(int rig, const CatRigSettings &settings) const
{
    CatSnapshot snapshot;
    snapshot.rig = rig;
    snapshot.backend = settings.backend;

#ifdef Q_OS_WIN
    const int omniRig = std::clamp(settings.rigSlot, 1, 2);
    const QString script = QString(
        "$ErrorActionPreference='Stop';"
        "$o=New-Object -ComObject OmniRig.OmniRigX;"
        "$r=$o.Rig%1;"
        "$rx=0;"
        "try { $rx=$r.GetRxFrequency() } catch { $rx=0 };"
        "[Console]::Out.WriteLine('Status=' + [string]$r.Status);"
        "[Console]::Out.WriteLine('StatusStr=' + [string]$r.StatusStr);"
        "[Console]::Out.WriteLine('Freq=' + [string]$r.Freq);"
        "[Console]::Out.WriteLine('FreqA=' + [string]$r.FreqA);"
        "[Console]::Out.WriteLine('FreqB=' + [string]$r.FreqB);"
        "[Console]::Out.WriteLine('RxFrequency=' + [string]$rx);"
        "[Console]::Out.WriteLine('Mode=' + [string]$r.Mode);")
                               .arg(omniRig);
    const QString reply = runPowerShell(script);
    writeCatLog(QString("Rig %1 OmniRig%2 raw -> %3").arg(rig).arg(omniRig).arg(reply));

    QMap<QString, QString> values;
    const QStringList lines = reply.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int equals = line.indexOf('=');
        if (equals > 0) {
            values.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }

    bool freqOk = false;
    double frequency = values.value("RxFrequency").toDouble(&freqOk);
    if (!freqOk || frequency <= 0.0) {
        frequency = values.value("FreqA").toDouble(&freqOk);
    }
    if (!freqOk || frequency <= 0.0) {
        frequency = values.value("FreqB").toDouble(&freqOk);
    }
    if (!freqOk || frequency <= 0.0) {
        frequency = values.value("Freq").toDouble(&freqOk);
    }

    const QString statusText = values.value("StatusStr", values.value("Status", "unknown"));
    if (!freqOk || frequency <= 0.0) {
        snapshot.status = QString("CAT Rig %1 OmniRig%2 no valid frequency (%3)")
                              .arg(rig)
                              .arg(omniRig)
                              .arg(statusText);
        return snapshot;
    }

    snapshot.connected = true;
    snapshot.frequencyHz = frequency;
    snapshot.mode = values.value("Mode");
    snapshot.status = QString("CAT Rig %1 OmniRig%2 %3 MHz %4 mode %5")
                          .arg(rig)
                          .arg(omniRig)
                          .arg(frequency / 1000000.0, 0, 'f', 6)
                          .arg(statusText)
                          .arg(snapshot.mode);
#else
    Q_UNUSED(settings);
    snapshot.status = QString("CAT Rig %1 OmniRig is Windows only").arg(rig);
#endif

    return snapshot;
}

bool CatController::setRigctldPtt(const CatRigSettings &settings, bool enabled) const
{
    const QString reply = rigctldCommand(settings, QString("T %1").arg(enabled ? 1 : 0));
    return !reply.contains("RPRT -", Qt::CaseInsensitive);
}

bool CatController::setRigctldFrequency(const CatRigSettings &settings, double frequencyHz) const
{
    const QString command = QString("F %1").arg(static_cast<qint64>(std::llround(frequencyHz)));
    const QString reply = rigctldCommand(settings, command, 1000);
    writeCatLog(QString("rigctld set frequency %1 -> %2").arg(command).arg(reply));
    return !reply.contains("RPRT -", Qt::CaseInsensitive);
}

bool CatController::setOmniRigFrequency(int rig, double frequencyHz) const
{
#ifdef Q_OS_WIN
    const int omniRig = std::clamp(rig, 1, 2);
    const qint64 hz = static_cast<qint64>(std::llround(frequencyHz));
    const QString script = QString(
        "$ErrorActionPreference='Stop';"
        "$o=New-Object -ComObject OmniRig.OmniRigX;"
        "$r=$o.Rig%1;"
        "$r.FreqA=%2;"
        "try { $r.Freq=%2 } catch { };"
        "Start-Sleep -Milliseconds 150;"
        "$rx=0;"
        "try { $rx=$r.GetRxFrequency() } catch { $rx=0 };"
        "[Console]::Out.WriteLine('Freq=' + [string]$r.Freq);"
        "[Console]::Out.WriteLine('FreqA=' + [string]$r.FreqA);"
        "[Console]::Out.WriteLine('RxFrequency=' + [string]$rx);"
        "[Console]::Out.WriteLine('StatusStr=' + [string]$r.StatusStr);")
                               .arg(omniRig)
                               .arg(hz);
    const QString reply = runPowerShell(script, 2500);
    writeCatLog(QString("OmniRig%1 set frequency %2 -> %3").arg(omniRig).arg(hz).arg(reply));
    return reply.contains(QString::number(hz));
#else
    Q_UNUSED(rig);
    Q_UNUSED(frequencyHz);
    return false;
#endif
}

bool CatController::setOmniRigPtt(int rig, bool enabled) const
{
#ifdef Q_OS_WIN
    const int omniRig = std::clamp(rig, 1, 2);
    const QString script = QString(
        "$ErrorActionPreference='Stop';"
        "$o=New-Object -ComObject OmniRig.OmniRigX;"
        "$r=$o.Rig%1;"
        "$r.Tx=%2;")
                               .arg(omniRig)
                               .arg(enabled ? 1 : 0);
    runPowerShell(script);
    writeCatLog(QString("OmniRig%1 PTT %2").arg(omniRig).arg(enabled ? "TX" : "RX"));
    return true;
#else
    Q_UNUSED(rig);
    Q_UNUSED(enabled);
    return false;
#endif
}

QString CatController::rigctldCommand(const CatRigSettings &settings, const QString &command, int timeoutMs) const
{
    QTcpSocket socket;
    socket.connectToHost(settings.host, static_cast<quint16>(settings.tcpPort));
    if (!socket.waitForConnected(timeoutMs)) {
        writeCatLog(QString("rigctld connect failed %1:%2").arg(settings.host).arg(settings.tcpPort));
        return {};
    }

    socket.write(command.toUtf8());
    socket.write("\n");
    if (!socket.waitForBytesWritten(timeoutMs) || !socket.waitForReadyRead(timeoutMs)) {
        writeCatLog(QString("rigctld command timeout %1:%2 command=%3").arg(settings.host).arg(settings.tcpPort).arg(command));
        return {};
    }

    QByteArray reply = socket.readAll();
    while (socket.waitForReadyRead(50)) {
        reply += socket.readAll();
    }
    return QString::fromUtf8(reply).trimmed();
}

QString CatController::runPowerShell(const QString &script, int timeoutMs) const
{
    QProcess process;
    process.start("powershell.exe", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script});
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(250);
        writeCatLog("PowerShell CAT bridge timeout");
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        writeCatLog(QString("PowerShell CAT bridge failed: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
        return {};
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

void CatController::writeCatLog(const QString &message) const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        return;
    }

    QDir dir(base);
    if (!dir.exists() && !dir.mkpath(".")) {
        return;
    }

    QFile file(dir.filePath("cat.log"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " " << message << '\n';
}

bool CatController::isRigctldBackend(const QString &backend)
{
    return backend.contains("rigctld", Qt::CaseInsensitive) || backend.contains("hamlib", Qt::CaseInsensitive);
}

bool CatController::isOmniRigBackend(const QString &backend)
{
    return backend.contains("omnirig", Qt::CaseInsensitive) || backend.contains("omni", Qt::CaseInsensitive);
}
