#pragma once

#include <QObject>
#include <QPointer>
#include <QEvent>
#include <QString>

#include <ConnectionManager.h>
#include <Events.h>

class MobileConnectionController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit MobileConnectionController(QObject* parent = nullptr);

    bool connected() const { return m_connected; }
    QString lastError() const { return m_lastError; }

    signals:
        void connectedChanged();
    void lastErrorChanged();

    void incomingConnection(QString deviceName);

protected:
    bool event(QEvent* e) override;

private:
    void setError(const QString& e);

private:
    bool m_connected = false;
    QString m_lastError;
};
