import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var remoteInputController
    required property var goBackCallback
    property int activeModifiers: 0
    property bool restoreKeyboardAfterToolTap: false

    function toggleModifier(mask) {
        if (activeModifiers & mask)
            activeModifiers &= ~mask
        else
            activeModifiers |= mask
    }

    function sendSpecialKey(key) {
        remoteInputController.sendQtKeyEvent(key, "", activeModifiers)
        activeModifiers = 0
    }

    function rememberKeyboardVisibility() {
        restoreKeyboardAfterToolTap = Qt.inputMethod.visible
    }

    function restoreKeyboardIfNeeded() {
        if (!restoreKeyboardAfterToolTap)
            return

        Qt.callLater(function() {
            keyboardProxy.forceActiveFocus()
            Qt.inputMethod.show()
        })
    }

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

            TextArea {
                id: keyboardProxy
                width: 1
                height: 1
                opacity: 0.01
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 1
                focus: false
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhMultiLine
                wrapMode: Text.NoWrap
                property string previousText: ""
                property bool ignoreChange: false

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
                        remoteInputController.sendQtKeyEvent(Qt.Key_Backspace, "", page.activeModifiers)
                    }

                    const inserted = nextValue.slice(prefix, nextSuffix + 1)
                    for (let i = 0; i < inserted.length; ++i) {
                        remoteInputController.sendQtKeyEvent(0, inserted[i], page.activeModifiers)
                    }
                }

                onTextChanged: {
                    if (ignoreChange) return
                    sendDelta(previousText, text)
                    previousText = text

                    // Keep internal buffer small while preserving delta behavior.
                    if (text.length > 96) {
                        ignoreChange = true
                        text = ""
                        previousText = ""
                        ignoreChange = false
                    }
                }

                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Backspace) {
                        remoteInputController.sendQtKeyEvent(Qt.Key_Backspace, "", page.activeModifiers)
                        if (keyboardProxy.text.length > 0) {
                            ignoreChange = true
                            keyboardProxy.text = keyboardProxy.text.slice(0, -1)
                            keyboardProxy.previousText = keyboardProxy.text
                            ignoreChange = false
                        }
                        event.accepted = true
                        return
                    }
                }
            }

            Item {
                id: focusSink
                width: 1
                height: 1
                focus: false
            }

            ScrollView {
                id: keyboardScroll
                anchors.fill: parent
                anchors.margins: 16
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: Math.max(0, keyboardScroll.availableWidth)
                    spacing: 14

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Qt.inputMethod.visible ? 8 : 40
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
                        text: "Text is forwarded to desktop in real time."
                        color: Theme.mutedTextColor
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        contentWidth: specialKeysRow.implicitWidth
                        contentHeight: height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        Row {
                            id: specialKeysRow
                            height: parent.height
                            spacing: 8

                            Repeater {
                                model: [
                                    { label: "Ctrl", key: Qt.Key_Control, modifier: Qt.ControlModifier },
                                    { label: "Alt", key: Qt.Key_Alt, modifier: Qt.AltModifier },
                                    { label: "Super", key: Qt.Key_Meta, modifier: Qt.MetaModifier },
                                    { label: "Shift", key: Qt.Key_Shift, modifier: Qt.ShiftModifier },
                                    { label: "Esc", key: Qt.Key_Escape, modifier: 0 },
                                    { label: "Tab", key: Qt.Key_Tab, modifier: 0 },
                                    { label: "Del", key: Qt.Key_Delete, modifier: 0 },
                                    { label: "Home", key: Qt.Key_Home, modifier: 0 },
                                    { label: "End", key: Qt.Key_End, modifier: 0 },
                                    { label: "PgUp", key: Qt.Key_PageUp, modifier: 0 },
                                    { label: "PgDn", key: Qt.Key_PageDown, modifier: 0 },
                                    { label: "F1", key: Qt.Key_F1, modifier: 0 },
                                    { label: "F2", key: Qt.Key_F2, modifier: 0 },
                                    { label: "F3", key: Qt.Key_F3, modifier: 0 },
                                    { label: "F4", key: Qt.Key_F4, modifier: 0 },
                                    { label: "F5", key: Qt.Key_F5, modifier: 0 },
                                    { label: "F6", key: Qt.Key_F6, modifier: 0 },
                                    { label: "F7", key: Qt.Key_F7, modifier: 0 },
                                    { label: "F8", key: Qt.Key_F8, modifier: 0 },
                                    { label: "F9", key: Qt.Key_F9, modifier: 0 },
                                    { label: "F10", key: Qt.Key_F10, modifier: 0 },
                                    { label: "F11", key: Qt.Key_F11, modifier: 0 },
                                    { label: "F12", key: Qt.Key_F12, modifier: 0 }
                                ]

                                Button {
                                    width: Math.max(54, implicitWidth)
                                    height: 44
                                    text: modelData.label
                                    focusPolicy: Qt.NoFocus
                                    checkable: modelData.modifier !== 0
                                    checked: modelData.modifier !== 0 && (page.activeModifiers & modelData.modifier)
                                    onPressedChanged: {
                                        if (pressed)
                                            page.rememberKeyboardVisibility()
                                    }
                                    onClicked: {
                                        if (modelData.modifier !== 0)
                                            page.toggleModifier(modelData.modifier)
                                        else
                                            page.sendSpecialKey(modelData.key)
                                        page.restoreKeyboardIfNeeded()
                                    }
                                }
                            }
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        text: Qt.inputMethod.visible ? "Hide Keyboard" : "Show Keyboard"
                        focusPolicy: Qt.NoFocus
                        onClicked: {
                            if (Qt.inputMethod.visible) {
                                keyboardProxy.focus = false
                                focusSink.forceActiveFocus()
                                Qt.inputMethod.hide()
                            } else {
                                keyboardProxy.forceActiveFocus()
                                Qt.inputMethod.show()
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 16
                    }
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
