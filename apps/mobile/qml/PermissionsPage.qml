import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var continueToHomeCallback
    readonly property bool smsAndContactsPermissionsGranted:
        page.conn.smsReceivePermissionGranted
        && page.conn.smsReadPermissionGranted
        && page.conn.smsSendPermissionGranted
        && page.conn.contactsPermissionGranted
    readonly property bool allPermissionsGranted:
        page.conn.cameraPermissionGranted
        && page.conn.microphonePermissionGranted
        && page.conn.notificationSendPermissionGranted
        && page.conn.notificationListenerPermissionGranted
        && page.conn.allFilesPermissionGranted
        && page.conn.batteryPermissionGranted
        && page.smsAndContactsPermissionsGranted

    background: Rectangle {
        color: Theme.backgroundColor
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            x: 16
            y: 16
            width: Math.max(parent.width - 32, 320)
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: qsTr("Allow Permissions")
                color: Theme.textColor
                font.pixelSize: 24
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Grant permissions for camera, microphone, notifications, all-files access, and SMS features. You can skip and grant them later.")
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Frame {
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Camera")
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.cameraPermissionGranted ? qsTr("Granted") : qsTr("Grant")
                            enabled: !page.conn.cameraPermissionGranted && !page.conn.permissionsBusy
                            Material.elevation: 1
                            onClicked: page.conn.requestCameraPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Microphone")
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.microphonePermissionGranted ? qsTr("Granted") : qsTr("Grant")
                            enabled: !page.conn.microphonePermissionGranted && !page.conn.permissionsBusy
                            Material.elevation: 1
                            onClicked: page.conn.requestMicrophonePermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Notification Sending")
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.notificationSendPermissionGranted ? qsTr("Granted") : qsTr("Grant")
                            enabled: !page.conn.notificationSendPermissionGranted && !page.conn.permissionsBusy
                            Material.elevation: 1
                            onClicked: page.conn.requestNotificationSendPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Notification Listener")
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.notificationListenerPermissionGranted ? qsTr("Granted") : qsTr("Grant")
                            enabled: !page.conn.notificationListenerPermissionGranted && !page.conn.permissionsBusy
                            Material.elevation: 1
                            onClicked: page.conn.requestNotificationListenerPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("All Files Access")
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.allFilesPermissionGranted ? qsTr("Granted") : qsTr("Grant")
                            enabled: !page.conn.allFilesPermissionGranted && !page.conn.permissionsBusy
                            Material.elevation: 1
                            onClicked: page.conn.requestAllFilesPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Battery Optimization")
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.batteryPermissionGranted ? qsTr("Granted") : qsTr("Grant")
                            enabled: !page.conn.batteryPermissionGranted && !page.conn.permissionsBusy
                            Material.elevation: 1
                            onClicked: page.conn.requestBatteryPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("SMS + Contacts")
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.smsAndContactsPermissionsGranted ? qsTr("Granted") : qsTr("Grant")
                            enabled: !page.smsAndContactsPermissionsGranted && !page.conn.permissionsBusy
                            Material.elevation: 1
                            onClicked: page.conn.requestSmsPermissions()
                        }
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: page.conn.permissionsBusy ? qsTr("Requesting...") : qsTr("Grant All")
                visible: !page.allPermissionsGranted
                enabled: !page.conn.permissionsBusy
                Material.elevation: 1
                onClicked: page.conn.requestAllPermissions()
            }

            Button {
                Layout.fillWidth: true
                text: qsTr("Skip For Now")
                visible: !page.allPermissionsGranted
                Material.elevation: 1
                onClicked: {
                    page.conn.completePermissionsOnboarding()
                    page.continueToHomeCallback()
                }
            }

            Button {
                Layout.fillWidth: true
                text: qsTr("Continue")
                highlighted: true
                Material.elevation: 2
                enabled: page.allPermissionsGranted && !page.conn.permissionsBusy
                onClicked: {
                    page.conn.completePermissionsOnboarding()
                    page.continueToHomeCallback()
                }
            }
        }
    }
}
