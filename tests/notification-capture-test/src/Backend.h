#ifndef NOTIFICATIONLISTENER_KT_BACKEND_H
#define NOTIFICATIONLISTENER_KT_BACKEND_H

#include <QCoreApplication>
#include <QObject>

class Backend : public QObject
{
    Q_OBJECT
public:
    explicit Backend(QObject *parent = nullptr) : QObject(parent) {}

    signals:
        void notification(QString message);
};

#endif //NOTIFICATIONLISTENER_KT_BACKEND_H