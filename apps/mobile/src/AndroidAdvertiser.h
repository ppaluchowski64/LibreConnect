#pragma once
#include <QObject>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include <Scanner.h>
#include <ConnectionManager.h>

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
    bool m_hasLock = false;
    bool m_running = false;

#ifdef Q_OS_ANDROID
    QJniObject m_multicastLock;
#endif
};
