import QtQuick
import QtQuick.Window

Window {
    id: fileManagerWindow
    width: 980
    height: 680
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    color: "white"
    title: "LibreConnect - File Manager"

    signal windowClosed()

    FileManagerPage {
        anchors.fill: parent
        standaloneWindow: true
        onRequestClose: fileManagerWindow.close()
    }

    onClosing: function(close) {
        close.accepted = true
        windowClosed()
    }
}
