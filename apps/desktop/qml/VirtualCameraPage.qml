import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
    readonly property string windowTitleSuffix: "Virtual Camera"

    property string selectedAspectRatio: ""
    property var availableAspectRatios: {
        var ratios = {}
        var list = []
        var formats = virtualCameraController.formatList || []
        for (var i = 0; i < formats.length; ++i) {
            var ar = formats[i].aspectRatio
            if (!ratios[ar]) {
                ratios[ar] = true
                list.push(ar)
            }
        }
        return list
    }
    
    onAvailableAspectRatiosChanged: {
        if (availableAspectRatios.indexOf(selectedAspectRatio) === -1) {
            selectedAspectRatio = availableAspectRatios.length > 0 ? availableAspectRatios[0] : ""
        }
    }

    VirtualCameraController {
        id: virtualCameraController
    }

    background: Rectangle {
        color: Theme.backgroundColor
    }

    Flickable {
        anchors.fill: parent
        contentHeight: mainColumn.height + 56
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        Column {
            id: mainColumn
            width: parent.width - 56
            x: 28
            y: 28
            spacing: 20

        Row {
            spacing: 16

            ThemedButton {
                text: "Back"
                width: 100
                height: 42
                onClicked: windowRef.goBack()
            }

            Text {
                text: "Virtual Camera"
                font.family: Theme.fontFamily
                font.pixelSize: 30
                font.bold: true
                color: Theme.textColor
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            width: parent.width
            height: innerColumn.height + 36
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            clip: true

            Column {
                id: innerColumn
                width: parent.width - 36
                x: 18
                y: 18
                spacing: 14

                Text {
                    text: "Choose the remote camera and format to expose through the virtual camera."
                    font.family: Theme.fontFamily
                    font.pixelSize: 18
                    wrapMode: Text.WordWrap
                    color: Theme.textColor
                    width: parent.width
                }

                ComboBox {
                    id: cameraCombo
                    width: parent.width
                    height: 42
                    model: virtualCameraController.cameraDescriptions
                    currentIndex: virtualCameraController.selectedCameraIndex
                    enabled: !virtualCameraController.enabled && !virtualCameraController.busy
                    onActivated: virtualCameraController.selectedCameraIndex = currentIndex

                    font.family: Theme.fontFamily
                    font.pixelSize: 15

                    contentItem: Text {
                        leftPadding: 10
                        rightPadding: cameraCombo.indicator.width + cameraCombo.spacing
                        text: cameraCombo.displayText
                        font: cameraCombo.font
                        color: Theme.textColor
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    indicator: Text {
                        x: cameraCombo.width - width - 10
                        y: (cameraCombo.height - height) / 2
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
                        width: cameraCombo.width
                        height: 42
                        highlighted: cameraCombo.highlightedIndex === index

                        contentItem: Text {
                            text: modelData
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
                        y: cameraCombo.height + 4
                        width: cameraCombo.width
                        implicitHeight: contentItem.implicitHeight
                        padding: 1

                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: cameraCombo.popup.visible ? cameraCombo.delegateModel : null
                            currentIndex: cameraCombo.highlightedIndex
                        }

                        background: Rectangle {
                            radius: 8
                            color: Theme.panelColor
                            border.color: Theme.panelBorderColor
                            border.width: 1
                        }
                    }
                }

                Text {
                    text: "Aspect Ratio:"
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    color: Theme.textColor
                    visible: root.availableAspectRatios.length > 0
                }

                Flickable {
                    width: parent.width
                    height: 36
                    contentWidth: aspectRow.width
                    visible: root.availableAspectRatios.length > 0
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: aspectRow
                        spacing: 10
                        Repeater {
                            model: root.availableAspectRatios
                            delegate: Rectangle {
                                width: arText.implicitWidth + 32
                                height: 36
                                radius: 18
                                color: root.selectedAspectRatio === modelData ? Theme.selectedColor : (arMouse.containsMouse ? Theme.buttonColor : Theme.backgroundColor)
                                border.color: root.selectedAspectRatio === modelData ? Theme.selectedBorderColor : Theme.panelBorderColor
                                border.width: 1

                                Text {
                                    id: arText
                                    anchors.centerIn: parent
                                    text: modelData
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 14
                                    color: root.selectedAspectRatio === modelData ? (Theme.dark ? "#ffffff" : Theme.textColor) : Theme.textColor
                                }

                                MouseArea {
                                    id: arMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: !virtualCameraController.enabled && !virtualCameraController.busy
                                    onClicked: root.selectedAspectRatio = modelData
                                }
                            }
                        }
                    }
                }

                Text {
                    text: "Resolution:"
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    color: Theme.textColor
                    visible: root.availableAspectRatios.length > 0
                }

                Flow {
                    width: parent.width
                    spacing: 12
                    visible: root.availableAspectRatios.length > 0

                    Repeater {
                        model: {
                            var formats = virtualCameraController.formatList || []
                            var filtered = []
                            for (var i = 0; i < formats.length; ++i) {
                                if (formats[i].aspectRatio === root.selectedAspectRatio) {
                                    filtered.push(formats[i])
                                }
                            }
                            return filtered
                        }

                        delegate: Rectangle {
                            width: 140
                            height: 64
                            radius: 8
                            color: virtualCameraController.selectedFormatIndex === modelData.index ? Theme.selectedColor : (fmtMouse.containsMouse ? Theme.buttonColor : Theme.backgroundColor)
                            border.color: virtualCameraController.selectedFormatIndex === modelData.index ? Theme.selectedBorderColor : Theme.panelBorderColor
                            border.width: 1

                            Column {
                                anchors.centerIn: parent
                                spacing: 4

                                Text {
                                    text: modelData.label
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 15
                                    font.bold: true
                                    color: virtualCameraController.selectedFormatIndex === modelData.index ? (Theme.dark ? "#ffffff" : Theme.textColor) : Theme.textColor
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                                Text {
                                    text: modelData.fps + " FPS"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: virtualCameraController.selectedFormatIndex === modelData.index ? (Theme.dark ? "#ffffff" : Theme.mutedTextColor) : Theme.mutedTextColor
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                            }

                            MouseArea {
                                id: fmtMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: !virtualCameraController.enabled && !virtualCameraController.busy
                                onClicked: virtualCameraController.selectedFormatIndex = modelData.index
                            }
                        }
                    }
                }

                ThemedButton {
                    text: virtualCameraController.enabled ? "Disable Virtual Camera" : (virtualCameraController.busy ? "Working..." : "Enable Virtual Camera")
                    width: 220
                    height: 42
                    enabled: !virtualCameraController.busy
                    onClicked: virtualCameraController.setVirtualCameraEnabled(!virtualCameraController.enabled)
                }
            }
        }

        Text {
            text: virtualCameraController.statusMessage
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.mutedTextColor
            font.family: Theme.fontFamily
            font.pixelSize: 15
        }
        }
    }
}
