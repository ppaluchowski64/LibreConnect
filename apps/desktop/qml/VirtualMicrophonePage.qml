import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var virtualMicrophoneController
    required property var permissionStateController

    readonly property string windowTitleSuffix: "Virtual Microphone"

    background: Rectangle {
        color: "transparent"
    }

    Column {
        id: mainColumn
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Row {
            id: headerRow
            spacing: 16

            Text {
                text: "Virtual Microphone"
                font.family: Theme.fontFamily
                font.pixelSize: 30
                font.bold: true
                color: Theme.textColor
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            id: contentPanel
            width: parent.width
            height: Math.max(140, parent.height - headerRow.height - statusText.implicitHeight - (2 * mainColumn.spacing))
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            clip: true

            Flickable {
                id: panelFlickable
                anchors.fill: parent
                anchors.margins: 18
                clip: true
                contentWidth: width
                contentHeight: innerColumn.height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                Column {
                    id: innerColumn
                    width: panelFlickable.width
                    spacing: 14

                    Text {
                        text: "Audio Device"
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        color: Theme.textColor
                    }

                    ComboBox {
                        id: deviceCombo
                        width: parent.width
                        height: 42
                        model: root.virtualMicrophoneController.audioDevices
                        textRole: "name"
                        currentIndex: root.virtualMicrophoneController.selectedDeviceIndex
                        enabled: !root.virtualMicrophoneController.enabled
                                 && !root.virtualMicrophoneController.busy
                                 && root.virtualMicrophoneController.audioDevices.length > 0
                        onActivated: root.virtualMicrophoneController.selectedDeviceIndex = currentIndex

                        font.family: Theme.fontFamily
                        font.pixelSize: 15

                        contentItem: Text {
                            anchors.fill: parent
                            leftPadding: 10
                            rightPadding: deviceCombo.indicator.width + deviceCombo.spacing
                            text: deviceCombo.displayText.length > 0 ? deviceCombo.displayText : "No virtual audio devices detected"
                            font: deviceCombo.font
                            color: deviceCombo.enabled ? Theme.textColor : Theme.mutedTextColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Text {
                            x: Math.round(deviceCombo.width - width - 10)
                            y: Math.round((deviceCombo.height - height) / 2)
                            text: "v"
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: Theme.textColor
                        }

                        background: Rectangle {
                            radius: 8
                            color: deviceCombo.hovered
                                   ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.12) : Qt.darker(Theme.buttonColor, 1.05))
                                   : Theme.buttonColor
                            border.color: Theme.panelBorderColor
                            border.width: 1
                        }

                        delegate: ItemDelegate {
                            id: deviceOptionDelegate
                            width: deviceCombo.width
                            height: 42
                            hoverEnabled: true
                            highlighted: deviceCombo.highlightedIndex === index

                            contentItem: Text {
                                anchors.fill: parent
                                leftPadding: 10
                                rightPadding: 10
                                text: modelData.name
                                font.family: Theme.fontFamily
                                font.pixelSize: 15
                                color: (deviceOptionDelegate.highlighted || deviceOptionDelegate.hovered) ? Theme.selectedTextColor : Theme.textColor
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                color: deviceOptionDelegate.highlighted
                                       ? Theme.selectedColor
                                       : (deviceOptionDelegate.hovered ? Theme.buttonColor : Theme.panelColor)
                                border.color: Theme.panelBorderColor
                                border.width: 1
                            }
                        }

                        popup: Popup {
                            id: devicePopup
                            modal: true
                            closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
                            dim: false
                            x: 0
                            y: Math.round(deviceCombo.height + 4)
                            width: Math.round(deviceCombo.width)
                            implicitHeight: Math.min(contentItem.implicitHeight, 260)
                            padding: 1
                            onOpened: {
                                if (deviceCombo.currentIndex >= 0)
                                    devicePopupList.positionViewAtIndex(deviceCombo.currentIndex, ListView.Contain)
                            }

                            contentItem: ListView {
                                id: devicePopupList
                                clip: true
                                implicitHeight: contentHeight
                                boundsBehavior: Flickable.StopAtBounds
                                model: deviceCombo.delegateModel
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

                    RowLayout {
                        width: parent.width
                        spacing: 10

                        ThemedButton {
                            text: "Refresh Devices"
                            Layout.preferredWidth: 160
                            height: 40
                            font.pixelSize: 14
                            enabled: !root.virtualMicrophoneController.busy
                            onClicked: root.virtualMicrophoneController.refreshAudioDevices()
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 8
                        visible: root.virtualMicrophoneController.createDeviceSupported

                        Text {
                            text: "Create Device"
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            color: Theme.textColor
                        }

                        RowLayout {
                            width: parent.width
                            spacing: 10

                            TextField {
                                id: deviceNameField
                                Layout.fillWidth: true
                                height: 40
                                text: root.virtualMicrophoneController.newDeviceName
                                enabled: !root.virtualMicrophoneController.enabled && !root.virtualMicrophoneController.busy
                                color: Theme.textColor
                                placeholderText: "Device name"
                                placeholderTextColor: Theme.mutedTextColor
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                selectByMouse: true
                                onTextChanged: root.virtualMicrophoneController.newDeviceName = text

                                background: Rectangle {
                                    radius: 8
                                    color: Theme.buttonColor
                                    border.color: Theme.panelBorderColor
                                    border.width: 1
                                }
                            }

                            ThemedButton {
                                text: "Create"
                                Layout.preferredWidth: 120
                                height: 40
                                font.pixelSize: 14
                                enabled: !root.virtualMicrophoneController.enabled && !root.virtualMicrophoneController.busy
                                onClicked: root.virtualMicrophoneController.createAudioDevice()
                            }
                        }
                    }

                    ThemedButton {
                        text: root.virtualMicrophoneController.enabled
                              ? "Disable Virtual Microphone"
                              : (root.virtualMicrophoneController.busy ? "Working..." : "Enable Virtual Microphone")
                        width: 250
                        height: 42
                        enabled: (!root.virtualMicrophoneController.busy
                                 && root.permissionStateController.microphoneGranted
                                 && root.virtualMicrophoneController.audioDevices.length > 0) || root.virtualMicrophoneController.enabled
                        onClicked: root.virtualMicrophoneController.setVirtualMicrophoneEnabled(!root.virtualMicrophoneController.enabled)
                    }
                }
            }
        }

        Text {
            id: statusText
            text: root.virtualMicrophoneController.statusMessage
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.mutedTextColor
            font.family: Theme.fontFamily
            font.pixelSize: 15
        }
    }
}
