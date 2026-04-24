import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
    required property var connectionController
    readonly property string windowTitleSuffix: "Setup"

    background: Rectangle {
        color: Theme.backgroundColor
    }

    RoundedLogo {
        id: logo
        source: "qrc:/LibreConnect/desktop/libreconnect_logo_1024.png"
        width: 140
        height: 140
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
            text: "Thank you for downloading LibreConnect!\nLet\u2019s set up your phone now."
            font.family: Theme.fontFamily
            font.pixelSize: 20
            wrapMode: Text.WordWrap
            color: Theme.textColor
        }

        Text {
            text: "(Please ensure the app for your relevant OS is \ndownloaded and open before you continue.)"
            font.family: Theme.fontFamily
            font.pixelSize: 20
            font.italic: true
            wrapMode: Text.WordWrap
            color: Theme.mutedTextColor
        }
    }

    ThemedButton {
        id: nextButton
        text: "Next"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        width: 160
        height: 56
        font.pixelSize: 20
        onClicked: {
            if (Qt.platform.os === "osx" && !connectionController.localNetworkPermissionGranted) {
                localNetworkDialog.open()
                return
            }

            windowRef.showDevicePicker(false)
        }
    }

    Dialog {
        id: localNetworkDialog
        modal: true
        title: ""
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 420
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        Column {
            width: parent.width
            spacing: 12

            Text {
                width: parent.width
                text: "Allow Local Network Access"
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "LibreConnect needs macOS local network access before it can scan for nearby phones."
                font.family: Theme.fontFamily
                font.pixelSize: 16
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "After you continue, macOS will show its Local Network permission prompt."
                font.family: Theme.fontFamily
                font.pixelSize: 14
                color: Theme.mutedTextColor
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 10

                ThemedButton {
                    text: "Not Now"
                    width: 140
                    height: 44
                    onClicked: localNetworkDialog.close()
                }

                ThemedButton {
                    text: "Continue"
                    width: 140
                    height: 44
                    onClicked: {
                        localNetworkDialog.close()
                        windowRef.showDevicePicker(false)
                    }
                }
            }
        }
    }
}
