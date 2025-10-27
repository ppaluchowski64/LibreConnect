#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QDebug>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    // Inline QML source as a string
    const char *qmlData = R"(
        import QtQuick
        import QtQuick.Controls

        ApplicationWindow {
            width: 400
            height: 300
            visible: true
            title: qsTr("Inline Qt Quick Test")

            Rectangle {
                anchors.fill: parent
                color: "#2b2b2b"

                Text {
                    anchors.centerIn: parent
                    text: "Hello from Inline QML!"
                    color: "white"
                    font.pixelSize: 22
                }
            }
        }
    )";

    QQmlComponent component(&engine);
    component.setData(qmlData, QUrl());
    QObject *object = component.create();

    if (!object)
        qWarning() << component.errors();

    return app.exec();
}
