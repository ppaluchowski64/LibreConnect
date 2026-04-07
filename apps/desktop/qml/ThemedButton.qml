import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Button {
    id: control

    font.family: Theme.fontFamily

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 8
        color: control.enabled ? Theme.buttonColor : Qt.darker(Theme.buttonColor, 1.15)
        border.color: Theme.panelBorderColor
        border.width: 1
    }
}
