import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var goBackCallback

    background: Rectangle {
        color: Theme.backgroundColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            ToolButton {
                icon.source: Theme.dark
                             ? "qrc:/LibreConnect/mobile/back_dark.svg"
                             : "qrc:/LibreConnect/mobile/back.svg"
                icon.width: 24
                icon.height: 24
                display: AbstractButton.IconOnly
                onClicked: page.goBackCallback()
            }

            Text {
                Layout.fillWidth: true
                text: "Paired Devices"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            Item {
                width: 32
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: page.conn.pairedDevices
            spacing: 8

            delegate: Rectangle {
                required property var modelData

                width: ListView.view.width
                radius: 10
                border.width: 1
                border.color: Theme.panelBorderColor
                color: Theme.panelColor
                implicitHeight: itemRow.implicitHeight + 16

                RowLayout {
                    id: itemRow
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Column {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: modelData.deviceName
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            text: modelData.deviceType + "  |  " + modelData.deviceId
                            color: Theme.mutedTextColor
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            width: Math.max(0, parent.width - 4)
                        }
                    }

                    Button {
                        text: "Unpair"
                        onClicked: page.conn.removePairedDevice(modelData.deviceId)
                        contentItem: Text {
                            text: parent.text
                            color: Theme.destructiveColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
