#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDir>
#include <QDebug>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    const QUrl url(QStringLiteral("qrc:/MainWindow.qml"));
    engine.addImportPath("qrc:");
    QDir resDir(":");

    qDebug() << "gtfs";
    foreach (const QString &entry, resDir.entryList(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot)) {
        qDebug() << "  - " << entry;
    }

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                 &app, [url](const QObject *obj, const QUrl &objUrl) {
    if (!obj && url == objUrl)
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
