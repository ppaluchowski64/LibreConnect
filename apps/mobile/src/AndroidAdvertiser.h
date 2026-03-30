#pragma once
#include <QObject>

class AndroidAdvertiser : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
public:
    explicit AndroidAdvertiser(QObject* parent = nullptr);
    ~AndroidAdvertiser();

    bool running() const { return m_running; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    Q_INVOKABLE void acquireMulticastLock();
    Q_INVOKABLE void releaseMulticastLock();

signals:
    void runningChanged();

private:
    bool m_running = false;
};
