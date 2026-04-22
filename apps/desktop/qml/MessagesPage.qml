import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var smsBridgeController
    readonly property string windowTitleSuffix: "Messages"

    function sendCurrentMessage() {
        const message = composerField.text.trim()
        if (message.length === 0) {
            return
        }

        smsBridgeController.sendMessage(message)
        composerField.text = ""
    }

    background: Rectangle {
        color: Theme.panelColor
    }

    Connections {
        target: smsBridgeController

        function onMessagesChanged() {
            if (messagesList.count > 0) {
                messagesList.positionViewAtEnd()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 320
            Layout.minimumWidth: 260
            Layout.fillHeight: true
            radius: 12
            color: Theme.backgroundColor
            border.width: 1
            border.color: Theme.panelBorderColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        Layout.fillWidth: true
                        text: "Conversations"
                        color: Theme.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.bold: true
                    }

                    ThemedButton {
                        text: "Refresh"
                        width: 92
                        height: 36
                        font.pixelSize: 13
                        onClicked: smsBridgeController.refreshConversations()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: smsBridgeController.connected
                          ? (smsBridgeController.ready ? "Phone connected" : "Connecting SMS bridge...")
                          : "Phone disconnected"
                    color: smsBridgeController.connected ? Theme.successColor : Theme.mutedTextColor
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }

                ListView {
                    id: contactsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: smsBridgeController.contacts

                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 78
                        radius: 10
                        color: modelData.selected ? Theme.selectedColor : Theme.panelColor
                        border.width: modelData.selected ? 1 : 0
                        border.color: Theme.selectedBorderColor

                        MouseArea {
                            anchors.fill: parent
                            onClicked: smsBridgeController.selectConversation(modelData.number, modelData.name)
                        }

                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 5

                            Row {
                                width: parent.width
                                spacing: 6

                                Text {
                                    width: parent.width - unreadBadge.width - 8
                                    text: modelData.name.length > 0 ? modelData.name : modelData.number
                                    color: modelData.selected ? Theme.selectedTextColor : Theme.textColor
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    id: unreadBadge
                                    visible: modelData.unread > 0
                                    width: visible ? Math.max(20, badgeText.implicitWidth + 10) : 0
                                    height: visible ? 20 : 0
                                    radius: 10
                                    color: Theme.successColor

                                    Text {
                                        id: badgeText
                                        anchors.centerIn: parent
                                        text: modelData.unread
                                        color: Theme.backgroundColor
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        font.bold: true
                                    }
                                }
                            }

                            Text {
                                width: parent.width
                                text: modelData.preview
                                color: modelData.selected ? Theme.selectedTextColor : Theme.mutedTextColor
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: Theme.backgroundColor
            border.width: 1
            border.color: Theme.panelBorderColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: smsBridgeController.selectedContactName.length > 0
                          ? smsBridgeController.selectedContactName
                          : "Select a conversation"
                    color: Theme.textColor
                    font.family: Theme.fontFamily
                    font.pixelSize: 22
                    font.bold: true
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: smsBridgeController.selectedContactNumber
                    visible: text.length > 0
                    color: Theme.mutedTextColor
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: Theme.panelColor
                    border.width: 1
                    border.color: Theme.panelBorderColor
                    clip: true

                    ListView {
                        id: messagesList
                        anchors.fill: parent
                        anchors.margins: 10
                        model: smsBridgeController.messages
                        spacing: 6
                        clip: true

                        delegate: Item {
                            required property var modelData
                            width: ListView.view.width
                            height: bubble.implicitHeight + 2

                            Row {
                                width: parent.width
                                spacing: 0
                                layoutDirection: modelData.incoming ? Qt.LeftToRight : Qt.RightToLeft

                                Rectangle {
                                    id: bubble
                                    readonly property real maxBubbleWidth: parent.width * 0.72
                                    readonly property real minBubbleWidth: 56
                                    width: Math.min(
                                        maxBubbleWidth,
                                        Math.max(
                                            minBubbleWidth,
                                            Math.max(bubbleText.implicitWidth, metaRow.implicitWidth) + 16
                                        )
                                    )
                                    radius: 12
                                    color: modelData.incoming ? Theme.buttonColor : Theme.selectedColor
                                    border.width: modelData.failed ? 1 : 0
                                    border.color: Theme.dangerColor
                                    implicitHeight: bubbleContent.implicitHeight + 16

                                    Column {
                                        id: bubbleContent
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 4

                                        Text {
                                            id: bubbleText
                                            width: parent.width
                                            text: modelData.body
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            color: modelData.incoming ? Theme.textColor : Theme.selectedTextColor
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 14
                                        }

                                        Row {
                                            id: metaRow
                                            width: parent.width
                                            spacing: 6
                                            visible: timeText.text.length > 0 || statusText.text.length > 0

                                            Text {
                                                id: timeText
                                                text: modelData.timestamp > 0
                                                      ? Qt.formatDateTime(new Date(modelData.timestamp), "HH:mm")
                                                      : ""
                                                color: Theme.mutedTextColor
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 11
                                            }

                                            Text {
                                                id: statusText
                                                text: modelData.failed ? "Failed to send" : (modelData.pending ? "Sending..." : "")
                                                color: modelData.failed ? Theme.dangerColor : Theme.mutedTextColor
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        onCountChanged: {
                            if (count > 0) {
                                positionViewAtEnd()
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    TextField {
                        id: composerField
                        Layout.fillWidth: true
                        placeholderText: smsBridgeController.canSend ? "Type a message" : "Select a conversation to send"
                        enabled: smsBridgeController.canSend
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        color: Theme.textColor
                        selectByMouse: true
                        onAccepted: root.sendCurrentMessage()

                        background: Rectangle {
                            radius: 10
                            color: Theme.panelColor
                            border.width: 1
                            border.color: Theme.panelBorderColor
                        }
                    }

                    ThemedButton {
                        text: "Send"
                        width: 96
                        height: 40
                        enabled: smsBridgeController.canSend && composerField.text.trim().length > 0
                        onClicked: root.sendCurrentMessage()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: smsBridgeController.statusMessage
                    color: Theme.mutedTextColor
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Component.onCompleted: smsBridgeController.refreshConversations()
}
