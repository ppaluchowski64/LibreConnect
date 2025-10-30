import QtQuick
import QtQuick.Controls
import "components"

Item {
    id: dashboard

    property int counter: 0

    Column {
        anchors.centerIn: parent
        spacing: 10

        Text {
            text: "Counter: " + dashboard.counter
            font.pixelSize: 20
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }

        ButtonCard {
            objectName: "IncreaseButton"
            label: "Increase"
            color: "#90caf9"
            onClicked: dashboard.counter++
        }

        ButtonCard {
            objectName: "ResetButton"
            label: "Reset"
            color: "#ef9a9a"
            onClicked: dashboard.counter = 0
        }
    }
}
