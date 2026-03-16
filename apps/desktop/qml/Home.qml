import QtQuick
import QtQuick.Controls

Page {
    id: root

    required property var windowRef
    required property var connectionController
    readonly property string windowTitleSuffix: ""

    background: Rectangle {
        color: "white"
    }

    Image {
        id: logo
        source: "libreconnect_logo.png"
        width: 112
        height: 112
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 20
        fillMode: Image.PreserveAspectFit
    }

    Text {
        id: statusTitle
        text: "Connected"
        anchors.top: logo.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        font.pixelSize: 38
        color: "#111111"
    }

    Column {
        id: primaryActions
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: statusTitle.bottom
        anchors.topMargin: 28
        spacing: 18

        Button {
            text: "File Manager"
            width: 150
            height: 46
            onClicked: windowRef.showFileManager()
        }

        Button {
            text: "Cameras"
            width: 150
            height: 46
            onClicked: windowRef.showVirtualCamera()
        }
    }

    Row {
        id: footerActions
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 18

        Button {
            text: "Settings"
            width: 136
            height: 42
            onClicked: windowRef.showSettings()
        }

        Button {
            text: "Disconnect"
            width: 136
            height: 42
            onClicked: connectionController.disconnect()
        }

        Button {
            text: "Exit"
            width: 136
            height: 42
            onClicked: Qt.quit()
        }
    }
}
