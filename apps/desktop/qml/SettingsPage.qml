import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    readonly property string windowTitleSuffix: qsTr("Settings")
    required property var connectionController
    required property string activeDeviceId
    required property var notificationSyncController
    required property var clipboardSyncController
    required property var permissionStateController
    required property var temporaryStorageController

    property var themeModes: [
        { label: qsTr("System"), value: "system" },
        { label: qsTr("Light"), value: "light" },
        { label: qsTr("Dark"), value: "dark" }
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

    Item {
        anchors.fill: parent

        Flickable {
            id: settingsFlickable
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: settingsColumn.implicitHeight + 36

            ScrollBar.vertical: ScrollBar {
                id: settingsScrollBar
                parent: scrollGutter
                anchors.fill: parent
                policy: ScrollBar.AlwaysOn
                active: true
                opacity: 1.0
                visible: scrollGutter.visible
            }

            ColumnLayout {
                id: settingsColumn
                x: 18
                y: 18
                width: settingsFlickable.width - (scrollGutter.visible ? 64 : 36)
                spacing: 16

            Text {
                Layout.fillWidth: true
                text: qsTr("Settings")
                font.family: Theme.fontFamily
                font.pixelSize: 30
                font.bold: true
                color: Theme.textColor
                verticalAlignment: Text.AlignVCenter
            }

        Rectangle {
            id: languageCard
            Layout.fillWidth: true
            implicitHeight: languageCardColumn.implicitHeight + 32
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            ColumnLayout {
                id: languageCardColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Column {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("Language")
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    Text {
                        text: qsTr("Choose the app language. Restart LibreConnect to apply changes.")
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                        color: Theme.mutedTextColor
                        width: parent.width - 4
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.fillWidth: true
                    }

                    ComboBox {
                        id: languageCombo
                        Layout.preferredWidth: 240
                        Layout.minimumWidth: 240
                        Layout.maximumWidth: 240
                        Layout.preferredHeight: 42
                        Layout.minimumHeight: 42
                        Layout.maximumHeight: 42
                        model: Language.availableLanguages
                        textRole: "name"
                        valueRole: "code"
                        currentIndex: {
                            const languages = Language.availableLanguages
                            for (let i = 0; i < languages.length; ++i) {
                                if (languages[i].code === Language.currentLanguage)
                                    return i
                            }
                            return 0
                        }
                        onActivated: Language.setLanguage(currentValue)

                        font.family: Theme.fontFamily
                        font.pixelSize: 15

                        contentItem: Text {
                            anchors.fill: parent
                            leftPadding: 10
                            rightPadding: languageCombo.indicator.width + languageCombo.spacing
                            text: languageCombo.displayText
                            font: languageCombo.font
                            color: Theme.textColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Text {
                            x: Math.round(languageCombo.width - width - 10)
                            y: Math.round((languageCombo.height - height) / 2)
                            text: "v"
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: Theme.textColor
                        }

                        background: Rectangle {
                            radius: 8
                            color: languageCombo.hovered
                                   ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.12) : Qt.darker(Theme.buttonColor, 1.05))
                                   : Theme.buttonColor
                            border.color: Theme.panelBorderColor
                            border.width: 1
                        }

                        delegate: ItemDelegate {
                            id: languageOptionDelegate
                            width: languageCombo.width
                            height: 42
                            hoverEnabled: true
                            highlighted: languageCombo.highlightedIndex === index

                            contentItem: Text {
                                anchors.fill: parent
                                leftPadding: 10
                                rightPadding: 10
                                text: modelData.name
                                font.family: Theme.fontFamily
                                font.pixelSize: 15
                                color: (languageOptionDelegate.highlighted || languageOptionDelegate.hovered) ? Theme.selectedTextColor : Theme.textColor
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                color: languageOptionDelegate.highlighted
                                       ? Theme.selectedColor
                                       : (languageOptionDelegate.hovered ? Theme.buttonColor : Theme.panelColor)
                                border.color: Theme.panelBorderColor
                                border.width: 1
                            }
                        }

                        popup: Popup {
                            id: languagePopup
                            modal: true
                            closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
                            dim: false
                            x: 0
                            y: Math.round(languageCombo.height + 4)
                            width: Math.round(languageCombo.width)
                            implicitHeight: Math.min(contentItem.implicitHeight, 260)
                            padding: 1
                            onOpened: {
                                if (languageCombo.currentIndex >= 0)
                                    languagePopupList.positionViewAtIndex(languageCombo.currentIndex, ListView.Contain)
                            }

                            contentItem: ListView {
                                id: languagePopupList
                                clip: true
                                implicitHeight: contentHeight
                                boundsBehavior: Flickable.StopAtBounds
                                model: languageCombo.delegateModel
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

                Text {
                    Layout.fillWidth: true
                    visible: Language.restartRequired
                    text: qsTr("Restart LibreConnect to apply the language change.")
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                    color: Theme.mutedTextColor
                }
            }
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
                        text: qsTr("Theme")
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    Text {
                        text: qsTr("Choose light, dark, or system theme. Changes apply instantly.")
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
                            closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
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
                        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
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
            implicitHeight: autoConnectColumn.implicitHeight + 32
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            ColumnLayout {
                id: autoConnectColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Text {
                        text: qsTr("Auto-connect")
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                        Layout.fillWidth: true
                    }
                    Switch {
                        id: autoConnectToggle
                        enabled: root.activeDeviceId !== ""
                        checked: root.connectionController.autoConnectEnabled(root.activeDeviceId)
                        onClicked: {
                            if (!root.connectionController.setAutoConnectEnabled(root.activeDeviceId, checked)) {
                                checked = root.connectionController.autoConnectEnabled(root.activeDeviceId)
                                autoConnectErrorDialog.open()
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Automatically connect this paired device when it is available. LibreConnect runs in the background after you sign in.")
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    color: Theme.mutedTextColor
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    height: 1
                    color: Theme.panelBorderColor
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Ask when connecting devices")
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        color: Theme.textColor
                    }
                    Switch {
                        checked: root.connectionController.autoConnectPromptEnabled
                        onClicked: root.connectionController.autoConnectPromptEnabled = checked
                    }
                }
            }
        }

        Rectangle {
            id: streamerModeCard
            Layout.fillWidth: true
            implicitHeight: streamerModeColumn.implicitHeight + 32
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            ColumnLayout {
                id: streamerModeColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Text {
                        text: qsTr("Streamer Mode")
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                        Layout.fillWidth: true
                    }

                    Switch {
                        id: streamerModeToggle
                        checked: Theme.streamerMode
                        onClicked: Theme.setStreamerMode(checked)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Hide sensitive info from the UI to protect your privacy while streaming or sharing your screen.")
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    color: Theme.mutedTextColor
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
                        text: qsTr("Notification Sync")
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
                        text: qsTr("Auto Clipboard Sync")
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
                    text: qsTr("Temporary Storage")
                    font.family: Theme.fontFamily
                    font.pixelSize: 20
                    font.bold: true
                    color: Theme.textColor
                }

                Text {
                    Layout.fillWidth: true
                    text: temporaryStorageController.temporaryStoragePath.length > 0
                          ? qsTr("Location: %1").arg(temporaryStorageController.temporaryStoragePath)
                          : qsTr("Location unavailable.")
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
                        text: qsTr("Clear Temporary Storage")
                        width: 220
                        enabled: temporaryStorageController.temporaryStoragePath.length > 0
                        onClicked: temporaryStorageController.clearTemporaryStorage()
                    }
                }
            }
        }
        }

        }

        Rectangle {
            id: scrollGutter
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.topMargin: 18
            anchors.bottomMargin: 18
            anchors.rightMargin: 8
            width: 14
            radius: 7
            color: Theme.backgroundColor
            border.color: Theme.panelBorderColor
            border.width: 1
            visible: settingsFlickable.contentHeight > settingsFlickable.height + 1
        }
    }

    Dialog {
        id: autoConnectErrorDialog
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        padding: 22
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
        }
        contentItem: Column {
            width: 340
            spacing: 16
            Text {
                text: qsTr("Auto-connect unavailable")
                font.family: Theme.fontFamily
                font.pixelSize: 22
                font.bold: true
                color: Theme.textColor
            }
            Text {
                width: parent.width
                text: root.connectionController.lastError
                font.family: Theme.fontFamily
                font.pixelSize: 14
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }
            ThemedButton {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("OK")
                width: 160
                height: 42
                onClicked: autoConnectErrorDialog.close()
            }
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
                      ? qsTr("Phone Notification Permission Required")
                      : qsTr("macOS Notification Permission Required")
                width: parent.width
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: notificationPermissionDialog.mobilePermissionMissing
                      ? qsTr("Grant notification permissions on the mobile app to use notification sync.")
                      : qsTr("Allow LibreConnect in System Settings > Notifications to show synced notifications on this Mac.")
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
                    text: qsTr("Cancel")
                    width: 120
                    height: 40
                    font.pixelSize: 14
                    onClicked: notificationPermissionDialog.close()
                }

                ThemedButton {
                    text: notificationPermissionDialog.mobilePermissionMissing ? qsTr("Prompt on Phone") : qsTr("Allow on Mac")
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
