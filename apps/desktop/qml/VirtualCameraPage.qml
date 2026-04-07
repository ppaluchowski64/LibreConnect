import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
    readonly property string windowTitleSuffix: "Virtual Camera"

    VirtualCameraController {
        id: virtualCameraController
    }

    background: Rectangle {
        color: Theme.backgroundColor
    }

    Column {
        anchors.fill: parent
        anchors.margins: 28
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
            height: 220
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            Column {
                anchors.fill: parent
                anchors.margins: 18
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
                    width: parent.width
                    model: virtualCameraController.cameraDescriptions
                    currentIndex: virtualCameraController.selectedCameraIndex
                    enabled: !virtualCameraController.enabled && !virtualCameraController.busy
                    onActivated: virtualCameraController.selectedCameraIndex = currentIndex
                }

                ComboBox {
                    width: parent.width
                    model: virtualCameraController.formatDescriptions
                    currentIndex: virtualCameraController.selectedFormatIndex
                    enabled: !virtualCameraController.enabled && !virtualCameraController.busy && count > 0
                    onActivated: virtualCameraController.selectedFormatIndex = currentIndex
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
