#pragma once
#include <QObject>

class AndroidAdvertiser : public QObject
{
    Q_OBJECT
public:
    explicit AndroidAdvertiser(QObject* parent = nullptr);
    ~AndroidAdvertiser();

    Q_INVOKABLE void acquireMulticastLock();
    Q_INVOKABLE void releaseMulticastLock();

private:
    bool m_hasLock = false;
};
