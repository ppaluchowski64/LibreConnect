import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var remoteInputController
    required property var goBackCallback

    background: Rectangle {
        color: Theme.backgroundColor
    }

    Dialog {
        id: permissionDialog
        property string message: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentWidth: Math.min(page.width - 32, 360)

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
            spacing: 12
            width: permissionDialog.contentWidth

            Text {
                width: parent.width
                text: "Permission Required"
                color: Theme.textColor
                font.pixelSize: 20
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: permissionDialog.message
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Button {
                width: parent.width
                text: "OK"
                onClicked: permissionDialog.close()
            }
        }
    }

    Connections {
        target: remoteInputController

        function onAccessibilityPermissionRequired(message) {
            permissionDialog.message = message
            permissionDialog.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        RowLayout {
            Layout.fillWidth: true

            ToolButton {
                id: backButton
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
                text: "Remote Keyboard"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            Item {
                Layout.preferredWidth: backButton.implicitWidth
                Layout.preferredHeight: backButton.implicitHeight
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true

            background: Rectangle {
                radius: 16
                color: Theme.panelColor
                border.width: 1
                border.color: Theme.panelBorderColor
            }

            TextField {
                id: keyboardProxy
                width: 1
                height: 1
                opacity: 0.01
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 1
                focus: false
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                property string previousText: ""
                property bool suppressAcceptedOnce: false

                function sendDelta(previousValue, nextValue) {
                    let prefix = 0
                    const previousLength = previousValue.length
                    const nextLength = nextValue.length

                    while (prefix < previousLength
                           && prefix < nextLength
                           && previousValue[prefix] === nextValue[prefix]) {
                        ++prefix
                    }

                    let previousSuffix = previousLength - 1
                    let nextSuffix = nextLength - 1
                    while (previousSuffix >= prefix
                           && nextSuffix >= prefix
                           && previousValue[previousSuffix] === nextValue[nextSuffix]) {
                        --previousSuffix
                        --nextSuffix
                    }

                    const removedCount = Math.max(0, previousSuffix - prefix + 1)
                    for (let i = 0; i < removedCount; ++i) {
                        remoteInputController.sendQtKeyEvent(Qt.Key_Backspace, "", 0)
                    }

                    const inserted = nextValue.slice(prefix, nextSuffix + 1)
                    for (let i = 0; i < inserted.length; ++i) {
                        remoteInputController.sendQtKeyEvent(0, inserted[i], 0)
                    }
                }

                onTextEdited: {
                    sendDelta(previousText, text)
                    previousText = text

                    // Keep internal buffer small while preserving delta behavior.
                    if (text.length > 96) {
                        text = ""
                        previousText = ""
                    }
                }

                onAccepted: {
                    if (suppressAcceptedOnce) {
                        suppressAcceptedOnce = false
                        return
                    }

                    remoteInputController.sendQtKeyEvent(Qt.Key_Return, "", 0)
                    keyboardProxy.forceActiveFocus()
                    Qt.callLater(function() { Qt.inputMethod.show() })
                }

                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        keyboardProxy.suppressAcceptedOnce = true
                        remoteInputController.sendQtKeyEvent(Qt.Key_Return, "", 0)
                        keyboardProxy.forceActiveFocus()
                        Qt.callLater(function() { keyboardProxy.suppressAcceptedOnce = false })
                        Qt.callLater(function() { Qt.inputMethod.show() })
                        event.accepted = true
                        return
                    }

                    if (event.key === Qt.Key_Backspace) {
                        remoteInputController.sendQtKeyEvent(Qt.Key_Backspace, "", 0)
                        if (keyboardProxy.text.length > 0) {
                            keyboardProxy.text = keyboardProxy.text.slice(0, -1)
                            keyboardProxy.previousText = keyboardProxy.text
                        }
                        event.accepted = true
                        return
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14

                Item {
                    Layout.fillHeight: true
                }

                Image {
                    Layout.alignment: Qt.AlignHCenter
                    source: Theme.dark
                            ? "qrc:/LibreConnect/mobile/keyboard_dark.svg"
                            : "qrc:/LibreConnect/mobile/keyboard.svg"
                    sourceSize.width: 64
                    sourceSize.height: 64
                    width: 64
                    height: 64
                    fillMode: Image.PreserveAspectFit
                }

                Text {
                    Layout.fillWidth: true
                    text: "Use your phone keyboard to type on desktop."
                    color: Theme.textColor
                    font.pixelSize: 18
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: "Backspace and Enter are forwarded even when no text is currently typed."
                    color: Theme.mutedTextColor
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    text: "Show Keyboard"
                    onClicked: {
                        keyboardProxy.forceActiveFocus()
                        Qt.inputMethod.show()
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: remoteInputController.statusMessage
            color: Theme.mutedTextColor
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
