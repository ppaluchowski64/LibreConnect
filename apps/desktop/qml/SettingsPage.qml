import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
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
        color: Theme.backgroundColor
    }

    Column {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 22

        Row {
            spacing: 16

            ThemedButton {
                text: "Back"
                width: 100
                height: 42
                onClicked: windowRef.goBack()
            }

            Text {
                text: "Settings"
                font.family: Theme.fontFamily
                font.pixelSize: 30
                font.bold: true
                color: Theme.textColor
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            width: parent.width
            height: 148
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            Row {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 16

                Column {
                    width: parent.width - 220
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
                        width: parent.width
                    }

                    Text {
                        text: "Mobile follows the system color scheme automatically."
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        color: Theme.subtleTextColor
                        width: parent.width
                    }
                }

                ComboBox {
                    id: themeCombo
                    anchors.verticalCenter: parent.verticalCenter
                    width: 180
                    height: 42
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
                        color: Theme.buttonColor
                        border.color: Theme.panelBorderColor
                        border.width: 1
                    }

                    delegate: ItemDelegate {
                        width: themeCombo.width
                        height: 42
                        highlighted: themeCombo.highlightedIndex === index

                        contentItem: Text {
                            text: modelData.label
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            color: Theme.textColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            color: parent.highlighted ? Theme.selectedColor : Theme.panelColor
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
            width: parent.width
            height: 132
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            Row {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 16

                Column {
                    width: parent.width - syncToggle.width - 20
                    spacing: 8

                    Text {
                        text: "Enable Notification Sync"
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textColor
                    }

                    Text {
                        text: notificationSyncController.statusMessage
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                        color: Theme.mutedTextColor
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
