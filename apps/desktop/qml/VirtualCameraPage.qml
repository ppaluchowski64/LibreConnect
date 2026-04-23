import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: root

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
                text: "Virtual Camera"
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
                            anchors.fill: parent
                            leftPadding: 10
                            rightPadding: cameraCombo.indicator.width + cameraCombo.spacing
                            text: cameraCombo.displayText
                            font: cameraCombo.font
                            color: Theme.textColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Text {
                            x: Math.round(cameraCombo.width - width - 10)
                            y: Math.round((cameraCombo.height - height) / 2)
                            text: "v"
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: Theme.textColor
                        }

                        background: Rectangle {
                            radius: 8
                            color: cameraCombo.hovered
                                   ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.12) : Qt.darker(Theme.buttonColor, 1.05))
                                   : Theme.buttonColor
                            border.color: Theme.panelBorderColor
                            border.width: 1
                        }

                        delegate: ItemDelegate {
                            id: cameraOptionDelegate
                            width: cameraCombo.width
                            height: 42
                            hoverEnabled: true
                            highlighted: cameraCombo.highlightedIndex === index

                            contentItem: Text {
                                anchors.fill: parent
                                leftPadding: 10
                                rightPadding: 10
                                text: modelData
                                font.family: Theme.fontFamily
                                font.pixelSize: 15
                                color: (cameraOptionDelegate.highlighted || cameraOptionDelegate.hovered) ? Theme.selectedTextColor : Theme.textColor
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                color: cameraOptionDelegate.highlighted
                                       ? Theme.selectedColor
                                       : (cameraOptionDelegate.hovered ? Theme.buttonColor : Theme.panelColor)
                                border.color: Theme.panelBorderColor
                                border.width: 1
                            }
                        }

                        popup: Popup {
                            id: cameraPopup
                            modal: true
                            dim: false
                            x: 0
                            y: Math.round(cameraCombo.height + 4)
                            width: Math.round(cameraCombo.width)
                            implicitHeight: Math.min(contentItem.implicitHeight, 260)
                            padding: 1
                            onOpened: {
                                if (cameraCombo.currentIndex >= 0)
                                    cameraPopupList.positionViewAtIndex(cameraCombo.currentIndex, ListView.Contain)
                            }

                            contentItem: ListView {
                                id: cameraPopupList
                                clip: true
                                implicitHeight: contentHeight
                                boundsBehavior: Flickable.StopAtBounds
                                model: cameraCombo.delegateModel
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

                    Text {
                        text: "Aspect Ratio:"
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        color: Theme.textColor
                        visible: root.availableAspectRatios.length > 0
                    }

                    Flow {
                        width: parent.width
                        spacing: 10
                        visible: root.availableAspectRatios.length > 0

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
                                    color: root.selectedAspectRatio === modelData ? (Theme.selectedTextColor) : Theme.textColor
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
                                        color: virtualCameraController.selectedFormatIndex === modelData.index ? (Theme.selectedTextColor) : Theme.textColor
                                        anchors.horizontalCenter: parent.horizontalCenter
                                    }
                                    Text {
                                        text: modelData.fps + " FPS"
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: virtualCameraController.selectedFormatIndex === modelData.index ? (Theme.selectedTextColor) : Theme.mutedTextColor
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
        }

        Text {
            id: statusText
            text: virtualCameraController.statusMessage
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.mutedTextColor
            font.family: Theme.fontFamily
            font.pixelSize: 15
        }
    }
}
