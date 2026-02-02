#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <QObject>
#include <DebugLog.h>
#include <CameraUtilities.h>


class Functions final : public QObject {
    Q_OBJECT
    public:
    explicit Functions(QObject *parent = nullptr);

    // Button 1: Fetch Specs
    Q_INVOKABLE QString fetchSpecs();

    // Button 2: Get Decoder (passing codec ID as int)
    Q_INVOKABLE QString getDecoder(int codecId);

    // Button 3: Get Encoder (passing codec ID as int)
    Q_INVOKABLE QString getEncoder(int codecId);
};

#endif //FUNCTIONS_H
