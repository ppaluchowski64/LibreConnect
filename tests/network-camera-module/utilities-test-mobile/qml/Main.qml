import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    visible: true
    width: 360
    height: 640
    title: "Camera Utils"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle {
                color: "#eeeeee"
                border.color: "#cccccc"
                radius: 4
            }

            TextArea {
                id: outputText
                text: "Results will appear here..."
                readOnly: true
                wrapMode: Text.Wrap
                font.pixelSize: 14
                padding: 10
            }
        }

        Button {
            text: "Fetch Camera Specs"
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            onClicked: {
                outputText.text = "Fetching specs...\n"
                var result = functions.fetchSpecs()
                outputText.text = result
            }
        }

        Button {
            text: "Get H264 Decoder"
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            onClicked: {
                var h264_id = 27
                outputText.text = functions.getDecoder(h264_id)
            }
        }

        Button {
            text: "Get H264 Encoder"
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            onClicked: {
                var h264_id = 27
                outputText.text = functions.getEncoder(h264_id)
            }
        }
    }
}