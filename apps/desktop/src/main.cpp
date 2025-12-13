#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDirIterator>
#include <QtQml>
#include "DeviceDiscovery.h"
#include "DeviceModel.h"


int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    qmlRegisterType<DeviceDiscovery>("LibreConnect.desktop", 1, 0, "DeviceDiscovery");
    qmlRegisterType<DeviceModel>("LibreConnect.desktop", 1, 0, "DeviceModel");
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    const QUrl url = QUrl("qrc:/LibreConnect/desktop/MainWindow.qml");

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                 [url](QObject *obj, const QUrl& urld) {
                     if (!obj) {
                         qDebug() << "Failed to load QML at" << urld;
                     }

                     if (url != urld) {
                         qDebug() << "Qml url does not match url" << url;
                     }

                     qDebug() << "Qml object created";
                 },
                 Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
