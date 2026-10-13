import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Estatísticas do stream: problemas, gráficos e detalhes (métricas do TS 6).
ScrollView {
    id: root
    property string streamId
    property int viewerClid: 0
    property string tab: "video"
    property var data_: Screenshare.stats(streamId, viewerClid)
    contentWidth: availableWidth
    clip: true

    Connections {
        target: Screenshare
        function onStatsChanged(id) {
            if (id === root.streamId)
                root.data_ = Screenshare.stats(root.streamId, root.viewerClid)
        }
    }

    readonly property var s: data_[tab] || ({})
    readonly property bool inbound: !data_.own
    readonly property var history: (data_.history || {})[tab] || []

    function ms(v) { return Math.round(1000 * (v || 0)) + "ms" }
    function bitrate(bps) {
        if (!bps || bps <= 0) return "0 bps"
        const u = ["bps", "Kbps", "Mbps", "Gbps"]
        let i = 0
        while (bps >= 1000 && i < 3) { bps /= 1000; ++i }
        return bps.toFixed(i === 0 ? 0 : 1) + " " + u[i]
    }
    function bytes(b) {
        if (!b || b <= 0) return "0 B"
        const u = ["B", "KiB", "MiB", "GiB"]
        let i = 0
        while (b >= 1024 && i < 3) { b /= 1024; ++i }
        return b.toFixed(i === 0 ? 0 : 1) + " " + u[i]
    }
    function sev(key) { return (s.issueSeverity || {})[key] || 0 }
    function series(field) { return history.map(h => h[field] || 0) }

    ColumnLayout {
        width: parent.width
        spacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 10

            RowLayout {
                visible: !!root.data_.own
                Layout.fillWidth: true
                Label { text: I18n.t("video.stream.stats.viewer"); color: Theme.textDim }
                ComboBox {
                    Layout.fillWidth: true
                    model: (root.data_.viewers || []).map(v => v.nickname)
                    currentIndex: Math.max(0, (root.data_.viewers || []).findIndex(v => v.clid === root.data_.viewerClid))
                    onActivated: function(i) {
                        root.viewerClid = root.data_.viewers[i].clid
                        root.data_ = Screenshare.stats(root.streamId, root.viewerClid)
                    }
                }
            }

            Label {
                visible: !root.data_.video
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: root.data_.own ? I18n.t("video.stream.stats.no_viewers") : I18n.t("video.join.status.connecting")
                color: Theme.textDim
            }

            TabBar {
                visible: !!root.data_.video
                Layout.fillWidth: true
                currentIndex: root.tab === "video" ? 0 : 1
                onCurrentIndexChanged: root.tab = currentIndex === 0 ? "video" : "audio"
                TabButton { text: I18n.t("video.stream.stats.video.title") }
                TabButton { text: I18n.t("video.stream.stats.audio.title") }
            }

            // problemas
            ColumnLayout {
                visible: !!root.data_.video
                Layout.fillWidth: true
                spacing: 4
                Rectangle {
                    visible: (root.s.issues || []).length === 0
                    Layout.fillWidth: true
                    implicitHeight: 32
                    radius: 5
                    color: Qt.rgba(0.07, 0.97, 0.55, 0.07)
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        Icon { name: "check"; size: 16; color: Theme.online }
                        Label { text: I18n.t("video.stream.issues.none"); color: Theme.online }
                    }
                }
                Repeater {
                    model: root.s.issues || []
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 32
                        radius: 5
                        color: modelData.severity >= 2 ? Qt.rgba(0.94, 0.22, 0.29, 0.1) : Qt.rgba(0.96, 0.65, 0.14, 0.09)
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            Icon { name: modelData.severity >= 2 ? "warning" : "info"; size: 16; color: Theme.severityColor(modelData.severity) }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: I18n.t("video.stream.issue." + modelData.category + "." + modelData.name,
                                             { value: modelData.value, expected: modelData.expected })
                                color: Theme.severityColor(modelData.severity)
                            }
                        }
                    }
                }
            }

            // gráficos
            GridLayout {
                visible: !!root.data_.video
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                StatGraph {
                    Layout.fillWidth: true
                    title: I18n.t("video.stream.stats.packet_loss")
                    valueText: (100 * (root.s.fractionLost || 0)).toFixed(2) + "%"
                    samples: root.series("fractionLost")
                    minimumMax: 0.1
                    severity: root.sev("network:packetloss")
                }
                StatGraph {
                    Layout.fillWidth: true
                    title: I18n.t("video.stream.stats.jitter.title")
                    valueText: root.ms(root.s.jitter)
                    samples: root.series("jitter")
                    minimumMax: 0.1
                    severity: root.sev("network:jitter")
                }
                StatGraph {
                    Layout.fillWidth: true
                    visible: !root.inbound
                    title: I18n.t("video.stream.stats.ping")
                    valueText: root.ms((root.s.lastRoundTripTime || 0) / 2)
                    samples: root.series("rtt")
                    minimumMax: 0.1
                    severity: root.sev("network:rtt")
                }
                StatGraph {
                    Layout.fillWidth: true
                    title: I18n.t("video.stream.stats.retransmit.title")
                    valueText: root.bitrate(root.s.retransmitBps)
                    samples: root.series("retransmitBps")
                    minimumMax: 100000
                    severity: root.sev("network:retransmit")
                }
                StatGraph {
                    Layout.fillWidth: true
                    title: I18n.t("video.stream.stats.bitrate")
                    valueText: root.bitrate(root.inbound ? root.s.receivedBps : root.s.sentBps)
                    samples: root.series("bps")
                    minimumMax: 100000
                }
                StatGraph {
                    Layout.fillWidth: true
                    visible: root.tab === "video"
                    title: root.inbound ? I18n.t("video.stream.stats.decode_time.title") : I18n.t("video.stream.stats.encode_time.title")
                    valueText: root.ms(root.inbound ? root.s.avgDecodeTime : root.s.avgEncodeTime)
                    samples: root.series("codingTime")
                    minimumMax: 0.02
                    severity: root.sev("video:encodetime")
                }
                StatGraph {
                    Layout.fillWidth: true
                    visible: root.tab === "video"
                    title: I18n.t("video.stream.stats.fps.title")
                    valueText: String(root.s.fps || 0)
                    samples: root.series("fps")
                    minimumMax: 30
                    severity: root.sev("video:fps")
                }
            }

            // detalhes
            Repeater {
                model: {
                    if (!root.data_.video)
                        return []
                    const s = root.s
                    const t = root.data_.target || {}
                    const rows = [[I18n.t("video.stream.stats.codec"), (s.codec || "").replace(/^(video|audio)\//, "") || "—"]]
                    if (root.tab === "video") {
                        if (root.inbound) {
                            rows.push([I18n.t("video.stream.stats.video.decoder"), s.decoder || "—"])
                        } else {
                            rows.push([I18n.t("video.stream.stats.video.encoder"), s.encoder || "—"])
                            rows.push([I18n.t("video.stream.stats.video.capture"), root.data_.capture || "—"])
                            rows.push([I18n.t("video.stream.stats.video.limit.reason"), s.limitReason || "none", "video:qualitylimit"])
                        }
                        const tw = t.width > 0 ? t.width + "x" + t.height : I18n.t("video.preset.source")
                        rows.push([I18n.t("video.stream.stats.video.quality.target"), tw + " " + (t.fps || 0) + "fps"])
                        rows.push([I18n.t("video.stream.stats.video.quality.current"), (s.width || 0) + "x" + (s.height || 0) + " " + (s.fps || 0) + "fps", "video:fps"])
                    }
                    if (root.tab === "audio" && !root.inbound)
                        rows.push([I18n.t("video.stream.stats.video.capture"), root.data_.audioCapture || "—"])
                    if (root.inbound) {
                        rows.push([I18n.t("video.stream.stats.received.packets"), String(s.receivedPackets || 0)])
                        rows.push([I18n.t("video.stream.stats.received.bytes"), root.bytes(s.receivedBytes)])
                        rows.push([I18n.t("video.stream.stats.received.packets_lost"), String(s.packetsLost || 0), "network:packetloss"])
                    } else {
                        rows.push([I18n.t("video.stream.stats.sent.packets"), String(s.sentPackets || 0)])
                        rows.push([I18n.t("video.stream.stats.sent.bytes"), root.bytes(s.sentBytes)])
                        rows.push([I18n.t("video.stream.stats.sent.packets_lost"), String(s.packetsLost || 0), "network:packetloss"])
                        rows.push([I18n.t("video.stream.stats.bitrate.target"), root.bitrate(s.targetBitrate)])
                        rows.push([I18n.t("video.stream.stats.ping.avg"), root.ms((s.avgRoundTripTime || 0) / 2), "network:rtt"])
                    }
                    rows.push([I18n.t("video.stream.stats.retransmit.packets"), String(s.retransmitPackets || 0), "network:retransmit"])
                    rows.push([I18n.t("video.stream.stats.retransmit.bytes"), root.bytes(s.retransmitBytes)])
                    rows.push([I18n.t("video.stream.stats.connection_type"), s.connectionType ? I18n.t("video.stream.stats.connection." + s.connectionType) : "—"])
                    rows.push([I18n.t("video.stream.stats.srtp_cipher"), s.srtpCipher || "—"])
                    return rows
                }
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                        Label { text: modelData[0]; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                        Label {
                            text: modelData[1]
                            font.pixelSize: 12
                            color: modelData[2] && root.sev(modelData[2]) ? Theme.severityColor(root.sev(modelData[2])) : Theme.text
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                }
            }
        }
    }
}
