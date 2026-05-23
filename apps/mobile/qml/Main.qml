import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
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
    readonly property int homeMenuIndex: 0
    readonly property int settingsMenuIndex: 1
    property int currentMainMenuIndex: homeMenuIndex

    readonly property url initialPageUrl: Qt.resolvedUrl("InitialPage.qml")
    readonly property url pairedDevicesPageUrl: Qt.resolvedUrl("PairedDevicesPage.qml")
    readonly property url permissionsPageUrl: Qt.resolvedUrl("PermissionsPage.qml")
    readonly property url homePageUrl: Qt.resolvedUrl("ConnectedHomePage.qml")
    readonly property url settingsPageUrl: Qt.resolvedUrl("SettingsPage.qml")
    readonly property url remoteInputPageUrl: Qt.resolvedUrl("RemoteInputPage.qml")
    readonly property url remoteKeyboardPageUrl: Qt.resolvedUrl("RemoteKeyboardPage.qml")
    readonly property url presenterModePageUrl: Qt.resolvedUrl("PresenterModePage.qml")

    function homePageProperties() {
        return {
            conn: conn,
            clipboardSyncController: clipboardSyncController,
            remoteInputController: remoteInputController,
            showRemoteInputCallback: function() { root.showRemoteInputPage() },
            showPresenterModeCallback: function() { root.showPresenterModePage() },
            showRemoteKeyboardCallback: function() { root.showRemoteKeyboardPage() },
            showSettingsCallback: function() { root.showSettingsPage() }
        }
    }

    function themedIcon(baseName) {
        return Theme.dark
                ? ("qrc:/LibreConnect/mobile/" + baseName + "_dark.svg")
                : ("qrc:/LibreConnect/mobile/" + baseName + ".svg")
    }

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
        currentMainMenuIndex = homeMenuIndex
        showRootPage(mainMenusPageComponent, { initialMenuIndex: homeMenuIndex })
    }

    function showHomePageFromPermissions() {
        currentMainMenuIndex = homeMenuIndex
        stackView.replace(mainMenusPageComponent, { initialMenuIndex: homeMenuIndex })
    }

    function showHomePageFromSettings() {
        showMainMenuPage(homeMenuIndex)
    }

    function showMainMenuPage(menuIndex) {
        if (stackView.busy)
            return

        if (stackView.currentItem && stackView.currentItem.isMainMenusPage) {
            stackView.currentItem.currentMenuIndex = menuIndex
            return
        }

        currentMainMenuIndex = menuIndex
        showRootPage(mainMenusPageComponent, { initialMenuIndex: menuIndex })
    }

    function settingsPageProperties() {
        return {
            conn: conn,
            notificationSyncController: notificationSyncController,
            clipboardSyncController: clipboardSyncController,
            themeModes: themeModes,
            showHomeCallback: function() { root.showHomePageFromSettings() }
        }
    }

    function showPairedDevicesPage() {
        conn.refreshPairedDevices()
        stackView.push(pairedDevicesPageUrl, {
            conn: conn,
            goBackCallback: function() { root.popPage() }
        })
    }

    function showSettingsPage() {
        showMainMenuPage(settingsMenuIndex)
    }

    function showRemoteInputPage() {
        stackView.push(remoteInputPageUrl, {
            conn: conn,
            remoteInputController: remoteInputController,
            goBackCallback: function() { root.popPage() }
        })
    }

    function showRemoteKeyboardPage() {
        stackView.push(remoteKeyboardPageUrl, {
            conn: conn,
            remoteInputController: remoteInputController,
            goBackCallback: function() { root.popPage() }
        })
    }

    function showPresenterModePage() {
        stackView.push(presenterModePageUrl, {
            conn: conn,
            remoteInputController: remoteInputController,
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

    MobileRemoteInputController {
        id: remoteInputController
    }

    MobileMediaNotificationController {
        id: mediaNotificationController
    }

    Component {
        id: mainMenusPageComponent

        Page {
            id: mainMenusPage

            required property int initialMenuIndex
            readonly property bool isMainMenusPage: true
            property alias currentMenuIndex: mainMenusSwipeView.currentIndex

            background: Rectangle {
                color: Theme.backgroundColor
            }

            component BottomNavButton: Button {
                id: navButton
                required property string labelText
                required property string iconBase
                property bool selected: false

                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: 60
                checkable: false
                hoverEnabled: true
                padding: 8

                background: Rectangle {
                    radius: 14
                    color: navButton.selected
                           ? "transparent"
                           : (navButton.pressed
                              ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.14) : Qt.darker(Theme.buttonColor, 1.05))
                              : "transparent")
                    border.width: 1
                    border.color: navButton.pressed ? Theme.panelBorderColor : "transparent"

                    Behavior on color {
                        ColorAnimation {
                            duration: 140
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                contentItem: Item {
                    anchors.fill: parent

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - (navButton.padding * 2)
                        spacing: 2

                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: root.themedIcon(navButton.iconBase)
                            sourceSize.width: 24
                            sourceSize.height: 24
                            width: 24
                            height: 24
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            scale: navButton.selected ? 1.08 : 1.0

                            Behavior on scale {
                                NumberAnimation {
                                    duration: 180
                                    easing.type: Easing.OutBack
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            text: navButton.labelText
                            color: navButton.selected ? Theme.selectedTextColor : Theme.textColor
                            font.pixelSize: 13
                            font.bold: navButton.selected
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight

                            Behavior on color {
                                ColorAnimation {
                                    duration: 160
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }
                    }
                }
            }

            SwipeView {
                id: mainMenusSwipeView
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: bottomNavigation.top
                interactive: true
                clip: true
                currentIndex: mainMenusPage.initialMenuIndex

                onCurrentIndexChanged: root.currentMainMenuIndex = currentIndex

                Component.onCompleted: {
                    currentIndex = mainMenusPage.initialMenuIndex
                    root.currentMainMenuIndex = currentIndex
                }

                ConnectedHomePage {
                    conn: root.homePageProperties().conn
                    clipboardSyncController: root.homePageProperties().clipboardSyncController
                    remoteInputController: root.homePageProperties().remoteInputController
                    showRemoteInputCallback: root.homePageProperties().showRemoteInputCallback
                    showPresenterModeCallback: root.homePageProperties().showPresenterModeCallback
                    showRemoteKeyboardCallback: root.homePageProperties().showRemoteKeyboardCallback
                    showSettingsCallback: function() { mainMenusSwipeView.currentIndex = root.settingsMenuIndex }
                    showBottomNavigation: false
                }

                SettingsPage {
                    conn: root.settingsPageProperties().conn
                    notificationSyncController: root.settingsPageProperties().notificationSyncController
                    clipboardSyncController: root.settingsPageProperties().clipboardSyncController
                    themeModes: root.settingsPageProperties().themeModes
                    showHomeCallback: function() { mainMenusSwipeView.currentIndex = root.homeMenuIndex }
                    showBottomNavigation: false
                }
            }

            Frame {
                id: bottomNavigation
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 80
                padding: 10
                Material.elevation: 4

                background: Rectangle {
                    radius: 16
                    color: Theme.panelColor
                    border.width: 1
                    border.color: Theme.panelBorderColor
                }

                contentItem: Item {
                    readonly property real navSpacing: 10
                    readonly property real selectedIndicatorWidth: (width - navSpacing) / 2
                    readonly property real selectedIndicatorX: mainMenusSwipeView.currentIndex * (selectedIndicatorWidth + navSpacing)

                    Rectangle {
                        x: parent.selectedIndicatorX
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.selectedIndicatorWidth
                        height: parent.height
                        radius: 14
                        color: Theme.selectedColor
                        border.width: 2
                        border.color: Theme.accentColor

                        Behavior on x {
                            NumberAnimation {
                                duration: 220
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        spacing: parent.navSpacing

                        BottomNavButton {
                            labelText: "Home"
                            iconBase: "home"
                            selected: mainMenusSwipeView.currentIndex === root.homeMenuIndex
                            onClicked: mainMenusSwipeView.currentIndex = root.homeMenuIndex
                        }

                        BottomNavButton {
                            labelText: "Settings"
                            iconBase: "settings"
                            selected: mainMenusSwipeView.currentIndex === root.settingsMenuIndex
                            onClicked: mainMenusSwipeView.currentIndex = root.settingsMenuIndex
                        }
                    }
                }
            }
        }
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

        replaceEnter: Transition {}

        replaceExit: Transition {}
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

    Dialog {
        id: approvalDialog
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(root.width - 24, 360)
        closePolicy: Popup.NoAutoClose
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
            spacing: 14
            width: approvalDialog.availableWidth

            Text {
                width: parent.width
                text: "Approve Connection"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: conn.pendingDeviceName.length > 0
                      ? ("Allow " + conn.pendingDeviceName + " to finish signing in?")
                      : "Allow this device to finish signing in?"
                color: Theme.textColor
                font.pixelSize: 16
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "Only accept if you just entered the code on your desktop."
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            RowLayout {
                width: parent.width
                spacing: 10

                Button {
                    id: denyApprovalButton
                    Layout.fillWidth: true
                    text: "Deny"
                    Material.elevation: 0
                    onClicked: conn.denyConnectionApproval()
                    contentItem: Text {
                        text: denyApprovalButton.text
                        color: Theme.textColor
                        font.pixelSize: 15
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 12
                        color: denyApprovalButton.pressed
                               ? Qt.darker(Theme.buttonColor, 1.14)
                               : (denyApprovalButton.hovered ? Qt.lighter(Theme.buttonColor, 1.08) : Theme.buttonColor)
                        border.width: 1
                        border.color: Theme.panelBorderColor
                    }
                }

                Button {
                    id: acceptApprovalButton
                    Layout.fillWidth: true
                    text: "Accept"
                    Material.elevation: 0
                    onClicked: conn.acceptConnectionApproval()
                    contentItem: Text {
                        text: acceptApprovalButton.text
                        color: Theme.selectedTextColor
                        font.pixelSize: 15
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 12
                        color: acceptApprovalButton.pressed
                               ? Qt.darker(Theme.accentColor, 1.12)
                               : (acceptApprovalButton.hovered ? Qt.lighter(Theme.accentColor, 1.08) : Theme.accentColor)
                        border.width: 1
                        border.color: Theme.accentColor
                    }
                }
            }
        }
    }

    Dialog {
        id: findMyPhoneDialog
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(root.width - 24, 360)
        closePolicy: Popup.NoAutoClose
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
            width: findMyPhoneDialog.availableWidth

            Text {
                width: parent.width
                text: "Find My Phone"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "Your phone is ringing. Tap OK to stop."
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Button {
                width: parent.width
                text: "OK"
                Material.elevation: 1
                onClicked: conn.stopFindMyPhoneAlert()
            }
        }
    }

    onClosing: function(close) {
        if (challengeDialog.visible) {
            challengeDialog.close()
            close.accepted = false
            return
        }

        if (approvalDialog.visible) {
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

        function onApprovalVisibleChanged() {
            if (conn.approvalVisible && !approvalDialog.visible) {
                approvalDialog.open()
                return
            }

            if (!conn.approvalVisible && approvalDialog.visible) {
                approvalDialog.close()
            }
        }

        function onFindMyPhoneAlertActiveChanged() {
            if (conn.findMyPhoneAlertActive) {
                if (!findMyPhoneDialog.visible)
                    findMyPhoneDialog.open()
                return
            }

            if (findMyPhoneDialog.visible)
                findMyPhoneDialog.close()
        }
    }

    Component.onCompleted: {
        advertiser.start()
        conn.refreshLocalIdentity()
        conn.refreshPairedDevices()
        conn.refreshPermissionStatuses()
        root.updateConnectedRoute()
        if (conn.findMyPhoneAlertActive)
            findMyPhoneDialog.open()
    }
}
