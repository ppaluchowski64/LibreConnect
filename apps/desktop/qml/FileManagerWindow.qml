import QtQuick
import QtQuick.Window
import LibreConnect.desktop 1.0

Window {
    id: fileManagerWindow
    required property var connectionController
    width: 980
    height: 680
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    color: Theme.backgroundColor
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

    Connections {
        target: connectionController

        function onConnectedChanged() {
            if (!connectionController.connected && fileManagerWindow.visible) {
                fileManagerWindow.close()
            }
        }
    }
}
