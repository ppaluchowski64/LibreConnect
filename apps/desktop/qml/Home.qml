import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
    required property var connectionController
    readonly property string windowTitleSuffix: ""

    background: Rectangle {
        color: Theme.backgroundColor
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
        font.family: Theme.fontFamily
        font.pixelSize: 38
        color: Theme.textColor
    }

    Column {
        id: primaryActions
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: statusTitle.bottom
        anchors.topMargin: 28
        spacing: 18

        ThemedButton {
            text: "File Manager"
            width: 150
            height: 46
            onClicked: windowRef.showFileManager()
        }

        ThemedButton {
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

        ThemedButton {
            text: "Settings"
            width: 136
            height: 42
            onClicked: windowRef.showSettings()
        }

        ThemedButton {
            text: "Disconnect"
            width: 136
            height: 42
            onClicked: connectionController.disconnect()
        }

        ThemedButton {
            text: "Exit"
            width: 136
            height: 42
            onClicked: Qt.quit()
        }
    }
}
