import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import "I18n.js" as I18n

// "Iniciar transmissão" / "Ajustar transmissão" (setup-stream do TS 6):
// fonte + pré-visualização à esquerda, configurações à direita.
Dialog {
    id: dialog
    property string editId: ""
    property bool sourceOnly: false
    readonly property bool editing: editId !== "" && !sourceOnly

    modal: true
    width: Math.min(1000, parent ? parent.width - 40 : 1000)
    height: Math.min(680, parent ? parent.height - 40 : 680)
    padding: 0
    title: editing ? I18n.t("video.setup.title.edit") : sourceOnly ? I18n.t("video.action.change_source") : I18n.t("video.setup.title")

    readonly property var presets: [
        { id: "360", bitrate: 500000, width: 640, height: 360, fps: 30, audioBitrate: 64000 },
        { id: "480", bitrate: 1000000, width: 852, height: 480, fps: 30, audioBitrate: 96000 },
        { id: "720", bitrate: 3000000, width: 1280, height: 720, fps: 30, audioBitrate: 128000 },
        { id: "1080", bitrate: 5000000, width: 1920, height: 1080, fps: 30, audioBitrate: 192000 },
        { id: "1440", bitrate: 6500000, width: 2560, height: 1440, fps: 30, audioBitrate: 256000 },
        { id: "source", bitrate: 8000000, width: -1, height: -1, fps: 30, audioBitrate: 320000 },
        { id: "presentation", bitrate: 2000000, width: -1, height: -1, fps: 5, audioBitrate: 320000 }
    ]
    readonly property var resolutions: [
        { id: "360", width: 640, height: 360 }, { id: "480", width: 852, height: 480 },
        { id: "720", width: 1280, height: 720 }, { id: "1080", width: 1920, height: 1080 },
        { id: "1440", width: 2560, height: 1440 }, { id: "source", width: -1, height: -1 }
    ]

    property var cfg: ({})
    property int sourceTab: 0 // 0 telas, 1 janelas, 2 câmeras, 3 teste
    property var sourceList: []
    property string selectedSource: ""
    property bool busy: false

    function patch(p) { cfg = Object.assign({}, cfg, p) }
    function presetId() {
        for (const p of presets)
            if (p.width === cfg.width && p.height === cfg.height && p.fps === cfg.fps && p.bitrate === cfg.bitrate)
                return p.id
        return "custom"
    }
    function applyPreset(id) {
        const p = presets.find(x => x.id === id)
        if (p) patch({ width: p.width, height: p.height, fps: p.fps, bitrate: p.bitrate, audioBitrate: p.audioBitrate })
    }
    function tabType(tab) { return tab === 1 ? 3 : tab === 2 ? 1 : 2 }
    function refreshSources() {
        sourceList = sourceTab === 3 ? [{ type: 2, id: "test", label: I18n.t("video.setup.test_pattern"), test: true }]
                                     : Screenshare.sources(tabType(sourceTab))
    }
    function pick(src) {
        selectedSource = src.id + ":" + src.type
        Screenshare.openPreview(src, cfg.fps || 30)
    }

    onOpened: {
        busy = false
        selectedSource = ""
        if (editing || sourceOnly) {
            const c = Screenshare.ownConfig(editId)
            cfg = c
        } else {
            const c = Object.assign({}, Settings.lastStreamingConfig)
            c.name = "Stream de " + Server.clientName(Server.ownClid)
            cfg = c
        }
        if (editing) {
            Screenshare.attachOwnPreview(editId, preview.videoSink)
        } else {
            sourceTab = 0
            refreshSources()
        }
    }
    onClosed: {
        if (editing)
            Screenshare.detachOwnPreview(editId, preview.videoSink)
        Screenshare.detachPreview(preview.videoSink)
        if (Screenshare.previewActive)
            Screenshare.closePreview()
    }
    Connections {
        target: Screenshare
        function onPreviewChanged() {
            if (Screenshare.previewActive && !dialog.editing)
                Screenshare.attachPreview(preview.videoSink)
        }
    }

    background: Rectangle { radius: 12; color: Theme.panel; border.color: Theme.borderStrong }

    contentItem: RowLayout {
        spacing: 0

        // ------------------------------------------------------- fonte
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: dialog.sourceOnly ? dialog.width : dialog.width * 0.54
            Layout.margins: 16
            spacing: 12

            TabBar {
                Layout.fillWidth: true
                visible: !dialog.editing
                currentIndex: dialog.sourceTab
                onCurrentIndexChanged: if (currentIndex !== dialog.sourceTab) { dialog.sourceTab = currentIndex; dialog.refreshSources() }
                TabButton { text: I18n.t("video.sources.screens") }
                TabButton { text: I18n.t("video.sources.apps") }
                TabButton { text: I18n.t("video.sources.cameras") }
                TabButton { text: I18n.t("video.sources.test"); visible: Settings.debug; width: visible ? implicitWidth : 0 }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: width * 9 / 16
                radius: Theme.radius
                color: "#05070b"
                border.color: Theme.border
                VideoOutput {
                    id: preview
                    anchors.fill: parent
                    anchors.margins: 1
                    fillMode: VideoOutput.PreserveAspectFit
                    visible: dialog.editing || Screenshare.previewActive
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: !preview.visible
                    Icon { Layout.alignment: Qt.AlignHCenter; name: "stream"; size: 44; color: Theme.textFaint }
                    Label { text: I18n.t("video.stream.select"); color: Theme.textFaint }
                }
            }
            RowLayout {
                visible: dialog.editing || Screenshare.previewActive
                Icon { name: "check"; size: 15; color: Theme.online }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: dialog.editing ? (dialog.cfg.sourceLabel || "") : Screenshare.previewLabel
                    color: Theme.textDim
                }
            }

            // lista de fontes
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !dialog.editing
                clip: true
                spacing: 4
                model: dialog.sourceList
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    highlighted: dialog.selectedSource === modelData.id + ":" + modelData.type
                    text: modelData.label
                    icon.width: 0
                    onClicked: dialog.pick(modelData)
                }
                Label {
                    anchors.centerIn: parent
                    visible: parent.count === 0
                    text: dialog.sourceTab === 2 ? I18n.t("video.sources.no_camera") : I18n.t("video.error.no_available_source")
                    color: Theme.textDim
                }
            }
            RowLayout {
                visible: !dialog.editing
                Button { flat: true; text: I18n.t("video.setup.refresh"); onClicked: dialog.refreshSources() }
                Item { Layout.fillWidth: true }
                Switch {
                    text: I18n.t("video.audio")
                    checked: !!dialog.cfg.audio
                    visible: !dialog.sourceOnly
                    onToggled: dialog.patch({ audio: checked })
                    ToolTip.visible: hovered
                    ToolTip.text: I18n.t("video.audio.info")
                }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: Theme.border; visible: !dialog.sourceOnly }

        // ------------------------------------------------- configurações
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !dialog.sourceOnly
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 14

                Item { height: 2 }
                Label { Layout.leftMargin: 16; text: I18n.t("video.setup.section.basic").toUpperCase(); color: Theme.textDim; font.pixelSize: 12 }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 6
                    Label { text: I18n.t("video.name"); color: Theme.textDim }
                    TextField {
                        Layout.fillWidth: true
                        text: dialog.cfg.name || ""
                        maximumLength: 64
                        onTextEdited: dialog.patch({ name: text })
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 6
                    Label { text: I18n.t("video.preset"); color: Theme.textDim }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: dialog.presets
                            delegate: Button {
                                required property var modelData
                                text: I18n.t("video.preset." + modelData.id)
                                checkable: true
                                checked: dialog.presetId() === modelData.id
                                highlighted: checked
                                onClicked: dialog.applyPreset(modelData.id)
                            }
                        }
                    }
                    Label { visible: dialog.presetId() === "custom"; text: I18n.t("video.preset.custom"); color: Theme.textDim; font.pixelSize: 12 }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 6
                    Label { text: I18n.t("video.privacy"); color: Theme.textDim }
                    RowLayout {
                        spacing: 4
                        Repeater {
                            model: [{ v: 1, k: "public" }, { v: 2, k: "contacts" }, { v: 3, k: "private" }]
                            delegate: Button {
                                required property var modelData
                                text: I18n.t("video.privacy." + modelData.k)
                                checkable: true
                                checked: dialog.cfg.access === modelData.v
                                highlighted: checked
                                onClicked: dialog.patch({ access: modelData.v })
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textDim
                        font.pixelSize: 12
                        text: I18n.t("video.privacy." + (dialog.cfg.access === 2 ? "contacts" : dialog.cfg.access === 3 ? "private" : "public") + ".info")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 6
                    Label { text: I18n.t("video.mode.title"); color: Theme.textDim }
                    RowLayout {
                        spacing: 4
                        Button { text: I18n.t("video.mode.p2p"); checkable: true; checked: true; highlighted: true; enabled: !dialog.editing }
                        Button {
                            text: I18n.t("video.mode.server")
                            enabled: false
                            ToolTip.visible: hovered
                            ToolTip.text: I18n.t("video.mode.server.unsupported")
                        }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12; text: I18n.t("video.mode.p2p.info") }
                }

                // avançado
                Button {
                    Layout.leftMargin: 10
                    flat: true
                    text: (Settings.advancedMode ? "▾  " : "▸  ") + I18n.t("video.setup.section.advanced")
                    onClicked: Settings.advancedMode = !Settings.advancedMode
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    visible: Settings.advancedMode
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 10

                    Label { text: I18n.t("video.resolution"); color: Theme.textDim }
                    ComboBox {
                        Layout.fillWidth: true
                        model: dialog.resolutions.map(r => I18n.t("video.preset." + r.id))
                        currentIndex: {
                            const i = dialog.resolutions.findIndex(r => r.width === dialog.cfg.width && r.height === dialog.cfg.height)
                            return i < 0 ? 5 : i
                        }
                        onActivated: function(i) { dialog.patch({ width: dialog.resolutions[i].width, height: dialog.resolutions[i].height }) }
                    }

                    Label { text: I18n.t("video.fps"); color: Theme.textDim }
                    RowLayout {
                        spacing: 4
                        Repeater {
                            model: [5, 30, 60]
                            delegate: Button {
                                required property int modelData
                                text: modelData
                                checkable: true
                                checked: dialog.cfg.fps === modelData
                                highlighted: checked
                                onClicked: dialog.patch({ fps: modelData })
                            }
                        }
                    }

                    Label { text: I18n.t("video.bitrate"); color: Theme.textDim }
                    RowLayout {
                        Slider {
                            id: bitrateSlider
                            Layout.fillWidth: true
                            from: 250; to: 20000; stepSize: 250
                            value: (dialog.cfg.bitrate || 3000000) / 1000
                            onMoved: dialog.patch({ bitrate: value * 1000 })
                        }
                        Label { text: Math.round(bitrateSlider.value) + " Kbps"; color: Theme.text; Layout.preferredWidth: 80 }
                    }

                    Label { text: I18n.t("video.viewer_limit"); color: Theme.textDim }
                    SpinBox {
                        from: 0; to: 1000
                        value: dialog.cfg.viewerLimit || 0
                        editable: true
                        onValueModified: dialog.patch({ viewerLimit: value })
                        ToolTip.visible: hovered
                        ToolTip.text: I18n.t("video.viewer_limit.info")
                    }

                    Label { text: I18n.t("video.audio.bitrate"); color: Theme.textDim; visible: !!dialog.cfg.audio }
                    ComboBox {
                        visible: !!dialog.cfg.audio
                        model: [64, 96, 128, 192, 256, 320]
                        currentIndex: Math.max(0, [64, 96, 128, 192, 256, 320].indexOf((dialog.cfg.audioBitrate || 128000) / 1000))
                        onActivated: function(i) { dialog.patch({ audioBitrate: [64, 96, 128, 192, 256, 320][i] * 1000 }) }
                    }
                }
                Item { height: 8 }
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            text: I18n.t("app.cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            highlighted: true
            palette.accent: dialog.editing || dialog.sourceOnly ? Theme.accent : Theme.live
            enabled: !dialog.busy && (dialog.editing || Screenshare.previewActive)
            text: dialog.editing ? I18n.t("video.setup.adjust_settings")
                                 : dialog.sourceOnly ? I18n.t("video.action.change_source") : I18n.t("video.action.go_live")
            onClicked: {
                dialog.busy = true
                if (dialog.editing)
                    Screenshare.updateStream(dialog.editId, dialog.cfg)
                else if (dialog.sourceOnly)
                    Screenshare.changeSource(dialog.editId)
                else
                    Screenshare.startStream(dialog.cfg)
                dialog.close()
            }
        }
        onRejected: dialog.close()
    }
}
