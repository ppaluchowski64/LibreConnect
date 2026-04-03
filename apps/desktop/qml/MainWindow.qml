import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import LibreConnect.desktop 1.0

Window {
    id: window
    visible: true
    width: 740
    height: 420
    minimumWidth: width
    minimumHeight: height
    maximumWidth: width
    maximumHeight: height
    color: "white"
    property string baseWindowTitle: "LibreConnect"
    property string currentWindowTitleSuffix: ""
    title: currentWindowTitleSuffix.length > 0
           ? baseWindowTitle + " - " + currentWindowTitleSuffix
           : baseWindowTitle

    DeviceConnectionController {
        id: connectionController
    }

    function updateWindowTitle() {
        const currentItem = stackView.currentItem
        currentWindowTitleSuffix = currentItem && currentItem.windowTitleSuffix !== undefined
                ? currentItem.windowTitleSuffix
                : ""
    }

    function replaceRootPage(url, properties) {
        if (stackView.depth === 0) {
            stackView.push(url, properties)
            return
        }

        if (stackView.depth > 1) {
            stackView.clear()
            stackView.push(url, properties)
            return
        }

        stackView.replace(url, properties)
    }

    function showInitial() {
        replaceRootPage("qrc:/LibreConnect/desktop/Initial.qml", {
            windowRef: window
        })
    }

    function showPairedDevices() {
        replaceRootPage("qrc:/LibreConnect/desktop/PairedDevices.qml", {
            windowRef: window,
            connectionController: connectionController
        })
    }

    function showDevicePicker() {
        replaceRootPage("qrc:/LibreConnect/desktop/DevicePicker.qml", {
            windowRef: window,
            connectionController: connectionController
        })
    }

    function showHome() {
        replaceRootPage("qrc:/LibreConnect/desktop/Home.qml", {
            windowRef: window,
            connectionController: connectionController
        })
    }

    function showFileManager() {
        stackView.push("qrc:/LibreConnect/desktop/FileManagerPage.qml", {
            windowRef: window
        })
    }

    function showSettings() {
        stackView.push("qrc:/LibreConnect/desktop/SettingsPage.qml", {
            windowRef: window
        })
    }

    function showVirtualCamera() {
        stackView.push("qrc:/LibreConnect/desktop/VirtualCameraPage.qml", {
            windowRef: window
        })
    }

    function goBack() {
        if (stackView.depth > 1)
            stackView.pop()
    }

    StackView {
        id: stackView
        anchors.fill: parent

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

        replaceEnter: Transition {
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

        replaceExit: Transition {
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

        onCurrentItemChanged: window.updateWindowTitle()
    }

    Connections {
        target: connectionController

        function onConnectedChanged() {
            if (!connectionController.connected && stackView.depth > 0) {
                connectionController.refreshPairedDevices()
                if (connectionController.hasPairedDevices) {
                    showPairedDevices()
                } else {
                    showInitial()
                }
            }
        }
    }

    Component.onCompleted: {
        connectionController.refreshPairedDevices()
        if (connectionController.hasPairedDevices) {
            showPairedDevices()
        } else {
            showInitial()
        }
    }
}
