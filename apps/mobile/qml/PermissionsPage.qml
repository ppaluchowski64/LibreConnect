import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var continueToHomeCallback
    readonly property bool allPermissionsGranted:
        page.conn.cameraPermissionGranted
        && page.conn.notificationSendPermissionGranted
        && page.conn.notificationListenerPermissionGranted
        && page.conn.filePermissionGranted
        && page.conn.allFilesPermissionGranted
        && page.conn.batteryPermissionGranted
        && page.conn.smsReceivePermissionGranted
        && page.conn.smsReadPermissionGranted
        && page.conn.smsSendPermissionGranted
        && page.conn.contactsPermissionGranted

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
                text: "Allow Permissions"
                color: Theme.textColor
                font.pixelSize: 24
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: "Grant permissions for camera, notifications, files, and SMS features. You can skip and grant them later."
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
                            text: "Camera"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.cameraPermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.cameraPermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestCameraPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Notification Sending"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.notificationSendPermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.notificationSendPermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestNotificationSendPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Notification Listener"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.notificationListenerPermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.notificationListenerPermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestNotificationListenerPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Files"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.filePermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.filePermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestFilePermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "All Files Access"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.allFilesPermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.allFilesPermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestAllFilesPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Battery Optimization"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.batteryPermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.batteryPermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestBatteryPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "SMS Receive"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.smsReceivePermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.smsReceivePermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestSmsReceivePermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "SMS Read"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.smsReadPermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.smsReadPermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestSmsReadPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "SMS Send"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.smsSendPermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.smsSendPermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestSmsSendPermission()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Contacts (for SMS names)"
                            color: Theme.textColor
                            font.pixelSize: 15
                        }
                        Button {
                            text: page.conn.contactsPermissionGranted ? "Granted" : "Grant"
                            enabled: !page.conn.contactsPermissionGranted && !page.conn.permissionsBusy
                            onClicked: page.conn.requestContactsPermission()
                        }
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: page.conn.permissionsBusy ? "Requesting..." : "Grant All"
                enabled: !page.conn.permissionsBusy
                onClicked: page.conn.requestAllPermissions()
            }

            Button {
                Layout.fillWidth: true
                text: "Continue"
                enabled: page.allPermissionsGranted && !page.conn.permissionsBusy
                onClicked: {
                    page.conn.completePermissionsOnboarding()
                    page.continueToHomeCallback()
                }
            }

            Button {
                Layout.fillWidth: true
                text: "Skip For Now"
                onClicked: {
                    page.conn.completePermissionsOnboarding()
                    page.continueToHomeCallback()
                }
            }
        }
    }
}
