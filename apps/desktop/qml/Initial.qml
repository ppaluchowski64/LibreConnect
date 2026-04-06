import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
    readonly property string windowTitleSuffix: "Setup"

    background: Rectangle {
        color: "white"
    }

    Image {
        id: logo
        source: "qrc:/LibreConnect/desktop/libreconnect_logo.png"
        width: 140
        height: 140
        fillMode: Image.PreserveAspectFit
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 24
    }

    Text {
        id: title
        text: "LibreConnect"
        anchors.right: logo.left
        anchors.top: parent.top
        anchors.margins: 32
        font.pixelSize: 44
        color: "black"
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: title.bottom
        anchors.margins: 32
        rightPadding: logo.width + 24
        spacing: 12

        Text {
            text: "Thank you for downloading LibreConnect!\nLet\u2019s set up your phone now."
            font.pixelSize: 20
            wrapMode: Text.WordWrap
            color: "#111111"
        }

        Text {
            text: "(Please ensure the app for your relevant OS is \ndownloaded and open before you continue.)"
            font.pixelSize: 20
            font.italic: true
            wrapMode: Text.WordWrap
            color: "#333333"
        }
    }

    Button {
        id: nextButton
        text: "Next"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        width: 160
        height: 56
        font.pixelSize: 20
        onClicked: windowRef.showDevicePicker(false)
    }
}
