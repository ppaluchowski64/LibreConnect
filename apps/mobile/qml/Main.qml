import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import LibreConnect.mobile 1.0

ApplicationWindow {
    id: root
    width: 420
    height: 760
    visible: true
    title: "LibreConnect Mobile"
    color: Theme.backgroundColor
    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.accent: Theme.accentColor
    Material.primary: Theme.panelColor

    property var themeModes: [
        { label: "System", value: "system" },
        { label: "Light", value: "light" },
        { label: "Dark", value: "dark" }
    ]
    readonly property int navAnimationDuration: 220

    readonly property url initialPageUrl: Qt.resolvedUrl("InitialPage.qml")
    readonly property url pairedDevicesPageUrl: Qt.resolvedUrl("PairedDevicesPage.qml")
    readonly property url permissionsPageUrl: Qt.resolvedUrl("PermissionsPage.qml")
    readonly property url homePageUrl: Qt.resolvedUrl("ConnectedHomePage.qml")
    readonly property url settingsPageUrl: Qt.resolvedUrl("SettingsPage.qml")

    function showRootPage(pageUrl, properties) {
        stackView.clear()
        stackView.push(pageUrl, properties || {})
    }

    function showInitialPage() {
        showRootPage(initialPageUrl, {
            conn: conn,
            showPairedDevicesCallback: function() { root.showPairedDevicesPage() }
        })
    }

    function showPermissionsPage() {
        showRootPage(permissionsPageUrl, {
            conn: conn,
            continueToHomeCallback: function() { root.showHomePageFromPermissions() }
        })
    }

    function showHomePage() {
        showRootPage(homePageUrl, {
            conn: conn,
            clipboardSyncController: clipboardSyncController,
            showSettingsCallback: function() { root.showSettingsPage() }
        })
    }

    function showHomePageFromPermissions() {
        stackView.replace(homePageUrl, {
            conn: conn,
            clipboardSyncController: clipboardSyncController,
            showSettingsCallback: function() { root.showSettingsPage() }
        })
    }

    function showPairedDevicesPage() {
        conn.refreshPairedDevices()
        stackView.push(pairedDevicesPageUrl, {
            conn: conn,
            goBackCallback: function() { root.popPage() }
        })
    }

    function showSettingsPage() {
        stackView.push(settingsPageUrl, {
            conn: conn,
            notificationSyncController: notificationSyncController,
            clipboardSyncController: clipboardSyncController,
            themeModes: themeModes,
            goBackCallback: function() { root.popPage() }
        })
    }

    function popPage() {
        if (stackView.depth > 1) {
            stackView.pop()
        }
    }

    function updateConnectedRoute() {
        if (conn.connected) {
            conn.refreshPermissionStatuses()
            if (conn.permissionsOnboardingRequired) {
                showPermissionsPage()
            } else {
                showHomePage()
            }
        } else {
            showInitialPage()
        }
    }

    AndroidAdvertiser {
        id: advertiser
    }

    MobileConnectionController {
        id: conn
    }

    MobileNotificationSyncController {
        id: notificationSyncController
    }

    MobileClipboardSyncController {
        id: clipboardSyncController
    }

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: Item {}

        pushEnter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "x"
                    from: stackView.width * 0.28
                    to: 0
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
            }
        }

        pushExit: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "x"
                    from: 0
                    to: -stackView.width * 0.1
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
            }
        }

        popEnter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "x"
                    from: -stackView.width * 0.1
                    to: 0
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
            }
        }

        popExit: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "x"
                    from: 0
                    to: stackView.width * 0.25
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
            }
        }

        replaceEnter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "x"
                    from: stackView.width * 0.28
                    to: 0
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
            }
        }

        replaceExit: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "x"
                    from: 0
                    to: -stackView.width * 0.1
                    duration: root.navAnimationDuration
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    Dialog {
        id: challengeDialog
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(root.width - 24, 360)
        closePolicy: Popup.CloseOnEscape
        padding: 16
        background: Rectangle {
            radius: 24
            color: Theme.panelColor
            border.width: 1
            border.color: Theme.panelBorderColor
        }
        Overlay.modal: Rectangle {
            color: Theme.dark ? "#99000000" : "#73000000"
        }

        contentItem: Column {
            spacing: 12
            width: challengeDialog.availableWidth

            Text {
                width: parent.width
                text: "Verify Connection"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: conn.pendingDeviceName.length > 0
                      ? ("Connection request from " + conn.pendingDeviceName)
                      : "Connection request"
                color: Theme.textColor
                font.pixelSize: 16
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "Enter this code on the desktop app:"
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: conn.challengeCode
                color: Theme.textColor
                font.pixelSize: 30
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    onClosing: function(close) {
        if (challengeDialog.visible) {
            challengeDialog.close()
            close.accepted = false
            return
        }

        if (stackView.depth > 1) {
            root.popPage()
            close.accepted = false
        }
    }

    Connections {
        target: conn

        function onConnectedChanged() {
            root.updateConnectedRoute()
        }

        function onChallengeVisibleChanged() {
            if (conn.challengeVisible && !challengeDialog.visible) {
                challengeDialog.open()
                return
            }

            if (!conn.challengeVisible && challengeDialog.visible) {
                challengeDialog.close()
            }
        }
    }

    Component.onCompleted: {
        advertiser.start()
        conn.refreshLocalIdentity()
        conn.refreshPairedDevices()
        conn.refreshPermissionStatuses()
        root.updateConnectedRoute()
    }
}
