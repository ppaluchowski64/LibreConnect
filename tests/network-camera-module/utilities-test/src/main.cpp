#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <Functions.h>
#include <qqmlcontext.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    const QUrl url = QUrl("qrc:/NetworkCameraModuleUtilitiesTest/Main.qml");

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

    Functions functions;
    engine.rootContext()->setContextProperty("functions", &functions);

    engine.load(url);
    return app.exec();
}
