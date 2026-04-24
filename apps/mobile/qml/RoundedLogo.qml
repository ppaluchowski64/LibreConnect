import QtQuick
import QtQuick.Effects

Item {
    id: root

    property url source: "qrc:/LibreConnect/mobile/libreconnect_logo_1024.png"
    property real cornerRadius: width * 0.22
    property int sourcePixelSize: 1024

    implicitWidth: 96
    implicitHeight: 96

    Image {
        id: logoSource
        anchors.fill: parent
        source: root.source
        sourceSize.width: root.sourcePixelSize
        sourceSize.height: root.sourcePixelSize
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        visible: false
        layer.enabled: true
    }

    Rectangle {
        id: logoMask
        anchors.fill: parent
        radius: root.cornerRadius
        color: "white"
        antialiasing: true
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        source: logoSource
        visible: logoSource.status === Image.Ready
        maskEnabled: true
        maskSource: logoMask
    }

    Image {
        anchors.fill: parent
        source: root.source
        sourceSize.width: root.sourcePixelSize
        sourceSize.height: root.sourcePixelSize
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        visible: logoSource.status !== Image.Ready
    }
}
