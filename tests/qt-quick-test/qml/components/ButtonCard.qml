import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    property alias label: labelText.text
    signal clicked()

    width: 150
    height: 50
    radius: 8
    color: "#cccccc"

    Text {
        id: labelText
        anchors.centerIn: parent
        font.pixelSize: 16
        color: "white"
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
        hoverEnabled: true
        onEntered: root.opacity = 0.8
        onExited: root.opacity = 1.0
    }
}