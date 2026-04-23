import QtQuick
import LibreConnect.desktop 1.0

Rectangle {
    id: control

    property string text: ""
    property string iconSource: ""
    property string darkIconSource: ""
    property int iconSize: 20
    signal clicked()

    readonly property string resolvedIconSource: {
        if (Theme.dark && darkIconSource.length > 0)
            return darkIconSource
        return iconSource
    }
    readonly property bool hasIcon: resolvedIconSource.length > 0

    radius: 8
    color: {
        if (!control.enabled)
            return Qt.darker(Theme.buttonColor, 1.15)
        if (buttonMouse.pressed)
            return Qt.darker(Theme.buttonColor, 1.12)
        if (buttonMouse.containsMouse)
            return Qt.lighter(Theme.buttonColor, 1.08)
        return Theme.buttonColor
    }
    border.color: Theme.panelBorderColor
    border.width: 1

    Row {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 8

        Image {
            id: iconImage
            anchors.verticalCenter: parent.verticalCenter
            visible: control.hasIcon
            width: control.iconSize
            height: control.iconSize
            sourceSize.width: control.iconSize
            sourceSize.height: control.iconSize
            fillMode: Image.PreserveAspectFit
            smooth: true
            source: control.resolvedIconSource
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - (iconImage.visible ? iconImage.width + parent.spacing : 0)
            text: control.text
            font.family: Theme.fontFamily
            font.pixelSize: 14
            color: Theme.textColor
            horizontalAlignment: iconImage.visible ? Text.AlignLeft : Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: buttonMouse
        anchors.fill: parent
        enabled: control.enabled
        hoverEnabled: true
        onClicked: control.clicked()
    }
}
