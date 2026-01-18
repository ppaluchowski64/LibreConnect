#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <QObject>
#include <DebugLog.h>
#include <CameraUtilities.h>


class Functions final : public QObject {
    Q_OBJECT
public:
    explicit Functions(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void FetchCamerasSpecificationCall();
};

#endif //FUNCTIONS_H
