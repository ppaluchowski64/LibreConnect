import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
    required property var connectionController
    readonly property string windowTitleSuffix: "Paired Devices"

    property var pairedDevices: []
    property string selectedDeviceId: ""
    property string statusMessage: ""
    property bool removingDevice: false
    property bool isConnecting: false

    DeviceDiscovery {
        id: discovery
    }

    function reloadPairedDevices() {
        pairedDevices = connectionController.getPairedDevices()

        if (pairedDevices.length === 0) {
            selectedDeviceId = ""
            windowRef.showInitial()
            return
        }

        let hasSelectedDevice = false
        for (let i = 0; i < pairedDevices.length; ++i) {
            if (pairedDevices[i].deviceId === selectedDeviceId) {
                hasSelectedDevice = true
                break
            }
        }

        if (!selectedDeviceId || !hasSelectedDevice) {
            selectedDeviceId = pairedDevices[0].deviceId
        }
    }

    function connectSelectedDevice() {
        if (!selectedDeviceId || isConnecting || connectionController.pending || connectionController.connected)
            return

        const discoveredDevice = discovery.deviceById(selectedDeviceId)
        if (!discoveredDevice || !discoveredDevice.ipAddress || discoveredDevice.port <= 0) {
            statusMessage = "Selected device is currently offline. Open the mobile app and try again."
            return
        }

        statusMessage = ""
        isConnecting = true
        connectionController.connectTo(discoveredDevice.ipAddress, discoveredDevice.port, 1)
    }

    function removeSelectedDevice() {
        if (!selectedDeviceId || removingDevice)
            return

        removingDevice = true
        const removed = connectionController.removePairedDevice(selectedDeviceId)
        removingDevice = false

        if (!removed) {
            statusMessage = "Could not remove the selected paired device."
            return
        }

        statusMessage = ""
        reloadPairedDevices()
    }

    background: Rectangle {
        color: "white"
    }

    Image {
        id: logo
        source: "qrc:/LibreConnect/desktop/libreconnect_logo.png"
        width: 140
        height: 140
        fillMode: Image.PreserveAspectFit
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 24
    }

    Text {
        id: title
        text: "Paired Devices"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 24
        font.pixelSize: 36
        color: "black"
    }

    Text {
        text: "Select a paired device to connect, or remove devices you no longer use."
        anchors.left: title.left
        anchors.right: logo.left
        anchors.top: title.bottom
        anchors.topMargin: 8
        wrapMode: Text.WordWrap
        color: "#333333"
        font.pixelSize: 16
    }

    Item {
        id: contentArea
        anchors.top: title.bottom
        anchors.topMargin: 24
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24

        Item {
            id: contentRow
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            height: 292

            Rectangle {
                id: pairedListContainer
                x: 0
                y: 8
                width: contentRow.width - actionColumn.width - 20
                height: 250
                border.color: "#cccccc"
                border.width: 1
                color: "white"

                ListView {
                    id: pairedList
                    anchors.fill: parent
                    anchors.margins: 8
                    model: pairedDevices
                    clip: true

                    delegate: Rectangle {
                        id: rowItem
                        required property var modelData
                        property bool selected: root.selectedDeviceId === modelData.deviceId

                        width: pairedList.width
                        height: 80
                        radius: 4
                        color: selected ? "#eaf4ff" : "white"
                        border.color: selected ? "#2196f3" : "#e0e0e0"
                        border.width: selected ? 2 : 1

                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 12
                            spacing: 4

                            Text {
                                text: rowItem.modelData.deviceName
                                font.pixelSize: 18
                                font.bold: true
                                color: "#111111"
                            }

                            Text {
                                text: rowItem.modelData.deviceType + "  |  " + rowItem.modelData.deviceId
                                font.pixelSize: 12
                                color: "#555555"
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selectedDeviceId = rowItem.modelData.deviceId
                            onDoubleClicked: {
                                root.selectedDeviceId = rowItem.modelData.deviceId
                                root.connectSelectedDevice()
                            }
                        }
                    }
                }
            }

            Column {
                id: actionColumn
                x: pairedListContainer.width + 20
                width: 180
                spacing: 12
                anchors.bottom: pairedListContainer.bottom

                Button {
                    text: isConnecting || connectionController.pending ? "Connecting..." : "Connect"
                    width: parent.width
                    height: 48
                    enabled: root.selectedDeviceId.length > 0
                             && !isConnecting
                             && !connectionController.pending
                             && !connectionController.connected
                    onClicked: root.connectSelectedDevice()
                }

                Button {
                    text: removingDevice ? "Removing..." : "Remove"
                    width: parent.width
                    height: 48
                    enabled: root.selectedDeviceId.length > 0 && !removingDevice && !isConnecting
                    onClicked: root.removeSelectedDevice()
                }

                Button {
                    text: "New Pairing"
                    width: parent.width
                    height: 48
                    enabled: !isConnecting && !removingDevice
                    onClicked: windowRef.showInitial()
                }
            }

            Text {
                anchors.left: parent.left
                anchors.right: pairedListContainer.right
                anchors.top: pairedListContainer.bottom
                anchors.topMargin: 10
                text: statusMessage
                color: "#b00020"
                font.pixelSize: 13
                visible: statusMessage.length > 0
                wrapMode: Text.WordWrap
            }
        }
    }

    Connections {
        target: connectionController

        function onConnectedChanged() {
            if (connectionController.connected) {
                isConnecting = false
                discovery.cancelScan()
                windowRef.showHome()
            }
        }

        function onPendingChanged() {
            isConnecting = connectionController.pending
        }

        function onLastErrorChanged() {
            if (connectionController.lastError.length > 0) {
                statusMessage = connectionController.lastError
                isConnecting = false
            }
        }

        function onPairedDevicesChanged() {
            reloadPairedDevices()
        }
    }

    Component.onCompleted: {
        reloadPairedDevices()
        discovery.discover()
    }
}
