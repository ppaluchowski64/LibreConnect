import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1200
    height: 900
    title: "Network Camera Module Utilities Test"


    Button {
        text: "Run FetchCamerasSpecification"
        anchors.centerIn: parent

        onClicked: {
            functions.FetchCamerasSpecificationCall();
        }
    }
}