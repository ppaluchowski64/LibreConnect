import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

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
                text: "Media Remote"
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

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                Rectangle {
                    id: coverTemplate
                    readonly property int coverInset: 2
                    Layout.alignment: Qt.AlignHCenter
                    width: 144
                    height: 144
                    radius: 16
                    color: Theme.buttonColor
                    border.width: 1
                    border.color: Theme.panelBorderColor

                    Item {
                        id: coverImageContainer
                        anchors.fill: parent
                        anchors.margins: coverTemplate.coverInset
                        visible: coverImage.status === Image.Ready

                        Image {
                            id: coverImage
                            anchors.fill: parent
                            source: remoteInputController.coverImageSource
                            fillMode: Image.PreserveAspectCrop
                            cache: false
                            visible: false
                            layer.enabled: true
                        }

                        Rectangle {
                            id: coverMask
                            anchors.fill: parent
                            radius: Math.max(0, coverTemplate.radius - coverTemplate.coverInset)
                            visible: false
                            layer.enabled: true
                        }

                        MultiEffect {
                            anchors.fill: parent
                            source: coverImage
                            maskEnabled: true
                            maskSource: coverMask
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: coverImage.status !== Image.Ready
                        text: "Cover"
                        color: Theme.mutedTextColor
                        font.pixelSize: 15
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: remoteInputController.trackTitle.length > 0
                          ? remoteInputController.trackTitle
                          : "No track info"
                    color: Theme.textColor
                    font.pixelSize: 22
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: remoteInputController.trackArtist.length > 0
                          ? remoteInputController.trackArtist
                          : "Start playback on desktop to show metadata"
                    color: Theme.mutedTextColor
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    visible: remoteInputController.trackCollection.length > 0
                    text: remoteInputController.trackCollection
                    color: Theme.mutedTextColor
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Slider {
                    id: seekSlider
                    Layout.fillWidth: true
                    visible: remoteInputController.durationSeconds > 0
                    enabled: remoteInputController.durationSeconds > 0
                    from: 0
                    to: Math.max(remoteInputController.durationSeconds, 1)

                    onPressedChanged: {
                        if (!pressed && remoteInputController.durationSeconds > 0) {
                            remoteInputController.seekTo(value)
                        }
                    }

                    Connections {
                        target: remoteInputController
                        function onNowPlayingChanged() {
                            if (!seekSlider.pressed) {
                                seekSlider.value = remoteInputController.positionSeconds
                            }
                        }
                    }

                    Component.onCompleted: {
                        value = remoteInputController.positionSeconds
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: remoteInputController.durationSeconds > 0 && (remoteInputController.elapsedTime.length > 0
                             || remoteInputController.durationTime.length > 0)

                    Text {
                        Layout.alignment: Qt.AlignLeft
                        text: remoteInputController.elapsedTime
                        color: Theme.mutedTextColor
                        font.pixelSize: 13
                        visible: text.length > 0
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: remoteInputController.durationTime
                        color: Theme.mutedTextColor
                        font.pixelSize: 13
                        visible: text.length > 0
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 10

                    ToolButton {
                        icon.source: Theme.dark
                                     ? "qrc:/LibreConnect/mobile/previous_dark.svg"
                                     : "qrc:/LibreConnect/mobile/previous.svg"
                        icon.width: 26
                        icon.height: 26
                        onClicked: remoteInputController.sendMediaSignal(2)
                    }

                    ToolButton {
                        icon.source: remoteInputController.playing
                                     ? (Theme.dark
                                        ? "qrc:/LibreConnect/mobile/pause_dark.svg"
                                        : "qrc:/LibreConnect/mobile/pause.svg")
                                     : (Theme.dark
                                        ? "qrc:/LibreConnect/mobile/play_dark.svg"
                                        : "qrc:/LibreConnect/mobile/play.svg")
                        icon.width: 34
                        icon.height: 34
                        onClicked: remoteInputController.sendMediaSignal(0)
                    }

                    ToolButton {
                        icon.source: Theme.dark
                                     ? "qrc:/LibreConnect/mobile/next_dark.svg"
                                     : "qrc:/LibreConnect/mobile/next.svg"
                        icon.width: 26
                        icon.height: 26
                        onClicked: remoteInputController.sendMediaSignal(1)
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    spacing: 8

                    ToolButton {
                        icon.source: Theme.dark
                                     ? "qrc:/LibreConnect/mobile/volume_down_dark.svg"
                                     : "qrc:/LibreConnect/mobile/volume_down.svg"
                        icon.width: 28
                        icon.height: 28
                        onClicked: remoteInputController.sendMediaSignal(4)
                    }

                    Slider {
                        id: volumeSlider
                        Layout.fillWidth: true
                        from: 0
                        to: 100

                        onPressedChanged: {
                            if (!pressed) {
                                remoteInputController.setVolume(value)
                            }
                        }

                        Connections {
                            target: remoteInputController
                            function onNowPlayingChanged() {
                                if (!volumeSlider.pressed) {
                                    volumeSlider.value = remoteInputController.volume
                                }
                            }
                        }

                        Component.onCompleted: {
                            value = remoteInputController.volume
                        }
                    }

                    ToolButton {
                        icon.source: Theme.dark
                                     ? "qrc:/LibreConnect/mobile/volume_up_dark.svg"
                                     : "qrc:/LibreConnect/mobile/volume_up.svg"
                        icon.width: 28
                        icon.height: 28
                        onClicked: remoteInputController.sendMediaSignal(3)
                    }
                }

                Item {
                    Layout.fillWidth: true
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

    Component.onCompleted: remoteInputController.setSessionActive(true)
    Component.onDestruction: remoteInputController.setSessionActive(false)
}
