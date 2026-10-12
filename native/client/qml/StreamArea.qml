import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Área central: grade automática / foco (algoritmo do TS 6 em C++), paginação e barra inferior.
Rectangle {
    id: root
    color: Theme.background

    signal startStreamRequested()
    signal editStreamRequested(string id)
    signal changeSourceRequested(string id)
    signal viewersRequested(string id)
    signal statsRequested(string id)

    property string focusId: ""
    property int offset: 0
    property var rects: ({})
    property int perPage: 0
    readonly property int gap: 8

    function focusStream(id) {
        if (!hasItem(id))
            Screenshare.joinStream(id)
        focusId = id
        offset = 0
        relayout()
    }
    function hasItem(id) {
        for (let i = 0; i < streams.count; ++i)
            if (streams.get(i).streamIdentifier === id)
                return true
        return false
    }

    // Modelo estável: atualiza no lugar para não recriar os VideoOutput.
    ListModel { id: streams }
    readonly property var roleDefaults: ({
        kind: "", name: "", type: 0, access: 0, viewerCount: 0, paused: false, thumbnail: "", ownerName: "",
        status: "", errorKey: "", severity: 0, pendingRequests: 0, volume: 1.0, hasVideo: false, ownerClid: 0
    })

    function sync() {
        const items = Screenshare.items
        const ids = items.map(it => it.streamIdentifier)
        for (let i = streams.count - 1; i >= 0; --i)
            if (ids.indexOf(streams.get(i).streamIdentifier) < 0)
                streams.remove(i)
        for (let i = 0; i < items.length; ++i) {
            const it = items[i]
            const row = { streamIdentifier: it.streamIdentifier }
            for (const r in roleDefaults)
                row[r] = it[r] === undefined || it[r] === null ? roleDefaults[r] : it[r]
            let at = -1
            for (let j = 0; j < streams.count; ++j)
                if (streams.get(j).streamIdentifier === it.streamIdentifier) { at = j; break }
            if (at < 0) {
                streams.insert(i, row)
                continue
            }
            if (at !== i)
                streams.move(at, i, 1)
            for (const r in roleDefaults)
                if (streams.get(i)[r] !== row[r])
                    streams.setProperty(i, r, row[r])
        }
        if (focusId && ids.indexOf(focusId) < 0)
            focusId = ""
        relayout()
    }
    function relayout() {
        let focusIndex = -1
        for (let i = 0; i < streams.count; ++i)
            if (streams.get(i).streamIdentifier === focusId) focusIndex = i
        const r = StreamLayout.compute(streams.count, focusIndex, stage.width, stage.height, gap, offset)
        const map = {}
        for (const t of r.tiles)
            map[streams.get(t.index).streamIdentifier] = t
        rects = map
        perPage = r.perPage
    }

    Connections {
        target: Screenshare
        function onItemsChanged() { root.sync() }
    }
    Component.onCompleted: sync()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                id: stage
                anchors.fill: parent
                anchors.margins: 10
                onWidthChanged: root.relayout()
                onHeightChanged: root.relayout()

                Repeater {
                    model: streams
                    delegate: StreamTile {
                        required property string streamIdentifier
                        required property var model
                        streamId: streamIdentifier
                        data_: model
                        readonly property var rect: root.rects[streamIdentifier]
                        visible: !!rect
                        x: rect ? rect.x : 0
                        y: rect ? rect.y : 0
                        width: rect ? rect.w : 0
                        height: rect ? rect.h : 0
                        focused: root.focusId === streamIdentifier
                        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        onToggleFocus: {
                            root.focusId = root.focusId === streamIdentifier ? "" : streamIdentifier
                            root.offset = 0
                            root.relayout()
                        }
                        onEditRequested: root.editStreamRequested(streamIdentifier)
                        onChangeSourceRequested: root.changeSourceRequested(streamIdentifier)
                        onViewersRequested: root.viewersRequested(streamIdentifier)
                        onStatsRequested: root.statsRequested(streamIdentifier)
                    }
                }
            }

            // Vazio
            ColumnLayout {
                anchors.centerIn: parent
                visible: streams.count === 0
                spacing: 10
                Icon { Layout.alignment: Qt.AlignHCenter; name: "stream"; size: 56; color: Theme.borderStrong }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: I18n.t("video.stream.empty")
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Label { Layout.alignment: Qt.AlignHCenter; text: I18n.t("video.stream.empty.hint"); color: Theme.textDim }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    visible: Server.hasStreamingP2P
                    text: I18n.t("video.action.start_stream")
                    highlighted: true
                    palette.accent: Theme.live
                    onClicked: root.startStreamRequested()
                }
            }

            // Paginação
            RoundButton {
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                visible: root.offset > 0
                contentItem: Item {}
        Icon { anchors.centerIn: parent; name: "chevronLeft" }
                onClicked: { root.offset = Math.max(0, root.offset - Math.max(1, root.perPage)); root.relayout() }
            }
            RoundButton {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                visible: root.perPage > 0 && root.offset + root.perPage < (root.focusId ? streams.count - 1 : streams.count)
                contentItem: Item {}
        Icon { anchors.centerIn: parent; name: "chevronRight" }
                onClicked: { root.offset += Math.max(1, root.perPage); root.relayout() }
            }
        }

        // ---------------------------------------------------- barra inferior
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: Theme.panel
            Rectangle { width: parent.width; height: 1; color: Theme.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Button {
                    visible: Server.hasStreamingP2P
                    text: I18n.t("video.action.start_stream")
                    highlighted: true
                    palette.accent: Theme.live
                    onClicked: root.startStreamRequested()
                }
                Button {
                    visible: Screenshare.pendingRequests > 0
                    text: Screenshare.pendingRequests + " " + I18n.t("video.viewers.join_request").toLowerCase()
                    palette.buttonText: Theme.warn
                    ToolTip.visible: hovered
                    ToolTip.text: I18n.t("video.join_requests.tooltip")
                    onClicked: {
                        for (let i = 0; i < streams.count; ++i)
                            if (streams.get(i).kind === "own" && streams.get(i).pendingRequests > 0) {
                                root.viewersRequested(streams.get(i).streamIdentifier)
                                return
                            }
                    }
                }
                Label { text: I18n.t("video.streams", { count: streams.count }); color: Theme.textDim; leftPadding: 6 }
                Item { Layout.fillWidth: true }
                Button {
                    flat: true
                    visible: root.focusId !== ""
                    text: I18n.t("video.layout.auto")
                    ToolTip.visible: hovered
                    ToolTip.text: I18n.t("video.layout.auto.tooltip")
                    onClicked: { root.focusId = ""; root.offset = 0; root.relayout() }
                }
                Button {
                    id: hiddenButton
                    flat: true
                    readonly property var hiddenHere: {
                        const out = []
                        const tree = Server.tree
                        for (const ch of tree) {
                            if (!ch.current) continue
                            for (const c of ch.clients)
                                for (const s of c.streams)
                                    if (Settings.hiddenStreams.indexOf(s.streamIdentifier) >= 0)
                                        out.push(s)
                        }
                        return out
                    }
                    visible: hiddenHere.length > 0
                    text: I18n.t("video.stream.manage_hidden_streams") + " (" + hiddenHere.length + ")"
                    onClicked: hiddenMenu.open()
                    Menu {
                        id: hiddenMenu
                        y: -implicitHeight
                        Repeater {
                            model: hiddenButton.hiddenHere
                            delegate: MenuItem {
                                required property var modelData
                                text: I18n.t("video.stream.show_stream") + ": " + modelData.name
                                onTriggered: Settings.showStream(modelData.streamIdentifier)
                            }
                        }
                    }
                }
                Button {
                    id: viewButton
                    flat: true
                    text: I18n.t("video.view_settings")
                    onClicked: viewMenu.open()
                    Menu {
                        id: viewMenu
                        y: -implicitHeight
                        MenuItem {
                            text: I18n.t("video.view_settings.show_own_streams")
                            checkable: true
                            checked: Settings.showOwnStreams
                            onTriggered: Settings.showOwnStreams = checked
                        }
                        MenuItem {
                            text: I18n.t("video.view_settings.show_inactive_streams")
                            checkable: true
                            checked: Settings.showInactiveStreams
                            onTriggered: Settings.showInactiveStreams = checked
                        }
                    }
                }
            }
        }
    }
}
