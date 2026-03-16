import QtQuick
import QtQuick.Controls

Page {
    id: root

    readonly property string windowTitleSuffix: ""

    background: Rectangle {
        color: "white"
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
            text: "Connected!"
            font.pixelSize: 20
            font.bold: true
            color: "#111111"
        }

        Text {
            text: "Follow the instructions on your mobile device to\nenable the relevant app permissions."
            font.pixelSize: 20
            color: "#111111"
        }
    }

    Button {
        id: homeButton
        text: "Home"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        width: 160
        height: 56
        font.pixelSize: 20

        onClicked: {
            // TODO: home screen!
            console.log("Home clicked")
        }
    }
}
