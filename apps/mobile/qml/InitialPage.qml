import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

Page {
    id: page

    required property var conn
    required property var showPairedDevicesCallback

    background: Rectangle {
        color: Theme.backgroundColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        Item {
            id: logoWrap
            Layout.alignment: Qt.AlignHCenter
            width: 96
            height: 96
            readonly property real cornerRadius: width * 0.22

            Image {
                id: logoSource
                anchors.fill: parent
                source: "qrc:/LibreConnect/mobile/libreconnect_logo_1024.png"
                sourceSize.width: 1024
                sourceSize.height: 1024
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                visible: false
            }

            OpacityMask {
                anchors.fill: parent
                source: logoSource
                maskSource: Rectangle {
                    width: logoWrap.width
                    height: logoWrap.height
                    radius: logoWrap.cornerRadius
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "Connect With LibreConnect Desktop"
            color: Theme.textColor
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 24
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: "This phone is visible on your local network while the app is running."
            color: Theme.mutedTextColor
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 15
        }

        Rectangle {
            Layout.fillWidth: true
            radius: 10
            border.width: 1
            border.color: Theme.panelBorderColor
            color: Theme.panelColor
            implicitHeight: networkInfoColumn.implicitHeight + 20

            Column {
                id: networkInfoColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Text {
                    text: "Device: " + (page.conn.localDeviceName.length > 0 ? page.conn.localDeviceName : "Unavailable")
                    color: Theme.textColor
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }

                Text {
                    text: "IP: " + (page.conn.localIpAddress.length > 0 ? page.conn.localIpAddress : "Unavailable")
                    color: Theme.textColor
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }
            }
        }

        Button {
            Layout.fillWidth: true
            text: page.conn.hasPairedDevices ? "Paired Devices" : "No Paired Devices"
            enabled: page.conn.hasPairedDevices
            onClicked: page.showPairedDevicesCallback()
        }

        Item {
            Layout.fillHeight: true
        }

        Text {
            Layout.fillWidth: true
            visible: page.conn.lastError.length > 0
            text: page.conn.lastError
            color: Theme.destructiveColor
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }
}
