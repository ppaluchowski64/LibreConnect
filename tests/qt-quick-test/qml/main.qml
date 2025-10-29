import QtQuick
import QtQuick.Controls
import "components"

ApplicationWindow {
    width: 400
    height: 300
    visible: true
    title: "Multi-QML Example"

    Dashboard {
        anchors.fill: parent
    }
}