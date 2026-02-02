import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1200
    height: 900
    title: "Network Camera Module Utilities Test"

    GridLayout {
        id: buttonGrid
        anchors.centerIn: parent
        columns: 2
        rowSpacing: 20
        columnSpacing: 20

        Button {
            text: "Run FetchCamerasSpecification"
            Layout.fillWidth: true
            Layout.fillHeight: true
            onClicked: {
                functions.FetchCamerasSpecificationCall();
            }
        }

        Button {
            text: "Run GetCodecs"
            Layout.fillWidth: true
            Layout.fillHeight: true
            onClicked: {
                functions.GetCodecs();
            }
        }
    }
}
