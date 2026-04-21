import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    readonly property string windowTitleSuffix: "Settings"

    NotificationSyncController {
        id: notificationSyncController
    }

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

                    Text {
                        text: "Mobile follows the system color scheme automatically."
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        color: Theme.subtleTextColor
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
                            leftPadding: 10
                            rightPadding: themeCombo.indicator.width + themeCombo.spacing
                            text: themeCombo.displayText
                            font: themeCombo.font
                            color: Theme.textColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Text {
                            x: themeCombo.width - width - 10
                            y: (themeCombo.height - height) / 2
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
                            y: themeCombo.height + 4
                            width: themeCombo.width
                            implicitHeight: contentItem.implicitHeight
                            padding: 1

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
                        leftPadding: 10
                        rightPadding: themeComboCompact.indicator.width + themeComboCompact.spacing
                        text: themeComboCompact.displayText
                        font: themeComboCompact.font
                        color: Theme.textColor
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Text {
                        x: themeComboCompact.width - width - 10
                        y: (themeComboCompact.height - height) / 2
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
                        y: themeComboCompact.height + 4
                        width: themeComboCompact.width
                        implicitHeight: contentItem.implicitHeight
                        padding: 1

                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: themeComboCompact.popup.visible ? themeComboCompact.delegateModel : null
                            currentIndex: themeComboCompact.highlightedIndex
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
                        enabled: !notificationSyncController.busy
                        onClicked: notificationSyncController.setNotificationSyncEnabled(checked)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: notificationSyncController.statusMessage
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    color: Theme.mutedTextColor
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
