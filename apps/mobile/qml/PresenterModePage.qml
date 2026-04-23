import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var remoteInputController
    required property var goBackCallback

    background: Rectangle {
        color: Theme.backgroundColor
    }

    Dialog {
        id: permissionDialog
        property string message: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentWidth: Math.min(page.width - 32, 360)

        Overlay.modal: Rectangle {
            color: Theme.dark ? "#99000000" : "#73000000"
        }

        background: Rectangle {
            radius: 16
            color: Theme.panelColor
            border.width: 1
            border.color: Theme.panelBorderColor
        }

        contentItem: Column {
            spacing: 12
            width: permissionDialog.contentWidth

            Text {
                width: parent.width
                text: "Permission Required"
                color: Theme.textColor
                font.pixelSize: 20
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: permissionDialog.message
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Button {
                width: parent.width
                text: "OK"
                onClicked: permissionDialog.close()
            }
        }
    }

    Connections {
        target: remoteInputController

        function onAccessibilityPermissionRequired(message) {
            permissionDialog.message = message
            permissionDialog.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

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
                text: "Presenter Mode"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            Item {
                width: 32
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true

            background: Rectangle {
                radius: 16
                color: Theme.panelColor
                border.width: 1
                border.color: Theme.panelBorderColor
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 10

                    Button {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        onClicked: remoteInputController.presenterPreviousSlide()
                        background: Rectangle {
                            radius: 18
                            color: Theme.buttonColor
                            border.width: 1
                            border.color: Theme.panelBorderColor
                        }
                        contentItem: ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            Image {
                                Layout.alignment: Qt.AlignHCenter
                                source: Theme.dark
                                        ? "qrc:/LibreConnect/mobile/previous_dark.svg"
                                        : "qrc:/LibreConnect/mobile/previous.svg"
                                width: 74
                                height: 74
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Previous"
                                color: Theme.textColor
                                font.pixelSize: 17
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        onClicked: remoteInputController.presenterNextSlide()
                        background: Rectangle {
                            radius: 18
                            color: Theme.buttonColor
                            border.width: 1
                            border.color: Theme.panelBorderColor
                        }
                        contentItem: ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            Image {
                                Layout.alignment: Qt.AlignHCenter
                                source: Theme.dark
                                        ? "qrc:/LibreConnect/mobile/next_dark.svg"
                                        : "qrc:/LibreConnect/mobile/next.svg"
                                width: 74
                                height: 74
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Next"
                                color: Theme.textColor
                                font.pixelSize: 17
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Button {
                        Layout.fillWidth: true
                        text: "Start Slideshow"
                        onClicked: remoteInputController.presenterStartSlideshow()
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "End Slideshow"
                        onClicked: remoteInputController.presenterEndSlideshow()
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: remoteInputController.statusMessage
            color: Theme.mutedTextColor
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
