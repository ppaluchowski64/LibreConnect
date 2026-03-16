import QtQuick
import QtQuick.Controls

Page {
    id: root

    required property var windowRef
    readonly property string windowTitleSuffix: "Virtual Camera"

    VirtualCameraController {
        id: virtualCameraController
    }

    background: Rectangle {
        color: "white"
    }

    Column {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        Row {
            spacing: 16

            Button {
                text: "Back"
                width: 100
                height: 42
                onClicked: windowRef.goBack()
            }

            Text {
                text: "Virtual Camera"
                font.pixelSize: 30
                font.bold: true
                color: "#111111"
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            width: parent.width
            height: 220
            radius: 12
            color: "#f4f4f4"
            border.color: "#d8d8d8"

            Column {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                Text {
                    text: "Choose the remote camera and format to expose through the virtual camera."
                    font.pixelSize: 18
                    wrapMode: Text.WordWrap
                    color: "#111111"
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

                Button {
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
            color: "#444444"
            font.pixelSize: 15
        }
    }
}
