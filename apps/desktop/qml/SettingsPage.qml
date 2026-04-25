import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    readonly property string windowTitleSuffix: "Settings"
    required property var notificationSyncController
    required property var clipboardSyncController
    required property var permissionStateController
    required property var temporaryStorageController

    property var themeModes: [
        { label: "System", value: "system" },
        { label: "Light", value: "light" },
        { label: "Dark", value: "dark" }
    ]

    function currentThemeIndex() {
        for (let i = 0; i < themeModes.length; ++i) {
            if (themeModes[i].value === Theme.mode)
                return i
        }

        return 0
    }

    background: Rectangle {
        color: "transparent"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 16

        Text {
            Layout.fillWidth: true
            text: "Settings"
            font.family: Theme.fontFamily
            font.pixelSize: 30
            font.bold: true
            color: Theme.textColor
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            id: themeCard
            Layout.fillWidth: true
            implicitHeight: themeCardColumn.implicitHeight + 32
            readonly property bool compactThemeLayout: width < 520
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            ColumnLayout {
                id: themeCardColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Column {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "Theme"
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    Text {
                        text: "Choose light, dark, or system theme. Changes apply instantly."
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                        color: Theme.mutedTextColor
                        width: parent.width - 4
                    }

                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: !themeCard.compactThemeLayout

                    Item {
                        Layout.fillWidth: true
                    }

                    ComboBox {
                        id: themeCombo
                        Layout.preferredWidth: 180
                        Layout.minimumWidth: 180
                        Layout.maximumWidth: 180
                        Layout.preferredHeight: 42
                        Layout.minimumHeight: 42
                        Layout.maximumHeight: 42
                        model: root.themeModes
                        textRole: "label"
                        currentIndex: root.currentThemeIndex()
                        onActivated: Theme.setMode(root.themeModes[currentIndex].value)

                        font.family: Theme.fontFamily
                        font.pixelSize: 15

                        contentItem: Text {
                            anchors.fill: parent
                            leftPadding: 10
                            rightPadding: themeCombo.indicator.width + themeCombo.spacing
                            text: themeCombo.displayText
                            font: themeCombo.font
                            color: Theme.textColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Text {
                            x: Math.round(themeCombo.width - width - 10)
                            y: Math.round((themeCombo.height - height) / 2)
                            text: "v"
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: Theme.textColor
                        }

                        background: Rectangle {
                            radius: 8
                            color: themeCombo.hovered
                                   ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.12) : Qt.darker(Theme.buttonColor, 1.05))
                                   : Theme.buttonColor
                            border.color: Theme.panelBorderColor
                            border.width: 1
                        }

                        delegate: ItemDelegate {
                            id: themeOptionDelegate
                            width: themeCombo.width
                            height: 42
                            hoverEnabled: true
                            highlighted: themeCombo.highlightedIndex === index

                            contentItem: Text {
                                anchors.fill: parent
                                leftPadding: 10
                                rightPadding: 10
                                text: modelData.label
                                font.family: Theme.fontFamily
                                font.pixelSize: 15
                                color: (themeOptionDelegate.highlighted || themeOptionDelegate.hovered) ? Theme.selectedTextColor : Theme.textColor
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                color: themeOptionDelegate.highlighted
                                       ? Theme.selectedColor
                                       : (themeOptionDelegate.hovered ? Theme.buttonColor : Theme.panelColor)
                                border.color: Theme.panelBorderColor
                                border.width: 1
                            }
                        }

                        popup: Popup {
                            id: themePopup
                            modal: true
                            dim: false
                            x: 0
                            y: Math.round(themeCombo.height + 4)
                            width: Math.round(themeCombo.width)
                            implicitHeight: Math.min(contentItem.implicitHeight, 260)
                            padding: 1
                            onOpened: {
                                if (themeCombo.currentIndex >= 0)
                                    themePopupList.positionViewAtIndex(themeCombo.currentIndex, ListView.Contain)
                            }

                            contentItem: ListView {
                                id: themePopupList
                                clip: true
                                implicitHeight: contentHeight
                                boundsBehavior: Flickable.StopAtBounds
                                model: themeCombo.delegateModel
                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                }
                            }

                            background: Rectangle {
                                radius: 8
                                color: Theme.panelColor
                                border.color: Theme.panelBorderColor
                                border.width: 1
                            }
                        }
                    }
                }

                ComboBox {
                    id: themeComboCompact
                    visible: themeCard.compactThemeLayout
                    Layout.preferredWidth: 180
                    Layout.minimumWidth: 180
                    Layout.maximumWidth: 180
                    Layout.preferredHeight: 42
                    Layout.minimumHeight: 42
                    Layout.maximumHeight: 42
                    model: root.themeModes
                    textRole: "label"
                    currentIndex: root.currentThemeIndex()
                    onActivated: Theme.setMode(root.themeModes[currentIndex].value)
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    contentItem: Text {
                        anchors.fill: parent
                        leftPadding: 10
                        rightPadding: themeComboCompact.indicator.width + themeComboCompact.spacing
                        text: themeComboCompact.displayText
                        font: themeComboCompact.font
                        color: Theme.textColor
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Text {
                        x: Math.round(themeComboCompact.width - width - 10)
                        y: Math.round((themeComboCompact.height - height) / 2)
                        text: "v"
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        color: Theme.textColor
                    }
                    background: Rectangle {
                        radius: 8
                        color: themeComboCompact.hovered
                               ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.12) : Qt.darker(Theme.buttonColor, 1.05))
                               : Theme.buttonColor
                        border.color: Theme.panelBorderColor
                        border.width: 1
                    }

                    delegate: ItemDelegate {
                        id: themeCompactOptionDelegate
                        width: themeComboCompact.width
                        height: 42
                        hoverEnabled: true
                        highlighted: themeComboCompact.highlightedIndex === index

                        contentItem: Text {
                            anchors.fill: parent
                            leftPadding: 10
                            rightPadding: 10
                            text: modelData.label
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            color: (themeCompactOptionDelegate.highlighted || themeCompactOptionDelegate.hovered) ? Theme.selectedTextColor : Theme.textColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            color: themeCompactOptionDelegate.highlighted
                                   ? Theme.selectedColor
                                   : (themeCompactOptionDelegate.hovered ? Theme.buttonColor : Theme.panelColor)
                            border.color: Theme.panelBorderColor
                            border.width: 1
                        }
                    }

                    popup: Popup {
                        id: themePopupCompact
                        modal: true
                        dim: false
                        x: 0
                        y: Math.round(themeComboCompact.height + 4)
                        width: Math.round(themeComboCompact.width)
                        implicitHeight: Math.min(contentItem.implicitHeight, 260)
                        padding: 1
                        onOpened: {
                            if (themeComboCompact.currentIndex >= 0)
                                themePopupListCompact.positionViewAtIndex(themeComboCompact.currentIndex, ListView.Contain)
                        }

                        contentItem: ListView {
                            id: themePopupListCompact
                            clip: true
                            implicitHeight: contentHeight
                            boundsBehavior: Flickable.StopAtBounds
                            model: themeComboCompact.delegateModel
                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }
                        }

                        background: Rectangle {
                            radius: 8
                            color: Theme.panelColor
                            border.color: Theme.panelBorderColor
                            border.width: 1
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: notificationCardColumn.implicitHeight + 32
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            ColumnLayout {
                id: notificationCardColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Text {
                        text: "Enable Notification Sync"
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                        Layout.fillWidth: true
                    }

                    Switch {
                        id: syncToggle
                        checked: notificationSyncController.enabled
                        enabled: notificationSyncController.permissionsGranted && !notificationSyncController.busy
                        onClicked: notificationSyncController.setNotificationSyncEnabled(checked)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: notificationSyncController.permissionsGranted
                          ? notificationSyncController.statusMessage
                          : notificationSyncController.permissionMessage
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    color: Theme.mutedTextColor
                }
            }

            MouseArea {
                anchors.fill: parent
                visible: !notificationSyncController.permissionsGranted
                onClicked: notificationPermissionDialog.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: clipboardCardColumn.implicitHeight + 32
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            ColumnLayout {
                id: clipboardCardColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Text {
                        text: "Auto Clipboard Sync"
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                        Layout.fillWidth: true
                    }

                    Switch {
                        id: clipboardAutoSyncToggle
                        checked: clipboardSyncController.autoSyncEnabled
                        enabled: !clipboardSyncController.busy
                        onClicked: clipboardSyncController.setClipboardAutoSyncEnabled(checked)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: clipboardSyncController.statusMessage
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    color: Theme.mutedTextColor
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: temporaryStorageCardColumn.implicitHeight + 32
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            ColumnLayout {
                id: temporaryStorageCardColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Text {
                    text: "Temporary Storage"
                    font.family: Theme.fontFamily
                    font.pixelSize: 20
                    font.bold: true
                    color: Theme.textColor
                }

                Text {
                    Layout.fillWidth: true
                    text: temporaryStorageController.temporaryStoragePath.length > 0
                          ? ("Location: " + temporaryStorageController.temporaryStoragePath)
                          : "Location unavailable."
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    color: Theme.mutedTextColor
                }

                Text {
                    Layout.fillWidth: true
                    text: temporaryStorageController.statusMessage
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    color: Theme.mutedTextColor
                }

                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.fillWidth: true
                    }

                    ThemedButton {
                        text: "Clear Temporary Storage"
                        width: 220
                        enabled: temporaryStorageController.temporaryStoragePath.length > 0
                        onClicked: temporaryStorageController.clearTemporaryStorage()
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }

    Dialog {
        id: notificationPermissionDialog
        readonly property bool mobilePermissionMissing: !notificationSyncController.remotePermissionGranted
        title: ""
        modal: true
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        contentWidth: 360
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        contentItem: Column {
            spacing: 14
            width: notificationPermissionDialog.contentWidth

            Text {
                text: notificationPermissionDialog.mobilePermissionMissing
                      ? "Phone Notification Permission Required"
                      : "macOS Notification Permission Required"
                width: parent.width
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: notificationPermissionDialog.mobilePermissionMissing
                      ? "Grant notification permissions on the mobile app to use notification sync."
                      : "Allow LibreConnect in System Settings > Notifications to show synced notifications on this Mac."
                width: parent.width
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10

                ThemedButton {
                    text: "Cancel"
                    width: 120
                    height: 40
                    font.pixelSize: 14
                    onClicked: notificationPermissionDialog.close()
                }

                ThemedButton {
                    text: notificationPermissionDialog.mobilePermissionMissing ? "Prompt on Phone" : "Allow on Mac"
                    width: 170
                    height: 40
                    font.pixelSize: 14
                    onClicked: {
                        if (notificationPermissionDialog.mobilePermissionMissing)
                            permissionStateController.requestPermission(2)
                        else
                            permissionStateController.requestDesktopNotificationPermission()
                        notificationPermissionDialog.close()
                    }
                }
            }
        }
    }
}
