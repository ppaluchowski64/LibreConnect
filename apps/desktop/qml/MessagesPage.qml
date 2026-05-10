import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var smsBridgeController
    readonly property string windowTitleSuffix: "Messages"
    property bool restoringContactsScroll: false
    property real preservedContactsContentY: 0
    property bool stickMessagesToBottom: true

    function sendCurrentMessage() {
        const message = composerField.text.trim()
        if (message.length === 0) {
            return
        }

        smsBridgeController.sendMessage(message)
        composerField.text = ""
    }

    function preserveContactsScroll() {
        preservedContactsContentY = contactsList.contentY
        restoringContactsScroll = true
        contactsScrollRestoreTimer.restart()
    }

    function restoreContactsScroll() {
        if (!restoringContactsScroll) {
            return
        }

        const maxContentY = Math.max(0, contactsList.contentHeight - contactsList.height)
        contactsList.contentY = Math.max(0, Math.min(preservedContactsContentY, maxContentY))
    }

    function selectConversation(number, name) {
        preserveContactsScroll()
        smsBridgeController.selectConversation(number, name)
        restoreContactsScroll()
    }

    function messagesAtBottom() {
        const maxContentY = Math.max(0, messagesList.contentHeight - messagesList.height)
        return messagesList.contentY >= maxContentY - 8
    }

    function scrollMessagesToBottom() {
        if (messagesList.count > 0) {
            messagesList.positionViewAtEnd()
        }
    }

    function scrollMessagesToBottomLater() {
        Qt.callLater(root.scrollMessagesToBottom)
    }

    background: Rectangle {
        color: Theme.panelColor
    }

    Connections {
        target: smsBridgeController

        function onMessagesChanged() {
            root.stickMessagesToBottom = true
            root.scrollMessagesToBottomLater()
            root.restoreContactsScroll()
            Qt.callLater(root.restoreContactsScroll)
        }

        function onContactsChanged() {
            root.restoreContactsScroll()
            Qt.callLater(root.restoreContactsScroll)
        }
    }

    Timer {
        id: contactsScrollRestoreTimer
        interval: 4000
        repeat: false
        onTriggered: root.restoringContactsScroll = false
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
                        enabled: !smsBridgeController.busy
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
                    onCountChanged: root.restoreContactsScroll()
                    onContentHeightChanged: root.restoreContactsScroll()

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
                            onClicked: root.selectConversation(modelData.number, modelData.name)
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
                                text: modelData.loading ? "Loading..." : modelData.preview
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
                        onMovementEnded: root.stickMessagesToBottom = root.messagesAtBottom()
                        onContentYChanged: {
                            if (moving || dragging) {
                                root.stickMessagesToBottom = root.messagesAtBottom()
                            }
                        }
                        onContentHeightChanged: {
                            if (root.stickMessagesToBottom) {
                                root.scrollMessagesToBottomLater()
                            }
                        }

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
                                    readonly property bool hasAttachments: modelData.attachments && modelData.attachments.length > 0
                                    width: Math.min(
                                        maxBubbleWidth,
                                        Math.max(
                                            minBubbleWidth,
                                            Math.max(
                                                hasAttachments ? 220 : 0,
                                                Math.max(bubbleText.implicitWidth, metaRow.implicitWidth)
                                            ) + 16
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
                                            visible: text.length > 0
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            color: modelData.incoming ? Theme.textColor : Theme.selectedTextColor
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 14
                                        }

                                        Repeater {
                                            model: modelData.attachments ? modelData.attachments : []

                                            delegate: Column {
                                                id: attachmentDelegate
                                                required property var modelData
                                                property bool imageFailed: false
                                                readonly property bool showImage: modelData.previewable
                                                                                  && modelData.fileUrl.length > 0
                                                                                  && !imageFailed
                                                width: bubbleContent.width
                                                spacing: 4

                                                Rectangle {
                                                    width: parent.width
                                                    height: attachmentDelegate.showImage ? 160 : 34
                                                    radius: 8
                                                    color: Theme.backgroundColor
                                                    border.width: 1
                                                    border.color: Theme.panelBorderColor
                                                    clip: true

                                                    Image {
                                                        id: attachmentPreview
                                                        anchors.fill: parent
                                                        anchors.margins: 4
                                                        source: attachmentDelegate.showImage ? modelData.fileUrl : ""
                                                        fillMode: Image.PreserveAspectFit
                                                        asynchronous: true
                                                        visible: attachmentDelegate.showImage && status !== Image.Error
                                                        onStatusChanged: {
                                                            if (status === Image.Error) {
                                                                attachmentDelegate.imageFailed = true
                                                            }
                                                        }
                                                    }

                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: modelData.loading
                                                              ? "Loading attachment..."
                                                              : (modelData.failed ? "Attachment unavailable" : "Open attachment")
                                                        visible: !attachmentDelegate.showImage
                                                        color: Theme.mutedTextColor
                                                        font.family: Theme.fontFamily
                                                        font.pixelSize: 12
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        enabled: modelData.fileUrl.length > 0
                                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                        onClicked: Qt.openUrlExternally(modelData.fileUrl)
                                                    }
                                                }
                                            }
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
                                root.stickMessagesToBottom = true
                                root.scrollMessagesToBottomLater()
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
