import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var clipboardSyncController
    required property var remoteInputController
    required property var showRemoteInputCallback
    required property var showRemoteKeyboardCallback
    required property var showSettingsCallback

    readonly property int menuWidth: 190

    background: Rectangle {
        color: Theme.backgroundColor
    }

    Menu {
        id: connectedMenu
        parent: Overlay.overlay
        width: page.menuWidth
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        enter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 170
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "scale"
                    from: 0.95
                    to: 1.0
                    duration: 170
                    easing.type: Easing.OutCubic
                }
            }
        }

        exit: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: 120
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "scale"
                    from: 1.0
                    to: 0.97
                    duration: 120
                    easing.type: Easing.OutCubic
                }
            }
        }

        MenuItem {
            text: "Disconnect"
            contentItem: Text {
                text: parent.text
                color: Theme.destructiveColor
            }
            onTriggered: disconnectDialog.open()
        }

        MenuItem {
            text: "Unpair"
            contentItem: Text {
                text: parent.text
                color: Theme.destructiveColor
            }
            onTriggered: unpairDialog.open()
        }
    }

    Dialog {
        id: disconnectDialog
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentWidth: Math.min(page.width - 28, 340)
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
            spacing: 14
            width: disconnectDialog.contentWidth

            Text {
                width: parent.width
                text: "Disconnect device?"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "You will be returned to the connection screen."
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            RowLayout {
                width: parent.width
                spacing: 10

                Button {
                    Layout.fillWidth: true
                    text: "Cancel"
                    onClicked: disconnectDialog.close()
                }

                Button {
                    Layout.fillWidth: true
                    text: "Disconnect"
                    contentItem: Text {
                        text: parent.text
                        color: Theme.destructiveColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        disconnectDialog.close()
                        page.conn.disconnect()
                    }
                }
            }
        }
    }

    Dialog {
        id: unpairDialog
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentWidth: Math.min(page.width - 28, 340)
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
            spacing: 14
            width: unpairDialog.contentWidth

            Text {
                width: parent.width
                text: "Unpair this device?"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "This removes the current desktop from paired devices and disconnects."
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            RowLayout {
                width: parent.width
                spacing: 10

                Button {
                    Layout.fillWidth: true
                    text: "Cancel"
                    onClicked: unpairDialog.close()
                }

                Button {
                    Layout.fillWidth: true
                    text: "Unpair"
                    contentItem: Text {
                        text: parent.text
                        color: Theme.destructiveColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        unpairDialog.close()
                        page.conn.unpairCurrentDevice()
                    }
                }
            }
        }
    }

    function openConnectedMenu(anchorItem) {
        if (!Overlay.overlay) {
            connectedMenu.open()
            return
        }

        const point = anchorItem.mapToItem(
            Overlay.overlay,
            anchorItem.width - connectedMenu.width,
            anchorItem.height + 8
        )
        connectedMenu.x = Math.max(12, Math.min(point.x, Overlay.overlay.width - connectedMenu.width - 12))
        connectedMenu.y = Math.max(12, point.y)
        connectedMenu.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        RowLayout {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: "Connected"
                color: Theme.textColor
                font.pixelSize: 24
                font.bold: true
            }

            ToolButton {
                id: moreButton
                icon.source: Theme.dark
                             ? "qrc:/LibreConnect/mobile/more_dark.svg"
                             : "qrc:/LibreConnect/mobile/more.svg"
                icon.width: 24
                icon.height: 24
                display: AbstractButton.IconOnly
                onClicked: page.openConnectedMenu(moreButton)
            }
        }

        Button {
            Layout.fillWidth: true
            text: "Remote Control"
            onClicked: {
                page.showRemoteInputCallback()
            }
        }

        Button {
            Layout.fillWidth: true
            text: "Remote Keyboard"
            onClicked: page.showRemoteKeyboardCallback()
        }

        Button {
            Layout.fillWidth: true
            text: "Sync Clipboard"
            enabled: !page.clipboardSyncController.busy
            onClicked: page.clipboardSyncController.syncClipboard()
        }

        Button {
            Layout.fillWidth: true
            text: "Settings"
            onClicked: page.showSettingsCallback()
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
