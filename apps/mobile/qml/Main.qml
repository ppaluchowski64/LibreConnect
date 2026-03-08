import QtQuick
import QtQuick.Controls
import LibreConnect.mobile 1.0

ApplicationWindow {
    width: 420
    height: 720
    visible: true
    title: "LibreConnect Mobile"

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
        }

        Text {
            text: conn.connected ? "Connected" : "Not connected"
            font.pixelSize: 18
        }

        Rectangle {
            visible: conn.challengeVisible
            width: 320
            height: challengeColumn.implicitHeight + 32
            radius: 8
            color: "#f6f7fb"
            border.color: "#c7c9d6"
            border.width: 1

            Column {
                id: challengeColumn
                anchors.margins: 16
                anchors.fill: parent
                spacing: 8

                Text {
                    text: conn.pendingDeviceName.length > 0
                          ? ("Connection request from " + conn.pendingDeviceName)
                          : "Connection request"
                    font.pixelSize: 16
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    text: "Enter this code on the desktop app:"
                    font.pixelSize: 14
                    color: "#333333"
                }

                Text {
                    text: conn.challengeCode
                    font.pixelSize: 28
                    font.bold: true
                }
            }
        }

        Button {
            text: advertiser.running ? "Stop advertising" : "Start advertising"
            onClicked: {
                if (advertiser.running) advertiser.stop()
                else advertiser.start()
            }
        }

        Text {
            visible: conn.lastError.length > 0
            text: "Error: " + conn.lastError
            color: "red"
            wrapMode: Text.WordWrap
            width: 320
        }
    }

    Component.onCompleted: {
        advertiser.start()
    }
}
