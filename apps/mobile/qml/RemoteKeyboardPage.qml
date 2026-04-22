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
                width: 32
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

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: "Type using your Android keyboard. Keystrokes are sent to the desktop in real time."
                    color: Theme.textColor
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }

                TextField {
                    id: keyboardField
                    Layout.fillWidth: true
                    placeholderText: "Tap here and type"
                    color: Theme.textColor
                    selectionColor: Theme.selectedColor
                    selectedTextColor: Theme.selectedTextColor
                    font.pixelSize: 18
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    property string previousText: ""

                    function sendDelta(previousValue, nextValue) {
                        let prefix = 0
                        const previousLength = previousValue.length
                        const nextLength = nextValue.length

                        while (prefix < previousLength &&
                               prefix < nextLength &&
                               previousValue[prefix] === nextValue[prefix]) {
                            ++prefix
                        }

                        let previousSuffix = previousLength - 1
                        let nextSuffix = nextLength - 1
                        while (previousSuffix >= prefix &&
                               nextSuffix >= prefix &&
                               previousValue[previousSuffix] === nextValue[nextSuffix]) {
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

                    background: Rectangle {
                        radius: 12
                        color: Theme.buttonColor
                        border.width: 1
                        border.color: Theme.panelBorderColor
                    }

                    onTextEdited: {
                        sendDelta(previousText, text)

                        previousText = text
                        if (text.length > 48) {
                            text = ""
                            previousText = ""
                        }
                    }

                    onAccepted: {
                        remoteInputController.sendQtKeyEvent(Qt.Key_Return, "\n", 0)
                        text = ""
                        previousText = ""
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Button {
                        Layout.fillWidth: true
                        text: "Backspace"
                        onClicked: remoteInputController.sendQtKeyEvent(Qt.Key_Backspace, "", 0)
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Enter"
                        onClicked: remoteInputController.sendQtKeyEvent(Qt.Key_Return, "\n", 0)
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: "Show Keyboard"
                    onClicked: {
                        keyboardField.forceActiveFocus()
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

    Component.onCompleted: {
        keyboardField.forceActiveFocus()
        Qt.inputMethod.show()
    }
}
