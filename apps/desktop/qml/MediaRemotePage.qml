import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var mediaNotificationController
    readonly property string windowTitleSuffix: "Media Remote"

    background: Rectangle {
        color: "transparent"
    }

    component MediaSlider: Slider {
        id: control

        background: Rectangle {
            x: control.leftPadding
            y: control.topPadding + Math.round((control.availableHeight - height) / 2)
            width: control.availableWidth
            height: 5
            radius: 3
            color: Theme.dark ? "#303030" : Theme.panelBorderColor

            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: Theme.dark ? "#f2f2f2" : Theme.selectedColor
            }
        }

        handle: Rectangle {
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + Math.round((control.availableHeight - height) / 2)
            width: 26
            height: 26
            radius: 13
            color: Theme.dark ? "#f7f7f7" : Theme.textColor
            border.color: Theme.dark ? "#cfcfcf" : Theme.panelBorderColor
            border.width: 1
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: "Media Remote"
            font.family: Theme.fontFamily
            font.pixelSize: 30
            font.bold: true
            color: Theme.textColor
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: Theme.backgroundColor
            border.color: Theme.panelBorderColor
            border.width: 1
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                Rectangle {
                    id: coverTemplate
                    readonly property int coverInset: 2
                    Layout.alignment: Qt.AlignHCenter
                    width: Math.min(220, Math.max(150, root.width * 0.22))
                    height: width
                    radius: 14
                    color: Theme.buttonColor
                    border.width: 1
                    border.color: Theme.panelBorderColor
                    clip: true

                    Item {
                        id: coverImageContainer
                        anchors.fill: parent
                        anchors.margins: coverTemplate.coverInset
                        visible: coverImage.status === Image.Ready

                        Image {
                            id: coverImage
                            anchors.fill: parent
                            source: mediaNotificationController.coverImageSource
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
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 620
                    Layout.alignment: Qt.AlignHCenter
                    text: mediaNotificationController.trackTitle.length > 0
                          ? mediaNotificationController.trackTitle
                          : "No track info"
                    color: Theme.textColor
                    font.family: Theme.fontFamily
                    font.pixelSize: 24
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 620
                    Layout.alignment: Qt.AlignHCenter
                    text: mediaNotificationController.trackArtist.length > 0
                          ? mediaNotificationController.trackArtist
                          : "Start playback on your phone to show metadata"
                    color: Theme.mutedTextColor
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 620
                    Layout.alignment: Qt.AlignHCenter
                    visible: mediaNotificationController.trackAlbum.length > 0
                    text: mediaNotificationController.trackAlbum
                    color: Theme.mutedTextColor
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                MediaSlider {
                    id: seekSlider
                    Layout.fillWidth: true
                    Layout.maximumWidth: 620
                    Layout.alignment: Qt.AlignHCenter
                    enabled: mediaNotificationController.duration > 0
                    from: 0
                    to: Math.max(mediaNotificationController.duration, 1)

                    onPressedChanged: {
                        if (!pressed && mediaNotificationController.duration > 0) {
                            mediaNotificationController.seekTo(value)
                        }
                    }

                    Connections {
                        target: mediaNotificationController
                        function onTrackInfoChanged() {
                            if (!seekSlider.pressed) {
                                seekSlider.value = mediaNotificationController.position
                            }
                        }
                    }

                    Component.onCompleted: value = mediaNotificationController.position
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 620
                    Layout.alignment: Qt.AlignHCenter

                    Text {
                        text: mediaNotificationController.elapsedTime
                        color: Theme.mutedTextColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        text: mediaNotificationController.durationTime
                        color: Theme.mutedTextColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12

                    ThemedButton {
                        width: 42
                        height: 42
                        iconSource: "previous.svg"
                        darkIconSource: "previous_dark.svg"
                        iconSize: 26
                        onClicked: mediaNotificationController.sendMediaSignal(2)
                    }

                    ThemedButton {
                        width: 46
                        height: 46
                        iconSource: mediaNotificationController.playing ? "pause.svg" : "play.svg"
                        darkIconSource: mediaNotificationController.playing ? "pause_dark.svg" : "play_dark.svg"
                        iconSize: 34
                        onClicked: mediaNotificationController.sendMediaSignal(0)
                    }

                    ThemedButton {
                        width: 42
                        height: 42
                        iconSource: "next.svg"
                        darkIconSource: "next_dark.svg"
                        iconSize: 26
                        onClicked: mediaNotificationController.sendMediaSignal(1)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 620
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    spacing: 8

                    ThemedButton {
                        width: 42
                        height: 42
                        iconSource: "volume_down.svg"
                        darkIconSource: "volume_down_dark.svg"
                        iconSize: 28
                        onClicked: mediaNotificationController.sendMediaSignal(4)
                    }

                    MediaSlider {
                        id: volumeSlider
                        Layout.fillWidth: true
                        from: 0
                        to: 100

                        onPressedChanged: {
                            if (!pressed) {
                                mediaNotificationController.setVolume(value)
                            }
                        }

                        Connections {
                            target: mediaNotificationController
                            function onTrackInfoChanged() {
                                if (!volumeSlider.pressed) {
                                    volumeSlider.value = mediaNotificationController.volume
                                }
                            }
                        }

                        Component.onCompleted: value = mediaNotificationController.volume
                    }

                    ThemedButton {
                        width: 42
                        height: 42
                        iconSource: "volume_up.svg"
                        darkIconSource: "volume_up_dark.svg"
                        iconSize: 28
                        onClicked: mediaNotificationController.sendMediaSignal(3)
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }
    }
}
