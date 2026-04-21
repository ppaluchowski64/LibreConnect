#pragma once

#include <QObject>
#include <QPointer>
#include <QEvent>
#include <QString>

#include <ConnectionManager.h>
#include <Events.h>
#include <BaseModule.h>

class MobileConnectionController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString challengeCode READ challengeCode NOTIFY challengeCodeChanged)
    Q_PROPERTY(bool challengeVisible READ challengeVisible NOTIFY challengeVisibleChanged)
    Q_PROPERTY(QString pendingDeviceName READ pendingDeviceName NOTIFY pendingDeviceNameChanged)

public:
    explicit MobileConnectionController(QObject* parent = nullptr);

    bool connected() const { return m_connected; }
    QString lastError() const { return m_lastError; }
    QString challengeCode() const { return m_challengeCode; }
    bool challengeVisible() const { return m_challengeVisible; }
    QString pendingDeviceName() const { return m_pendingDeviceName; }

    signals:
        void connectedChanged();
    void lastErrorChanged();
    void challengeCodeChanged();
    void challengeVisibleChanged();
    void pendingDeviceNameChanged();

    void incomingConnection(QString deviceName);

protected:
    bool event(QEvent* e) override;

private:
    void setError(const QString& e);
    void clearChallenge();
    void handleModuleErrorEvent(ModuleErrorEvent* ev);

private:
    bool m_connected = false;
    QString m_lastError;
    QString m_challengeCode;
    bool m_challengeVisible = false;
    QString m_pendingDeviceName;
};
