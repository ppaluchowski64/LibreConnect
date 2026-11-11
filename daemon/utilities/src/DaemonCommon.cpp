#include <DaemonCommon.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

QString DaemonUtils::listPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/daemon/auto-connect-list.JSON");
}

QString DaemonUtils::daemonPath() {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + QStringLiteral("/LibreConnect-daemon.exe");
#else
#ifndef Q_OS_MACOS
    const QString launcher = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../libreconnect-daemon-run.sh"));
    if (QFileInfo(launcher).isExecutable()) {
        return launcher;
    }
#endif
    return QCoreApplication::applicationDirPath() + QStringLiteral("/LibreConnect-daemon");
#endif
}

QJsonArray DaemonUtils::readList(bool* valid) {
    QFile file(listPath());
    if (valid) {
        *valid = false;
    }
    if (!file.exists()) {
        if (valid) {
            *valid = true;
        }
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray()) {
        return {};
    }
    if (valid) {
        *valid = true;
    }
    return document.array();
}

bool DaemonUtils::configureStartup(const bool enabled) {
#ifdef Q_OS_WIN
    QSettings startup(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if (enabled) {
        startup.setValue(QStringLiteral("LibreConnectDaemon"), QStringLiteral("\"") + QDir::toNativeSeparators(daemonPath()) + QStringLiteral("\""));
    } else {
        startup.remove(QStringLiteral("LibreConnectDaemon"));
    }
    startup.sync();
    return startup.status() == QSettings::NoError;
#else
#ifdef Q_OS_MACOS
    const QString path = QDir::homePath() + QStringLiteral("/Library/LaunchAgents/org.libreconnect.daemon.plist");
    const QByteArray contents = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict><key>Label</key><string>org.libreconnect.daemon</string><key>ProgramArguments</key><array><string>%1</string></array><key>RunAtLoad</key><true/></dict></plist>").arg(daemonPath().toHtmlEscaped()).toUtf8();
#else
    const QString path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/libreconnect-daemon.desktop");
    QString escapedPath = daemonPath();
    escapedPath.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escapedPath.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    escapedPath.replace(QStringLiteral("%"), QStringLiteral("%%"));
    escapedPath.replace(QStringLiteral("$"), QStringLiteral("\\$"));
    escapedPath.replace(QStringLiteral("`"), QStringLiteral("\\`"));
    const QByteArray contents = QStringLiteral("[Desktop Entry]\nType=Application\nName=LibreConnect daemon\nExec=\"%1\"\nTerminal=false\n").arg(escapedPath).toUtf8();
#endif
    if (!enabled) {
        return !QFile::exists(path) || QFile::remove(path);
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.commit();
#endif
}

bool DaemonUtils::SetAutoConnect(const std::string& uuid, const bool enabled) {
    const QString id = QString::fromStdString(uuid);
    if (id.isEmpty() || !QDir().mkpath(QFileInfo(listPath()).absolutePath())) {
        return false;
    }

    QLockFile lock(listPath() + QStringLiteral(".lock"));
    if (!lock.tryLock(2000)) {
        return false;
    }

    bool valid = false;
    const QJsonArray devices = readList(&valid);
    if (!valid) {
        return false;
    }
    QJsonArray updated;
    for (const QJsonValue& device : devices) {
        if (device.toString() != id) {
            updated.append(device);
        }
    }
    if (enabled) {
        updated.append(id);
        if (!RegisterApplication() || !StartDaemon()) {
            return false;
        }
    }

    if (!configureStartup(!updated.isEmpty())) {
        return false;
    }
    QSaveFile file(listPath());
    const QByteArray contents = QJsonDocument(updated).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        configureStartup(!devices.isEmpty());
        return false;
    }
    return true;
}

bool DaemonUtils::IsAutoConnectEnabled(const std::string& uuid) {
    return readList().contains(QString::fromStdString(uuid));
}

bool DaemonUtils::RegisterApplication() {
    const QString applicationPath = QCoreApplication::applicationFilePath();
    if (applicationPath.isEmpty()) {
        return false;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("daemon/applicationPath"), applicationPath);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QString DaemonUtils::ApplicationPath() {
    const QString applicationPath = QSettings().value(QStringLiteral("daemon/applicationPath")).toString();
    const QFileInfo info(applicationPath);
    if (info.isAbsolute() && info.isFile() && info.isExecutable()) {
        return info.absoluteFilePath();
    }

    return {};
}

bool DaemonUtils::StartApplication(const QString& address, const uint16_t port, const QString& deviceId, QString* error) {
    QString binary = ApplicationPath();
    if (binary.isEmpty()) {
#ifdef Q_OS_WIN
        binary = QCoreApplication::applicationDirPath() + QStringLiteral("/LibreConnect.exe");
#else
        binary = QCoreApplication::applicationDirPath() + QStringLiteral("/LibreConnect");
        if (!QFileInfo::exists(binary)) {
            binary = QCoreApplication::applicationDirPath() + QStringLiteral("/usr/bin/LibreConnect");
        }
#endif
    }

    QProcess process;
    process.setProgram(binary);
    process.setArguments({QStringLiteral("--port"), QString::number(port),
        QStringLiteral("--address"), address, QStringLiteral("--device-id"), deviceId,
        QStringLiteral("--hidden")});
    process.setWorkingDirectory(QFileInfo(binary).absolutePath());
    if (!process.startDetached()) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(binary, process.errorString());
        }
        return false;
    }
    return true;
}

bool DaemonUtils::StartDaemon() {
    return QProcess::startDetached(daemonPath(), {}, QCoreApplication::applicationDirPath());
}
