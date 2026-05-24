import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root
    clip: true
    readonly property bool compactControls: width < 760

    property var selectedRemotePaths: []
    property int lastInteractedIndex: -1
    property var pathSegments: []
    property bool dropActive: false
    property bool pathEditMode: false
    property string sortColumn: "modified"
    property bool sortAscending: false
    property var sortedRemoteEntries: sortEntries(fileManagerController.remoteEntries)
    readonly property int selectedCount: selectedRemotePaths.length
    readonly property string windowTitleSuffix: "File Manager"

    readonly property color destructiveFill: Theme.destructiveFillColor
    readonly property color destructiveFillHover: Theme.destructiveFillHoverColor
    readonly property color destructiveText: Theme.dangerColor

    FileManagerController {
        id: fileManagerController
        onCurrentRemotePathChanged: root.rebuildPathSegments()
        onRemoteEntriesChanged: root.pruneSelectionToVisibleEntries()
    }

    function rebuildPathSegments() {
        const currentPath = fileManagerController.currentRemotePath
        const segments = []
        const normalized = currentPath && currentPath.length > 0 ? currentPath : "/"

        if (normalized === "/") {
            segments.push({ "label": "/", "path": "/" })
            pathSegments = segments
            return
        }

        const parts = normalized.split("/")
        let runningPath = ""

        if (normalized.charAt(0) === "/") {
            segments.push({ "label": "/", "path": "/" })
            runningPath = ""
        }

        for (let i = 0; i < parts.length; ++i) {
            const part = parts[i]
            if (!part || part.length === 0)
                continue

            runningPath += "/" + part
            segments.push({ "label": part, "path": runningPath })
        }

        pathSegments = segments
    }

    function isPathSelected(path) {
        return selectedRemotePaths.indexOf(path) >= 0
    }

    function clearSelection() {
        selectedRemotePaths = []
        lastInteractedIndex = -1
    }

    function setSingleSelection(path, index) {
        selectedRemotePaths = path && path.length > 0 ? [path] : []
        lastInteractedIndex = index
    }

    function toggleSelection(path, index) {
        if (!path || path.length === 0)
            return

        const next = selectedRemotePaths.slice()
        const existingIndex = next.indexOf(path)
        if (existingIndex >= 0)
            next.splice(existingIndex, 1)
        else
            next.push(path)

        selectedRemotePaths = next
        lastInteractedIndex = index
    }

    function selectRange(index) {
        if (index < 0 || index >= sortedRemoteEntries.length)
            return

        if (lastInteractedIndex < 0 || lastInteractedIndex >= sortedRemoteEntries.length) {
            const item = sortedRemoteEntries[index]
            setSingleSelection(item.path, index)
            return
        }

        const first = Math.min(lastInteractedIndex, index)
        const last = Math.max(lastInteractedIndex, index)
        const next = []

        for (let i = first; i <= last; ++i) {
            const entry = sortedRemoteEntries[i]
            if (entry && entry.path)
                next.push(entry.path)
        }

        selectedRemotePaths = next
    }

    function selectionForContext(targetPath, index) {
        if (isPathSelected(targetPath))
            return

        setSingleSelection(targetPath, index)
    }

    function pruneSelectionToVisibleEntries() {
        const visible = {}
        for (let i = 0; i < sortedRemoteEntries.length; ++i) {
            const entry = sortedRemoteEntries[i]
            visible[entry.path] = true
        }

        const filtered = []
        for (let i = 0; i < selectedRemotePaths.length; ++i) {
            const path = selectedRemotePaths[i]
            if (visible[path])
                filtered.push(path)
        }

        if (filtered.length !== selectedRemotePaths.length)
            selectedRemotePaths = filtered

        if (selectedRemotePaths.length === 0)
            lastInteractedIndex = -1
    }

    function entryByPath(path) {
        for (let i = 0; i < sortedRemoteEntries.length; ++i) {
            const entry = sortedRemoteEntries[i]
            if (entry.path === path)
                return entry
        }

        return null
    }

    function uploadLocalUrls(urls) {
        if (!urls || urls.length === 0)
            return

        for (let i = 0; i < urls.length; ++i)
            fileManagerController.uploadLocalEntry(urls[i])
    }

    function beginExternalDrag(targetPath, index) {
        if (!targetPath || targetPath.length === 0)
            return

        if (!root.isPathSelected(targetPath))
            root.setSingleSelection(targetPath, index)

        const paths = root.selectedRemotePaths.length > 0
                    ? root.selectedRemotePaths.slice()
                    : [targetPath]
        fileManagerController.beginExternalDrag(paths)
    }

    function compareEntryValues(lhs, rhs, column) {
        if (column === "size")
            return (lhs.size || 0) - (rhs.size || 0)

        if (column === "modified") {
            const lhsKnown = lhs.modifiedKnown ? 1 : 0
            const rhsKnown = rhs.modifiedKnown ? 1 : 0
            if (lhsKnown !== rhsKnown)
                return lhsKnown - rhsKnown
            return (lhs.modified || 0) - (rhs.modified || 0)
        }

        const lhsName = String(lhs.name || "").toLocaleLowerCase()
        const rhsName = String(rhs.name || "").toLocaleLowerCase()
        if (lhsName < rhsName)
            return -1
        if (lhsName > rhsName)
            return 1
        return String(lhs.name || "").localeCompare(String(rhs.name || ""))
    }

    function sortEntries(entries) {
        const sorted = entries ? entries.slice() : []
        sorted.sort(function(lhs, rhs) {
            if (lhs.isDirectory !== rhs.isDirectory)
                return lhs.isDirectory ? -1 : 1

            let result = compareEntryValues(lhs, rhs, sortColumn)
            if (result === 0 && sortColumn !== "name")
                result = compareEntryValues(lhs, rhs, "name")

            return sortAscending ? result : -result
        })

        return sorted
    }

    function setSortColumn(column) {
        lastInteractedIndex = -1

        if (sortColumn === column) {
            sortAscending = !sortAscending
            return
        }

        sortColumn = column
        sortAscending = column === "name"
    }

    function sortIndicator(column) {
        if (sortColumn !== column)
            return ""

        return sortAscending ? " ^" : " v"
    }

    function sizeColumnWidth(totalWidth) {
        return 104
    }

    function modifiedColumnWidth(totalWidth) {
        return 148
    }

    function iconColumnWidth(totalWidth) {
        return 24
    }

    function nameColumnWidth(totalWidth) {
        const iconWidth = iconColumnWidth(totalWidth)
        const sizeWidth = sizeColumnWidth(totalWidth)
        const modifiedWidth = modifiedColumnWidth(totalWidth)

        return Math.max(0, totalWidth - iconWidth - (3 * 12) - sizeWidth - modifiedWidth)
    }

    function entryIconSource(entry) {
        if (!entry || !entry.path)
            return ""

        const fetchedSource = fileManagerController.iconSources[entry.path]
        return fetchedSource && fetchedSource.length > 0 ? fetchedSource : entry.iconSource
    }

    function beginPathEdit() {
        pathEditMode = true
        pathEditFieldCompact.text = fileManagerController.currentRemotePath
        pathEditFieldWide.text = fileManagerController.currentRemotePath
        if (root.compactControls) {
            pathEditFieldCompact.forceActiveFocus()
            pathEditFieldCompact.selectAll()
        } else {
            pathEditFieldWide.forceActiveFocus()
            pathEditFieldWide.selectAll()
        }
    }

    function commitPathEdit() {
        const nextPath = root.compactControls ? pathEditFieldCompact.text : pathEditFieldWide.text
        pathEditMode = false
        fileManagerController.browseTo(nextPath)
    }

    function cancelPathEdit() {
        pathEditMode = false
        pathEditFieldCompact.text = fileManagerController.currentRemotePath
        pathEditFieldWide.text = fileManagerController.currentRemotePath
    }

    background: Rectangle {
        color: "transparent"
    }

    FolderDialog {
        id: folderDialog
        title: "Choose download folder"
        onAccepted: {
            if (selectedFolder)
                fileManagerController.setLocalDownloadDirectory(selectedFolder)
        }
    }

    FileDialog {
        id: uploadDialog
        title: "Select files to upload"
        fileMode: FileDialog.OpenFiles
        onAccepted: root.uploadLocalUrls(selectedFiles)
    }

    FolderDialog {
        id: uploadFolderDialog
        title: "Select folder to upload"
        onAccepted: {
            if (selectedFolder)
                fileManagerController.uploadLocalEntry(selectedFolder)
        }
    }

    Dialog {
        id: deleteConfirmDialog
        title: ""
        modal: true
        closePolicy: Popup.CloseOnEscape
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        contentItem: Column {
            spacing: 12
            width: 360

            Text {
                text: fileContextMenu.targetPaths.length > 1
                      ? "Delete " + fileContextMenu.targetPaths.length + " entries?"
                      : "Delete " + (root.entryByPath(fileContextMenu.targetPath) ? root.entryByPath(fileContextMenu.targetPath).name : "entry") + "?"
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                color: Theme.textColor
                width: parent.width
                wrapMode: Text.WordWrap
            }

            Text {
                text: "This action cannot be undone. The selected files and folders will be permanently removed from the connected device."
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 15
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10

                ThemedButton {
                    text: "Cancel"
                    width: 160
                    height: 42
                    font.pixelSize: 14
                    onClicked: deleteConfirmDialog.close()
                }

                Rectangle {
                    width: 160
                    height: 42
                    radius: 8
                    color: deleteConfirmMouse.containsMouse ? root.destructiveFillHover : root.destructiveFill
                    border.color: Theme.panelBorderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Delete"
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        color: root.destructiveText
                    }

                    MouseArea {
                        id: deleteConfirmMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            deleteConfirmDialog.close()
                            fileManagerController.deleteEntries(fileContextMenu.targetPaths)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: fileContextMenu
        property var targetPaths: []
        property string targetPath: ""
        property bool singleTargetIsDirectory: false
        parent: root
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 6
        width: 220

        function popup(xPos, yPos) {
            x = Math.max(8, Math.min(xPos, root.width - width - 8))
            y = Math.max(8, Math.min(yPos, root.height - implicitHeight - 8))
            open()
        }

        background: Rectangle {
            radius: 8
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        contentItem: Column {
            spacing: 4

            Rectangle {
                id: openRow
                readonly property bool enabledItem: fileContextMenu.targetPaths.length === 1
                width: parent.width
                height: 34
                radius: 6
                color: openMouse.containsMouse && openRow.enabledItem ? Theme.buttonColor : "transparent"

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    text: fileContextMenu.singleTargetIsDirectory ? "Open Folder" : "Open"
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    color: openRow.enabledItem ? Theme.textColor : Theme.subtleTextColor
                    verticalAlignment: Text.AlignVCenter
                }

                MouseArea {
                    id: openMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: openRow.enabledItem
                    onClicked: {
                        fileContextMenu.close()
                        if (fileContextMenu.singleTargetIsDirectory)
                            fileManagerController.browseTo(fileContextMenu.targetPath)
                        else
                            fileManagerController.openEntry(fileContextMenu.targetPath)
                    }
                }
            }

            Rectangle {
                id: copyRow
                readonly property bool enabledItem: fileContextMenu.targetPaths.length > 0
                width: parent.width
                height: 34
                radius: 6
                color: copyMouse.containsMouse && copyRow.enabledItem ? Theme.buttonColor : "transparent"

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    text: fileContextMenu.targetPaths.length > 1 ? "Copy Selected" : "Copy"
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    color: copyRow.enabledItem ? Theme.textColor : Theme.subtleTextColor
                    verticalAlignment: Text.AlignVCenter
                }

                MouseArea {
                    id: copyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: copyRow.enabledItem
                    onClicked: {
                        fileContextMenu.close()
                        fileManagerController.copyEntries(fileContextMenu.targetPaths)
                    }
                }
            }

            Rectangle {
                id: downloadRow
                readonly property bool enabledItem: fileContextMenu.targetPaths.length > 0
                width: parent.width
                height: 34
                radius: 6
                color: downloadMouse.containsMouse && downloadRow.enabledItem ? Theme.buttonColor : "transparent"

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    text: fileContextMenu.targetPaths.length > 1 ? "Download Selected" : "Download"
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    color: downloadRow.enabledItem ? Theme.textColor : Theme.subtleTextColor
                    verticalAlignment: Text.AlignVCenter
                }

                MouseArea {
                    id: downloadMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: downloadRow.enabledItem
                    onClicked: {
                        fileContextMenu.close()
                        fileManagerController.downloadEntries(fileContextMenu.targetPaths)
                    }
                }
            }

            Rectangle {
                id: deleteRow
                readonly property bool enabledItem: fileContextMenu.targetPaths.length > 0
                width: parent.width
                height: 34
                radius: 6
                color: deleteMouse.containsMouse && deleteRow.enabledItem ? Theme.buttonColor : "transparent"

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    text: fileContextMenu.targetPaths.length > 1 ? "Delete Selected" : "Delete"
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    color: deleteRow.enabledItem ? root.destructiveText : Theme.subtleTextColor
                    verticalAlignment: Text.AlignVCenter
                }

                MouseArea {
                    id: deleteMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: deleteRow.enabledItem
                    onClicked: {
                        fileContextMenu.close()
                        deleteConfirmDialog.open()
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Row {
            Layout.fillWidth: true
            spacing: 16

            Text {
                text: "File Manager"
                font.family: Theme.fontFamily
                font.pixelSize: 30
                font.bold: true
                color: Theme.textColor
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: controlsColumn.implicitHeight + 36
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            Column {
                id: controlsColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                Flow {
                    width: parent.width
                    spacing: 10
                    visible: root.compactControls

                    Rectangle {
                        width: parent.width
                        height: 36
                        radius: 6
                        color: Theme.backgroundColor
                        border.color: Theme.panelBorderColor
                        border.width: 1

                        TextField {
                            id: pathEditFieldCompact
                            anchors.fill: parent
                            anchors.margins: 4
                            visible: root.pathEditMode
                            color: Theme.textColor
                            placeholderText: "Remote path"
                            placeholderTextColor: Theme.subtleTextColor
                            selectedTextColor: Theme.textColor
                            selectionColor: Theme.selectedColor
                            background: null
                            onAccepted: root.commitPathEdit()
                            onEditingFinished: {
                                if (!focus && root.pathEditMode)
                                    root.commitPathEdit()
                            }
                            Keys.onEscapePressed: root.cancelPathEdit()
                        }

                        Row {
                            anchors.fill: parent
                            anchors.margins: 3
                            spacing: 8
                            visible: !root.pathEditMode

                            Flickable {
                                id: breadcrumbFlickable
                                height: parent.height
                                width: Math.min(contentWidth, parent.width - 28 - parent.spacing)
                                contentWidth: breadcrumbRowCompact.width
                                contentHeight: breadcrumbRowCompact.height
                                clip: true
                                ScrollBar.horizontal: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                }

                                Row {
                                    id: breadcrumbRowCompact
                                    spacing: 6

                                    Repeater {
                                        model: root.pathSegments

                                        delegate: Row {
                                            spacing: 6

                                            ThemedButton {
                                                text: modelData.label
                                                height: 30
                                                width: Math.max(46, contentItem.implicitWidth + 16)
                                                onClicked: fileManagerController.browseTo(modelData.path)
                                            }

                                            Text {
                                                visible: index < root.pathSegments.length - 1
                                                text: ">"
                                                font.family: Theme.fontFamily
                                                color: Theme.subtleTextColor
                                                anchors.verticalCenter: parent.verticalCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                    }
                                }
                            }

                            Item {
                                id: editArea
                                height: parent.height
                                width: Math.max(28, parent.width - breadcrumbFlickable.width - parent.spacing)

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.beginPathEdit()
                                }
                            }
                        }
                    }

                    ThemedButton {
                        text: "Up"
                        width: 90
                        onClicked: fileManagerController.goUp()
                    }

                    ThemedButton {
                        text: "Refresh"
                        width: 130
                        onClicked: fileManagerController.refreshEntries()
                    }
                }

                Row {
                    width: parent.width
                    spacing: 10
                    visible: !root.compactControls

                    Rectangle {
                        width: Math.max(160, parent.width - upButton.width - refreshButton.width - (2 * parent.spacing))
                        height: 36
                        radius: 6
                        color: Theme.backgroundColor
                        border.color: Theme.panelBorderColor
                        border.width: 1

                        TextField {
                            id: pathEditFieldWide
                            anchors.fill: parent
                            anchors.margins: 4
                            visible: root.pathEditMode
                            color: Theme.textColor
                            placeholderText: "Remote path"
                            placeholderTextColor: Theme.subtleTextColor
                            selectedTextColor: Theme.textColor
                            selectionColor: Theme.selectedColor
                            background: null
                            onAccepted: root.commitPathEdit()
                            onEditingFinished: {
                                if (!focus && root.pathEditMode)
                                    root.commitPathEdit()
                            }
                            Keys.onEscapePressed: root.cancelPathEdit()
                        }

                        Row {
                            anchors.fill: parent
                            anchors.margins: 3
                            spacing: 8
                            visible: !root.pathEditMode

                            Flickable {
                                height: parent.height
                                width: Math.min(contentWidth, parent.width - 28 - parent.spacing)
                                contentWidth: breadcrumbRowWide.width
                                contentHeight: breadcrumbRowWide.height
                                clip: true
                                ScrollBar.horizontal: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                }

                                Row {
                                    id: breadcrumbRowWide
                                    spacing: 6

                                    Repeater {
                                        model: root.pathSegments

                                        delegate: Row {
                                            spacing: 6

                                            ThemedButton {
                                                text: modelData.label
                                                height: 30
                                                width: Math.max(46, contentItem.implicitWidth + 16)
                                                onClicked: fileManagerController.browseTo(modelData.path)
                                            }

                                            Text {
                                                visible: index < root.pathSegments.length - 1
                                                text: ">"
                                                font.family: Theme.fontFamily
                                                color: Theme.subtleTextColor
                                                anchors.verticalCenter: parent.verticalCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                    }
                                }
                            }

                            Item {
                                height: parent.height
                                width: Math.max(28, parent.width - parent.spacing - 56)

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.beginPathEdit()
                                }
                            }
                        }
                    }

                    ThemedButton {
                        id: upButton
                        text: "Up"
                        width: 90
                        onClicked: fileManagerController.goUp()
                    }

                    ThemedButton {
                        id: refreshButton
                        text: "Refresh"
                        width: 130
                        onClicked: fileManagerController.refreshEntries()
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 12
                    visible: root.compactControls

                    ThemedButton {
                        id: chooseDownloadFolderButton
                        text: "Choose Download Folder"
                        width: Math.min(220, parent.width)
                        onClicked: folderDialog.open()
                    }

                    Text {
                        text: fileManagerController.localDownloadDirectory
                        width: Math.max(120, Math.min(parent.width, parent.width - chooseDownloadFolderButton.width - parent.spacing))
                        height: chooseDownloadFolderButton.height
                        wrapMode: Text.WordWrap
                        elide: Text.ElideMiddle
                        color: Theme.mutedTextColor
                        font.family: Theme.fontFamily
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Row {
                    width: parent.width
                    spacing: 12
                    visible: !root.compactControls

                    ThemedButton {
                        id: chooseDownloadFolderButtonWide
                        text: "Choose Download Folder"
                        width: 220
                        onClicked: folderDialog.open()
                    }

                    Text {
                        text: fileManagerController.localDownloadDirectory
                        width: Math.max(120, parent.width - chooseDownloadFolderButtonWide.width - parent.spacing)
                        height: chooseDownloadFolderButtonWide.height
                        wrapMode: Text.WordWrap
                        elide: Text.ElideMiddle
                        color: Theme.mutedTextColor
                        font.family: Theme.fontFamily
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 10
                    visible: root.compactControls

                    ThemedButton {
                        text: "Copy"
                        width: 110
                        enabled: root.selectedCount > 0
                        onClicked: fileManagerController.copyEntries(root.selectedRemotePaths)
                    }

                    ThemedButton {
                        text: "Download"
                        width: 130
                        enabled: root.selectedCount > 0
                        onClicked: fileManagerController.downloadEntries(root.selectedRemotePaths)
                    }

                    ThemedButton {
                        text: "Clear"
                        width: 80
                        enabled: root.selectedCount > 0
                        onClicked: root.clearSelection()
                    }

                    ThemedButton {
                        text: "Upload Files"
                        width: 150
                        onClicked: uploadDialog.open()
                    }

                    ThemedButton {
                        text: "Upload Folder"
                        width: 160
                        onClicked: uploadFolderDialog.open()
                    }
                }

                Row {
                    width: parent.width
                    spacing: 10
                    visible: !root.compactControls

                    ThemedButton {
                        text: "Copy"
                        width: 110
                        enabled: root.selectedCount > 0
                        onClicked: fileManagerController.copyEntries(root.selectedRemotePaths)
                    }

                    ThemedButton {
                        text: "Download"
                        width: 130
                        enabled: root.selectedCount > 0
                        onClicked: fileManagerController.downloadEntries(root.selectedRemotePaths)
                    }

                    ThemedButton {
                        text: "Clear"
                        width: 80
                        enabled: root.selectedCount > 0
                        onClicked: root.clearSelection()
                    }

                    Item {
                        width: Math.max(0, parent.width - (110 + 130 + 80 + 150 + 160 + (5 * parent.spacing)))
                        height: 1
                    }

                    ThemedButton {
                        text: "Upload Files"
                        width: 150
                        onClicked: uploadDialog.open()
                    }

                    ThemedButton {
                        text: "Upload Folder"
                        width: 160
                        onClicked: uploadFolderDialog.open()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            radius: 12
            color: root.dropActive ? Qt.lighter(Theme.panelColor, 1.08) : Theme.panelColor
            border.width: root.dropActive ? 2 : 1
            border.color: root.dropActive ? Theme.selectedBorderColor : Theme.panelBorderColor

            Rectangle {
                id: entriesHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 10
                anchors.topMargin: 10
                anchors.rightMargin: scrollGutter.visible ? 28 : 10
                height: 34
                radius: 6
                color: Theme.backgroundColor
                border.color: Theme.panelBorderColor
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    Item {
                        width: root.iconColumnWidth(parent.width)
                        height: parent.height
                    }

                    Text {
                        width: root.nameColumnWidth(parent.width)
                        height: parent.height
                        text: "Name" + root.sortIndicator("name")
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.bold: root.sortColumn === "name"
                        color: Theme.subtleTextColor
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setSortColumn("name")
                        }
                    }

                    Text {
                        width: root.sizeColumnWidth(parent.width)
                        height: parent.height
                        visible: width > 0
                        text: "Size" + root.sortIndicator("size")
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.bold: root.sortColumn === "size"
                        color: Theme.subtleTextColor
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setSortColumn("size")
                        }
                    }

                    Text {
                        width: root.modifiedColumnWidth(parent.width)
                        height: parent.height
                        text: "Modified" + root.sortIndicator("modified")
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.bold: root.sortColumn === "modified"
                        color: Theme.subtleTextColor
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setSortColumn("modified")
                        }
                    }
                }
            }

            ListView {
                id: entriesView
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: entriesHeader.bottom
                anchors.bottom: parent.bottom
                anchors.leftMargin: 10
                anchors.topMargin: 8
                anchors.bottomMargin: 10
                anchors.rightMargin: scrollGutter.visible ? 28 : 10
                clip: true
                spacing: 8
                model: root.sortedRemoteEntries
                ScrollBar.vertical: ScrollBar {
                    id: entriesScrollBar
                    parent: scrollGutter
                    anchors.fill: parent
                    policy: ScrollBar.AlwaysOn
                    active: true
                    opacity: 1.0
                    visible: scrollGutter.visible
                }

                delegate: Rectangle {
                    required property int index
                    required property var modelData

                    function requestVisibleIcon() {
                        if (modelData && modelData.path)
                            fileManagerController.requestEntryIcon(modelData.path)
                    }

                    width: entriesView.width
                    height: 58
                    radius: 10
                    color: root.isPathSelected(modelData.path)
                           ? Theme.selectedColor
                           : (modelData.isDirectory ? Qt.lighter(Theme.panelColor, 1.05) : Theme.panelColor)
                    border.color: root.isPathSelected(modelData.path)
                                  ? Theme.selectedBorderColor
                                  : Theme.panelBorderColor

                    Component.onCompleted: requestVisibleIcon()
                    onModelDataChanged: requestVisibleIcon()

                    Row {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Image {
                            readonly property string resolvedIconSource: root.entryIconSource(modelData)
                            source: Theme.dark && resolvedIconSource.endsWith(".svg")
                                    ? resolvedIconSource.replace(".svg", "_dark.svg")
                                    : resolvedIconSource
                            width: root.iconColumnWidth(parent.width)
                            height: root.iconColumnWidth(parent.width)
                            sourceSize.width: width
                            sourceSize.height: height
                            asynchronous: true
                            cache: true
                            anchors.verticalCenter: parent.verticalCenter
                            fillMode: Image.PreserveAspectFit
                        }

                        Column {
                            width: root.nameColumnWidth(parent.width)
                            spacing: 2
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                text: modelData.name
                                font.family: Theme.fontFamily
                                font.pixelSize: 16
                                font.bold: true
                                color: Theme.textColor
                                elide: Text.ElideRight
                                width: parent.width
                            }

                            Text {
                                text: modelData.typeLabel
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: Theme.subtleTextColor
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }

                        Text {
                            width: root.sizeColumnWidth(parent.width)
                            height: parent.height
                            text: modelData.sizeLabel
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: Theme.mutedTextColor
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            width: root.modifiedColumnWidth(parent.width)
                            height: parent.height
                            text: modelData.modifiedLabel
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: Theme.mutedTextColor
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        property real pressX: 0
                        property real pressY: 0
                        property bool dragTriggered: false
                        property bool deferSingleSelection: false

                        onPressed: function(mouse) {
                            pressX = mouse.x
                            pressY = mouse.y
                            dragTriggered = false
                            deferSingleSelection = false

                            if (mouse.button === Qt.RightButton) {
                                root.selectionForContext(modelData.path, index)
                                fileContextMenu.targetPaths = root.selectedRemotePaths.slice()
                                fileContextMenu.targetPath = modelData.path
                                const targetEntry = root.entryByPath(modelData.path)
                                fileContextMenu.singleTargetIsDirectory = targetEntry && targetEntry.isDirectory
                                const position = mapToItem(root, mouse.x, mouse.y)
                                fileContextMenu.popup(position.x, position.y)
                                return
                            }

                            if (mouse.modifiers & Qt.ShiftModifier) {
                                root.selectRange(index)
                            } else if (mouse.modifiers & Qt.ControlModifier) {
                                root.toggleSelection(modelData.path, index)
                            } else {
                                if (root.isPathSelected(modelData.path) && root.selectedCount > 1) {
                                    deferSingleSelection = true
                                } else {
                                    root.setSingleSelection(modelData.path, index)
                                }
                            }
                        }

                        onPositionChanged: function(mouse) {
                            if (!(mouse.buttons & Qt.LeftButton) || dragTriggered)
                                return

                            const dx = mouse.x - pressX
                            const dy = mouse.y - pressY
                            const dragDistance = Math.sqrt(dx * dx + dy * dy)
                            if (dragDistance < Qt.styleHints.startDragDistance)
                                return

                            dragTriggered = true
                            root.beginExternalDrag(modelData.path, index)
                        }

                        onReleased: function(mouse) {
                            if (mouse.button === Qt.LeftButton && deferSingleSelection && !dragTriggered) {
                                root.setSingleSelection(modelData.path, index)
                            }
                            deferSingleSelection = false
                        }

                        onCanceled: deferSingleSelection = false

                        onDoubleClicked: {
                            root.setSingleSelection(modelData.path, index)
                            if (modelData.isDirectory) {
                                fileManagerController.browseTo(modelData.path)
                            } else {
                                fileManagerController.openEntry(modelData.path)
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: scrollGutter
                anchors.top: entriesHeader.bottom
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.topMargin: 8
                anchors.bottomMargin: 10
                anchors.rightMargin: 8
                width: 14
                radius: 7
                color: Theme.backgroundColor
                border.color: Theme.panelBorderColor
                border.width: 1
                visible: entriesView.contentHeight > entriesView.height + 1
            }

            DropArea {
                anchors.fill: parent
                enabled: !fileManagerController.dragExportInProgress
                onEntered: function(drag) {
                    if (fileManagerController.dragExportInProgress)
                        return

                    if (!drag.hasUrls)
                        return

                    root.dropActive = true
                    drag.acceptProposedAction()
                }
                onExited: root.dropActive = false
                onDropped: function(drop) {
                    root.dropActive = false
                    if (fileManagerController.dragExportInProgress)
                        return

                    if (!drop.hasUrls || drop.urls.length === 0)
                        return

                    root.uploadLocalUrls(drop.urls)
                    drop.acceptProposedAction()
                }
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - 40
                spacing: 8
                visible: entriesView.count === 0

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: fileManagerController.busy
                    visible: running
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.subtleTextColor
                    font.family: Theme.fontFamily
                    text: fileManagerController.busy
                          ? "Loading files from the connected device..."
                          : "No entries in this folder."
                }
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 1
            value: fileManagerController.transferProgress
            visible: fileManagerController.busy || fileManagerController.transferProgress > 0
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: fileManagerController.statusMessage
                wrapMode: Text.WordWrap
                color: Theme.mutedTextColor
                font.family: Theme.fontFamily
                font.pixelSize: 15
            }

            Text {
                text: root.selectedCount > 0
                      ? ("Files selected: " + root.selectedCount)
                      : "Files selected: 0"
                color: Theme.mutedTextColor
                font.family: Theme.fontFamily
                font.pixelSize: 15
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    Component.onCompleted: rebuildPathSegments()
}
