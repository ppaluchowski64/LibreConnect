import QtQuick
import QtQuick.Controls
import LibreConnect.mobile 1.0

ApplicationWindow {
    width: 420
    height: 720
    visible: true
    title: "LibreConnect Mobile"

    readonly property bool darkMode: Qt.application.styleHints.colorScheme === Qt.ColorScheme.Dark
    color: darkMode ? "#1C1C1C" : "#FFFFFF"

    AndroidAdvertiser {
        id: advertiser
    }

    MobileConnectionController {
        id: conn
    }

    Column {
        anchors.centerIn: parent
        spacing: 16

        Text {
            text: advertiser.running ? "Advertising on LAN" : "Not advertising"
            font.pixelSize: 18
            color: darkMode ? "#F0F0F0" : "#111111"
        }

        Text {
            text: conn.connected ? "Connected" : "Not connected"
            font.pixelSize: 18
            color: darkMode ? "#F0F0F0" : "#111111"
        }

        Rectangle {
            visible: conn.challengeVisible
            width: 320
            height: challengeColumn.implicitHeight + 32
            radius: 8
            color: darkMode ? "#242424" : "#F6F7FB"
            border.color: darkMode ? "#3A3A3A" : "#C7C9D6"
            border.width: 1

            Column {
                id: challengeColumn
                anchors.margins: 16
                anchors.fill: parent
                spacing: 8

                Text {
                    width: parent.width
                    text: conn.pendingDeviceName.length > 0
                        ? ("Connection request from " + conn.pendingDeviceName)
                        : "Connection request"
                    font.pixelSize: 16
                    font.bold: true
                    wrapMode: Text.WordWrap
                    color: darkMode ? "#F0F0F0" : "#111111"
                }

                Text {
                    text: "Enter this code on the desktop app:"
                    font.pixelSize: 14
                    color: darkMode ? "#BEBEBE" : "#333333"
                }

                Text {
                    text: conn.challengeCode
                    font.pixelSize: 28
                    font.bold: true
                    color: darkMode ? "#F0F0F0" : "#111111"
                }
            }
        }

        Button {
            text: advertiser.running ? "Stop advertising" : "Start advertising"
            contentItem: Text {
                text: parent.text
                color: darkMode ? "#F0F0F0" : "#111111"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 18
            }
            background: Rectangle {
                radius: 30
                color: darkMode ? "#2B2B2B" : "#D9D9D9"
                border.color: darkMode ? "#3A3A3A" : "#BEBEBE"
                border.width: 1
            }
            onClicked: {
                if (advertiser.running) advertiser.stop()
                else advertiser.start()
            }
        }

        Text {
            visible: conn.lastError.length > 0
            text: "Error: " + conn.lastError
            color: "#B00020"
            wrapMode: Text.WordWrap
            width: 320
        }
    }

    Component.onCompleted: {
        advertiser.start()
    }
}
