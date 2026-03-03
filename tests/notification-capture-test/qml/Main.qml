import QtQuick
import QtQuick.Controls
import LibreConnect.mobile 1.0

ApplicationWindow {
    width: 420
    height: 720
    visible: true
    title: "LibreConnect Mobile"

    Backend {
        id: myBackend
    }

    Column {
        anchors.centerIn: parent
        spacing: 16

        Button {
            text: "Send Notification"
            onClicked: {
                myBackend.notification("Hello from QML!")
            }

        }
    }
}
