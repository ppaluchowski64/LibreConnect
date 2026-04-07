import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: root

    readonly property string windowTitleSuffix: ""

    background: Rectangle {
        color: Theme.backgroundColor
    }

    Image {
        id: logo
        source: "libreconnect_logo.png"
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
        font.family: Theme.fontFamily
        font.pixelSize: 44
        color: Theme.textColor
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: title.bottom
        anchors.margins: 32
        rightPadding: logo.width + 24
        spacing: 12
        Text {
            text: "Connected!"
            font.family: Theme.fontFamily
            font.pixelSize: 20
            font.bold: true
            color: Theme.textColor
        }

        Text {
            text: "Follow the instructions on your mobile device to\nenable the relevant app permissions."
            font.family: Theme.fontFamily
            font.pixelSize: 20
            color: Theme.textColor
        }
    }

    ThemedButton {
        id: homeButton
        text: "Home"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        width: 160
        height: 56
        font.pixelSize: 20

        onClicked: {
            console.log("Home clicked")
        }
    }
}
