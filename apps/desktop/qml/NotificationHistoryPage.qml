import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var notificationSyncController
    readonly property string windowTitleSuffix: "Notifications"

    background: Rectangle {
        color: "transparent"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: "Notifications"
            font.family: Theme.fontFamily
            font.pixelSize: 30
            font.bold: true
            color: Theme.textColor
        }

        Text {
            Layout.fillWidth: true
            text: notificationSyncController.enabled
                  ? "Live list of active notifications on your phone."
                  : "Enable Notification Sync in Settings to view active notifications."
            font.family: Theme.fontFamily
            font.pixelSize: 15
            wrapMode: Text.WordWrap
            color: Theme.mutedTextColor
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: Theme.backgroundColor
            border.color: Theme.panelBorderColor
            border.width: 1

            Item {
                anchors.fill: parent
                anchors.margins: 12

                ListView {
                    id: historyList
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: scrollGutter.visible ? 28 : 0
                    visible: notificationSyncController.enabled && count > 0
                    clip: true
                    spacing: 8
                    model: notificationSyncController.notifications
                    ScrollBar.vertical: ScrollBar {
                        id: notificationsScrollBar
                        parent: scrollGutter
                        anchors.fill: parent
                        policy: ScrollBar.AlwaysOn
                        active: true
                        opacity: 1.0
                        visible: scrollGutter.visible
                    }

                    delegate: Rectangle {
                        required property var modelData
                        readonly property string appNameText: (typeof modelData.appName === "string" && modelData.appName.length > 0)
                                                                ? modelData.appName
                                                                : "Unknown app"
                        readonly property string titleText: (typeof modelData.title === "string" && modelData.title.length > 0)
                                                              ? modelData.title
                                                              : "(No title)"
                        readonly property string bodyText: (typeof modelData.content === "string" && modelData.content.length > 0)
                                                             ? modelData.content
                                                             : "(No description)"
                        readonly property string iconSource: (typeof modelData.icon === "string") ? modelData.icon : ""
                        readonly property bool isDismissable: modelData.dismissable !== false

                        width: historyList.width
                        implicitHeight: Math.max(62, Math.max(iconBox.height, Math.max(infoColumn.implicitHeight, actionSlot.height)) + 20)
                        height: implicitHeight
                        radius: 10
                        color: Theme.panelColor
                        border.color: Theme.panelBorderColor
                        border.width: 1

                        Item {
                            anchors.fill: parent
                            anchors.margins: 10

                            Rectangle {
                                id: iconBox
                                width: 42
                                height: 42
                                radius: 10
                                color: Theme.buttonColor
                                border.color: Theme.panelBorderColor
                                border.width: 1
                                anchors.left: parent.left
                                anchors.top: parent.top

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 5
                                    source: iconSource
                                    visible: iconSource.length > 0
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                }

                                Text {
                                    anchors.centerIn: parent
                                    visible: iconSource.length === 0
                                    text: appNameText.length > 0 ? appNameText[0].toUpperCase() : "N"
                                    color: Theme.textColor
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 15
                                    font.bold: true
                                }
                            }

                            Item {
                                id: actionSlot
                                width: 92
                                height: 36
                                anchors.right: parent.right
                                anchors.top: parent.top

                                ThemedButton {
                                    anchors.fill: parent
                                    text: "Dismiss"
                                    visible: isDismissable
                                    enabled: notificationSyncController.enabled
                                    onClicked: {
                                        if (!notificationSyncController.dismissNotification(modelData.key))
                                            undismissableDialog.open()
                                    }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    visible: !isDismissable
                                    radius: 8
                                    color: Theme.backgroundColor
                                    border.color: Theme.panelBorderColor
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Locked"
                                        color: Theme.subtleTextColor
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                }
                            }

                            Column {
                                id: infoColumn
                                anchors.left: iconBox.right
                                anchors.leftMargin: 10
                                anchors.right: parent.right
                                anchors.rightMargin: actionSlot.width + 10
                                anchors.top: parent.top
                                spacing: 4

                                Row {
                                    width: parent.width
                                    spacing: 8

                                    Text {
                                        width: parent.width
                                        text: appNameText
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        font.bold: true
                                        color: Theme.textColor
                                        elide: Text.ElideRight
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: titleText
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 14
                                    color: Theme.textColor
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: bodyText
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    color: Theme.mutedTextColor
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: (typeof modelData.timestampText === "string" && modelData.timestampText.length > 0)
                                          ? modelData.timestampText
                                          : "Unknown time"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: Theme.mutedTextColor
                                    elide: Text.ElideRight
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
                    width: 14
                    radius: 7
                    color: Theme.backgroundColor
                    border.color: Theme.panelBorderColor
                    border.width: 1
                    visible: historyList.visible && historyList.contentHeight > historyList.height + 1
                }

                Column {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 30, 460)
                    spacing: 8
                    visible: !notificationSyncController.enabled

                    Text {
                        width: parent.width
                        text: "Notifications Sync Is Off"
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: 24
                        font.bold: true
                        color: Theme.textColor
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        text: "Turn on notification sync from Settings to view and dismiss phone notifications here."
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        color: Theme.mutedTextColor
                        wrapMode: Text.WordWrap
                    }
                }

                Column {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 30, 420)
                    spacing: 8
                    visible: notificationSyncController.enabled && historyList.count === 0

                    Text {
                        width: parent.width
                        text: "No Active Notifications"
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: 24
                        font.bold: true
                        color: Theme.textColor
                    }

                    Text {
                        width: parent.width
                        text: "New phone notifications will appear here when they are active."
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        color: Theme.mutedTextColor
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    Dialog {
        id: undismissableDialog
        title: ""
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        contentWidth: 360
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        contentItem: Column {
            spacing: 12
            width: undismissableDialog.contentWidth

            Text {
                width: parent.width
                text: "Cannot Dismiss"
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 24
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "This notification is marked as non-dismissable by Android or could not be dismissed right now."
                color: Theme.mutedTextColor
                font.family: Theme.fontFamily
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                ThemedButton {
                    text: "OK"
                    width: 120
                    height: 40
                    onClicked: undismissableDialog.close()
                }
            }
        }
    }
}
