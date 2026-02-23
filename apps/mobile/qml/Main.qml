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
