import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var notificationSyncController
    required property var themeModes
    required property var goBackCallback

    function currentThemeIndex() {
        for (let i = 0; i < themeModes.length; ++i) {
            if (themeModes[i].value === Theme.mode) {
                return i
            }
        }
        return 0
    }

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
                    text: "Settings"
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
                        text: (page.conn.notificationSendPermissionGranted && page.conn.notificationListenerPermissionGranted)
                              ? page.notificationSyncController.statusMessage
                              : "Notification sync is disabled until notification permissions are granted."
                        color: Theme.mutedTextColor
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Frame {
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Text {
                        text: "Theme"
                        color: Theme.textColor
                        font.pixelSize: 16
                        font.bold: true
                    }

                    ComboBox {
                        id: themeCombo
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        model: page.themeModes
                        textRole: "label"
                        currentIndex: page.currentThemeIndex()
                        font.pixelSize: 16
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        Material.accent: Theme.panelBorderColor
                        onActivated: Theme.setMode(page.themeModes[currentIndex].value)

                        contentItem: Text {
                            leftPadding: 14
                            rightPadding: themeCombo.indicator.width + 18
                            text: themeCombo.displayText
                            font: themeCombo.font
                            color: Theme.textColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Text {
                            x: themeCombo.width - width - 14
                            y: (themeCombo.height - height) / 2
                            text: "v"
                            color: Theme.textColor
                            font.pixelSize: 14
                        }

                        background: Rectangle {
                            radius: 10
                            color: themeCombo.pressed
                                   ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.16) : Qt.darker(Theme.buttonColor, 1.06))
                                   : (themeCombo.hovered
                                      ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.10) : Qt.darker(Theme.buttonColor, 1.03))
                                      : Theme.buttonColor)
                            border.width: 1
                            border.color: Theme.panelBorderColor
                        }

                        delegate: ItemDelegate {
                            id: themeOptionDelegate
                            width: themeCombo.width
                            height: 48
                            hoverEnabled: true
                            highlighted: themeCombo.highlightedIndex === index

                            contentItem: Text {
                                text: modelData.label
                                color: (themeOptionDelegate.highlighted || themeOptionDelegate.hovered)
                                       ? Theme.selectedTextColor
                                       : Theme.textColor
                                font.pixelSize: 16
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                color: themeOptionDelegate.highlighted
                                       ? Theme.selectedColor
                                       : (themeOptionDelegate.hovered ? Theme.buttonColor : Theme.panelColor)
                            }
                        }

                        popup: Popup {
                            y: themeCombo.height + 6
                            width: themeCombo.width
                            padding: 2
                            implicitHeight: Math.min(contentItem.implicitHeight + (padding * 2), page.height * 0.55)

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
                                        from: 0.96
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
                                        duration: 110
                                        easing.type: Easing.OutCubic
                                    }
                                    NumberAnimation {
                                        property: "scale"
                                        from: 1.0
                                        to: 0.98
                                        duration: 110
                                        easing.type: Easing.OutCubic
                                    }
                                }
                            }

                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: themeCombo.popup.visible ? themeCombo.delegateModel : null
                                currentIndex: themeCombo.highlightedIndex
                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                }
                            }

                            background: Rectangle {
                                radius: 10
                                color: Theme.panelColor
                                border.width: 1
                                border.color: Theme.panelBorderColor
                            }
                        }
                    }
                }
            }
        }
    }
}
