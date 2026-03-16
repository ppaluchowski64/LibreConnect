import QtQuick
import QtQuick.Controls

Page {
    id: root

    required property var windowRef
    readonly property string windowTitleSuffix: "Settings"

    NotificationSyncController {
        id: notificationSyncController
    }

    background: Rectangle {
        color: "white"
    }

    Column {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 22

        Row {
            spacing: 16

            Button {
                text: "Back"
                width: 100
                height: 42
                onClicked: windowRef.goBack()
            }

            Text {
                text: "Settings"
                font.pixelSize: 30
                font.bold: true
                color: "#111111"
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            width: parent.width
            height: 132
            radius: 12
            color: "#f4f4f4"
            border.color: "#d8d8d8"

            Row {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 16

                Column {
                    width: parent.width - syncToggle.width - 20
                    spacing: 8

                    Text {
                        text: "Enable Notification Sync"
                        font.pixelSize: 20
                        font.bold: true
                        color: "#111111"
                    }

                    Text {
                        text: notificationSyncController.statusMessage
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                        color: "#444444"
                        width: parent.width
                    }
                }

                Switch {
                    id: syncToggle
                    anchors.verticalCenter: parent.verticalCenter
                    checked: notificationSyncController.enabled
                    enabled: !notificationSyncController.busy
                    onClicked: notificationSyncController.setNotificationSyncEnabled(checked)
                }
            }
        }
    }
}
