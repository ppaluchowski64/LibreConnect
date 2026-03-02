import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: devicePicker
    DeviceDiscovery {
        id: discovery
    }
    DeviceConnectionController {
        id: conn
    }
    property bool isConnecting: false
    function connectToCurrent() {
        if (deviceListView.currentIndex < 0 || isConnecting)
            return

        const dev = discovery.deviceAt(deviceListView.currentIndex)

        conn.connectTo(dev.ipAddress, dev.port, 0)

        isConnecting = true
    }

    Image {
        id: logo
        source: "libreconnect_logo.png"
        width: 140
        height: 140
        fillMode: Image.PreserveAspectFit
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 24
    }

    Rectangle {
        id: deviceListContainer
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 20
        width: 400
        border.color: "#cccccc"
        border.width: 1
        color: "white"

        ListView {
            id: deviceListView
            anchors.fill: parent
            clip: true
            focus: true

            model: discovery.model

            highlight: Rectangle {
                color: "#e3f2fd"
                border.color: "#2196f3"
                border.width: 1
                radius: 2
                visible: deviceListView.currentIndex >= 0
            }
            highlightFollowsCurrentItem: true
            highlightMoveDuration: 120

            delegate: Rectangle {
                id: tile
                width: deviceListView.width
                height: 100
                radius: 4
                property bool pressed: false

                color: pressed ? "#d7eafc" : (ListView.isCurrentItem ? "#eaf4ff" : "white")
                border.color: ListView.isCurrentItem ? "#2196f3" : "#e0e0e0"
                border.width: ListView.isCurrentItem ? 2 : 1

                Row {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Image {
                        id: deviceIcon
                        source: icon
                        width: 48
                        height: 48
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        Text {
                            text: deviceName
                            font.pixelSize: 18
                            font.bold: true
                            color: "#111111"
                        }

                        Text {
                            text: "IP: " + ipAddress + ", " + osName + " " + osVersion
                            font.pixelSize: 14
                            color: "#555555"
                        }

                        Text {
                            text: "App version " + appVersion
                            font.pixelSize: 14
                            color: "#555555"
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: deviceListView.currentIndex = index
                    onDoubleClicked: {
                        deviceListView.currentIndex = index
                        devicePicker.connectToCurrent()
                    }
                }
            }

            footer: Text {
                width: deviceListView.width
                topPadding: 20
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 16
                color: "#666666"
                visible: deviceListView.count === 0
                text: discovery.searching ? "Searching for devices..." : "No devices found"
            }

            Keys.onReturnPressed: devicePicker.connectToCurrent()
            Keys.onEnterPressed: devicePicker.connectToCurrent()
            Keys.onEscapePressed: deviceListView.currentIndex = -1
        }
    }

    Column {
        id: buttonColumn
        anchors.top: deviceListContainer.top
        anchors.right: logo.left
        anchors.rightMargin: 20
        spacing: 12

        Button {
            id: connectButton
            text: isConnecting ? "Connecting…" : "Connect"
            width: 120
            height: 48
            font.pixelSize: 16
            enabled: deviceListView.currentIndex >= 0 && !conn.pending && !conn.connected

            onClicked: devicePicker.connectToCurrent()
        }

        Button {
            id: refreshButton
            text: "Refresh"
            width: 120
            height: 48
            font.pixelSize: 16
            enabled: !isConnecting
            onClicked: discovery.discover()
        }
    }
    Column {
        id: helpText
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        anchors.topMargin: 100
        anchors.top: logo.bottom
        width: 280

        Text {
            text: "If your phone isn't appearing:"
            font.pixelSize: 16
            font.bold: true
            color: "#111111"
            width: parent.width
        }
        Column {
            width: parent.width
            Row {
                spacing: 6
                width: parent.width
                Text {
                    text: "• Make sure both of your devices are on the same network"
                    font.pixelSize: 14
                    color: "#333333"
                    wrapMode: Text.WordWrap
                    width: parent.width - 20
                }
            }
            Row {
                spacing: 6
                width: parent.width
                Text {
                    text: "• Make sure the app is in the foreground"
                    font.pixelSize: 14
                    color: "#333333"
                    wrapMode: Text.WordWrap
                    width: parent.width - 20
                }
            }
        }
    }
    Connections {
        target: conn
        function onConnectedChanged() {
            if (conn.connected) {
                isConnecting = false
                discovery.cancelScan()
                devicePicker.StackView.view.push("qrc:/LibreConnect/desktop/Connected.qml")
            }
        }
        function onLastErrorChanged() {
            if (conn.lastError.length > 0) {
                isConnecting = false
            }
        }

        function onPendingChanged() {
            isConnecting = conn.pending
        }
    }

    Component.onCompleted: {
        discovery.discover();
        console.log("DevicePicker loaded OK");
    }
}
