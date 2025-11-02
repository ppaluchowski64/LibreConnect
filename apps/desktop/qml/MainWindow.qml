import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import LibreConnect.desktop 1.0

Window {
    visible: true
    width: 740
    height: 420
    minimumWidth: width
    minimumHeight: height
    maximumWidth: width
    maximumHeight: height
    title: "LibreConnect - Setup"

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: Initial {}

        pushEnter: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 300
            }
            PropertyAnimation {
                property: "x"
                from: stackView.width
                to: 0
                duration: 300
                easing.type: Easing.OutCubic
            }
        }

        pushExit: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: 300
            }
            PropertyAnimation {
                property: "x"
                from: 0
                to: -stackView.width * 0.3
                duration: 300
                easing.type: Easing.OutCubic
            }
        }

        popEnter: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 300
            }
            PropertyAnimation {
                property: "x"
                from: -stackView.width * 0.3
                to: 0
                duration: 300
                easing.type: Easing.OutCubic
            }
        }

        popExit: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: 300
            }
            PropertyAnimation {
                property: "x"
                from: 0
                to: stackView.width
                duration: 300
                easing.type: Easing.OutCubic
            }
        }
    }
}