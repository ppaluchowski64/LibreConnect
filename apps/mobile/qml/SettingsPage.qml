import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var notificationSyncController
    required property var clipboardSyncController
    required property var themeModes
    required property var showHomeCallback
    property bool showBottomNavigation: true
    property string downloadPathDraft: conn.defaultDownloadPath

    function currentFindMyPhoneRingtoneIndex() {
        const options = page.conn.findMyPhoneRingtoneOptions
        const targetValue = page.conn.findMyPhoneRingtoneUri
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === targetValue)
                return i
        }
        return 0
    }

    function themedIcon(baseName) {
        return Theme.dark
                ? ("qrc:/LibreConnect/mobile/" + baseName + "_dark.svg")
                : ("qrc:/LibreConnect/mobile/" + baseName + ".svg")
    }

    background: Rectangle {
        color: Theme.backgroundColor
    }

    FileDialog {
        id: customRingtoneDialog
        title: "Choose ringtone"
        fileMode: FileDialog.OpenFile
        nameFilters: [ "Audio files (*.mp3 *.m4a *.aac *.ogg *.wav *.flac)", "All files (*)" ]
        onAccepted: {
            if (selectedFile)
                page.conn.setFindMyPhoneRingtoneFile(selectedFile)
        }
    }

    component BottomNavButton: Button {
        id: navButton
        required property string labelText
        required property string iconBase
        property bool selected: false

        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: 60
        checkable: false
        hoverEnabled: true
        padding: 8

        background: Rectangle {
            radius: 14
            color: navButton.selected
                   ? Theme.selectedColor
                   : (navButton.pressed
                      ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.14) : Qt.darker(Theme.buttonColor, 1.05))
                      : Theme.buttonColor)
            border.width: navButton.selected ? 2 : 1
            border.color: navButton.selected ? Theme.accentColor : Theme.panelBorderColor
        }

        contentItem: Item {
            anchors.fill: parent

            Column {
                anchors.centerIn: parent
                width: parent.width - (navButton.padding * 2)
                spacing: 2

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: page.themedIcon(navButton.iconBase)
                    sourceSize.width: 24
                    sourceSize.height: 24
                    width: 24
                    height: 24
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                Text {
                    width: parent.width
                    text: navButton.labelText
                    color: navButton.selected ? Theme.selectedTextColor : Theme.textColor
                    font.pixelSize: 13
                    font.bold: navButton.selected
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: settingsColumn
                x: 16
                y: 16
                width: Math.max(0, settingsScroll.availableWidth - 32)
                spacing: 12

                Frame {
                    Layout.fillWidth: true
                    background: Rectangle {
                        radius: 14
                        color: Theme.panelColor
                        border.width: 1
                        border.color: Theme.panelBorderColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Text {
                            text: "Theme"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        ButtonGroup {
                            id: themeGroup
                        }

                        Repeater {
                            model: page.themeModes
                            delegate: RadioButton {
                                text: modelData.label
                                checked: Theme.mode === modelData.value
                                ButtonGroup.group: themeGroup
                                onClicked: Theme.setMode(modelData.value)
                            }
                        }
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    background: Rectangle {
                        radius: 14
                        color: Theme.panelColor
                        border.width: 1
                        border.color: Theme.panelBorderColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Text {
                            text: "Notification Sync"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                Layout.fillWidth: true
                                text: "Enable for connected desktop device"
                                color: Theme.mutedTextColor
                                font.pixelSize: 14
                                wrapMode: Text.WordWrap
                            }

                            Switch {
                                id: notificationSwitch
                                checked: page.notificationSyncController.enabled
                                enabled: page.conn.notificationSendPermissionGranted
                                         && page.conn.notificationListenerPermissionGranted
                                         && !page.notificationSyncController.busy
                                onClicked: page.notificationSyncController.setNotificationSyncEnabled(checked)
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            visible: !(page.conn.notificationSendPermissionGranted && page.conn.notificationListenerPermissionGranted)
                            text: "Grant Notification Permissions"
                            onClicked: page.conn.requestNotificationPermissions()
                        }

                        Text {
                            Layout.fillWidth: true
                            text: !(page.conn.notificationSendPermissionGranted && page.conn.notificationListenerPermissionGranted)
                                  ? "Notification sync is disabled until notification permissions are granted."
                                  : (page.notificationSyncController.desktopPermissionGranted
                                      ? page.notificationSyncController.statusMessage
                                      : "Notification sync is waiting for the connected desktop device to allow LibreConnect notifications.")
                            color: Theme.mutedTextColor
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    background: Rectangle {
                        radius: 14
                        color: Theme.panelColor
                        border.width: 1
                        border.color: Theme.panelBorderColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Text {
                            text: "Auto Clipboard Sync"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                Layout.fillWidth: true
                                text: "Enable for connected desktop device"
                                color: Theme.mutedTextColor
                                font.pixelSize: 14
                                wrapMode: Text.WordWrap
                            }

                            Switch {
                                id: clipboardSwitch
                                checked: page.clipboardSyncController.autoSyncEnabled
                                enabled: !page.clipboardSyncController.busy
                                onClicked: page.clipboardSyncController.setClipboardAutoSyncEnabled(checked)
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: page.clipboardSyncController.statusMessage
                            color: Theme.mutedTextColor
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    background: Rectangle {
                        radius: 14
                        color: Theme.panelColor
                        border.width: 1
                        border.color: Theme.panelBorderColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Text {
                            text: "Find My Phone"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Choose the ringtone used when this device is pinged."
                            color: Theme.mutedTextColor
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }

                        ComboBox {
                            id: ringtoneCombo
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            model: page.conn.findMyPhoneRingtoneOptions
                            textRole: "label"
                            currentIndex: page.currentFindMyPhoneRingtoneIndex()
                            focusPolicy: Qt.NoFocus
                            onPressedChanged: {
                                if (pressed)
                                    page.conn.refreshFindMyPhoneRingtones()
                            }
                            onActivated: {
                                const selected = page.conn.findMyPhoneRingtoneOptions[currentIndex]
                                if (!selected)
                                    return
                                page.conn.setFindMyPhoneRingtoneUri(selected.value)
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "Choose Custom Ringtone"
                            onClicked: customRingtoneDialog.open()
                        }

                        Text {
                            Layout.fillWidth: true
                            text: page.conn.findMyPhoneRingtoneLabel
                            color: Theme.mutedTextColor
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    background: Rectangle {
                        radius: 14
                        color: Theme.panelColor
                        border.width: 1
                        border.color: Theme.panelBorderColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Text {
                            text: "Downloads"
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Folder on the desktop used for files shared from Android."
                            color: Theme.mutedTextColor
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }

                        TextField {
                            id: downloadPathField
                            Layout.fillWidth: true
                            text: page.downloadPathDraft
                            placeholderText: "Desktop folder path"
                            selectByMouse: true
                            onTextEdited: page.downloadPathDraft = text
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "Save Default Download Path"
                            enabled: page.downloadPathDraft.trim().length > 0
                            onClicked: page.conn.setDefaultDownloadPath(page.downloadPathDraft)
                        }

                        Text {
                            Layout.fillWidth: true
                            text: page.conn.defaultDownloadPathStatus
                            color: Theme.mutedTextColor
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                }
            }
        }

        Frame {
            visible: page.showBottomNavigation
            Layout.fillWidth: true
            padding: 10
            Material.elevation: 4

            background: Rectangle {
                radius: 16
                color: Theme.panelColor
                border.width: 1
                border.color: Theme.panelBorderColor
            }

            RowLayout {
                anchors.fill: parent
                spacing: 10

                BottomNavButton {
                    labelText: "Home"
                    iconBase: "home"
                    selected: false
                    onClicked: page.showHomeCallback()
                }

                BottomNavButton {
                    labelText: "Settings"
                    iconBase: "settings"
                    selected: true
                }
            }
        }
    }

    Connections {
        target: page.conn

        function onDefaultDownloadPathChanged() {
            page.downloadPathDraft = page.conn.defaultDownloadPath
        }
    }

    Component.onCompleted: page.conn.refreshDefaultDownloadPath()
}
