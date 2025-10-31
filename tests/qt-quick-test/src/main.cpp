#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/");

    const QUrl url(QStringLiteral("qrc:/main.qml"));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    QObject *root = engine.rootObjects().first();

    qDebug() << (root == nullptr);

    QObject *dashboard = root->findChild<QObject*>("dashboard");
    QObject *increaseButton = root->findChild<QObject*>("IncreaseButton");

    if (increaseButton) {
        increaseButton->setProperty("color", "#ff9800");
    }

    return app.exec();
}