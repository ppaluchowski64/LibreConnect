import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 360
    height: 640
    title: "Qt Quick Android Example"

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Label {
            text: "Hello from Qt Quick 👋"
            font.pixelSize: 22
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
        }

        Button {
            text: "Tap me"
            onClicked: statusLabel.text = "Button tapped!"
        }

        Label {
            id: statusLabel
            text: "Waiting for input…"
            color: "#666"
            Layout.alignment: Qt.AlignHCenter
        }
    }
}