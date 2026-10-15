import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Árvore de canais com clientes e indicadores de stream (ao vivo / espectadores).
Rectangle {
    id: root
    color: Theme.panel
    signal streamClicked(string id)

    function typeIcon(t) { return t === 1 ? "camera" : t === 3 ? "window" : "screen" }

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

    ScrollView {
        anchors.fill: parent
        anchors.rightMargin: 1
        clip: true

        Column {
            width: root.width - 1
            padding: 6
            spacing: 2

            RowLayout {
                width: parent.width - 12
                height: 34
                spacing: 8
                Item { width: 2 }
                Icon { name: "server"; color: Theme.gold }
                Label { text: Server.serverName; font.weight: Font.DemiBold; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                // Modos de transmissão que o servidor oferece; com relay (SFU) as
                // transmissões novas vão pelo servidor, senão direto (P2P).
                Rectangle {
                    visible: Server.hasStreamingP2P || Server.hasStreamingSFU
                    radius: 4
                    color: Theme.accentSoft
                    implicitWidth: modeLabel.implicitWidth + 12
                    implicitHeight: 18
                    Label { id: modeLabel; anchors.centerIn: parent; text: Server.hasStreamingSFU ? "SFU" : "P2P"; color: Theme.accent; font.pixelSize: 11; font.weight: Font.Bold }
                    HoverHandler { id: modeHover }
                    ToolTip.visible: modeHover.hovered
                    ToolTip.delay: 250
                    ToolTip.text: Server.hasStreamingSFU ? I18n.t("app.server.mode.sfu") : I18n.t("app.server.mode.p2p")
                }
            }

            Repeater {
                model: Server.tree
                delegate: Column {
                    id: channel
                    required property var modelData
                    width: parent.width - 12

                    ItemDelegate {
                        width: parent.width
                        height: 30
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: channel.modelData.description + "\n(" + I18n.t("app.channel.join") + ": duplo clique)"
                        onDoubleClicked: Server.moveTo(channel.modelData.cid)
                        contentItem: RowLayout {
                            spacing: 7
                            Icon { name: "channel"; size: 16; color: channel.modelData.current ? Theme.accent : Theme.textFaint }
                            Label {
                                Layout.fillWidth: true
                                text: channel.modelData.name
                                elide: Text.ElideRight
                                color: channel.modelData.current ? Theme.text : Theme.textDim
                                font.weight: Font.Medium
                            }
                            Row {
                                visible: channel.modelData.streamCount > 0
                                spacing: 3
                                Icon { name: "stream"; size: 14; color: Theme.live }
                                Label { text: channel.modelData.streamCount; color: Theme.live; font.pixelSize: 12 }
                            }
                        }
                    }

                    Repeater {
                        model: channel.modelData.clients
                        delegate: Column {
                            id: client
                            required property var modelData
                            width: channel.width

                            ItemDelegate {
                                width: parent.width
                                height: 28
                                leftPadding: 26
                                onClicked: if (!client.modelData.me) contextMenu.popup()
                                // Identidade real (Tailscale) fica discreta: só no hover/menu.
                                ToolTip.visible: hovered && !!client.modelData.identity
                                ToolTip.delay: 500
                                ToolTip.text: I18n.t("app.identity") + ": " + client.modelData.identity
                                contentItem: RowLayout {
                                    spacing: 7
                                    Rectangle { width: 9; height: 9; radius: 5; color: "transparent"; border.width: 2; border.color: Theme.online }
                                    Label {
                                        text: client.modelData.nickname
                                        color: Theme.text
                                        font.weight: client.modelData.me ? Font.DemiBold : Font.Normal
                                        elide: Text.ElideRight
                                        Layout.maximumWidth: 150
                                    }
                                    Label { visible: client.modelData.me; text: "(" + I18n.t("app.you") + ")"; color: Theme.textDim }
                                    Icon {
                                        visible: Settings.contacts.indexOf(client.modelData.uid) >= 0
                                        name: "star"; size: 13; color: Theme.gold
                                    }
                                    Item { Layout.fillWidth: true }
                                    Label {
                                        text: client.modelData.pingMs >= 0 ? Math.round(client.modelData.pingMs) + " ms" : "—"
                                        color: Theme.textDim
                                        font.pixelSize: 11
                                        ToolTip.visible: pingHover.hovered
                                        ToolTip.text: client.modelData.pingMs >= 0 ? "Ping desta pessoa até o servidor (RTT)" : "Ping não disponível neste modo ou aguardando medição"
                                        HoverHandler { id: pingHover }
                                    }
                                }
                                Menu {
                                    id: contextMenu
                                    MenuItem {
                                        visible: !!client.modelData.identity
                                        height: visible ? implicitHeight : 0
                                        enabled: false
                                        text: client.modelData.identity
                                    }
                                    MenuItem {
                                        text: Settings.contacts.indexOf(client.modelData.uid) >= 0 ? I18n.t("app.contacts.remove") : I18n.t("app.contacts.add")
                                        onTriggered: Settings.toggleContact(client.modelData.uid)
                                    }
                                }
                            }

                            Repeater {
                                model: client.modelData.streams
                                delegate: ItemDelegate {
                                    id: streamRow
                                    required property var modelData
                                    readonly property bool clickable: channel.modelData.current && !client.modelData.me
                                    width: client.width
                                    height: 26
                                    leftPadding: 44
                                    hoverEnabled: clickable
                                    onClicked: if (clickable) root.streamClicked(modelData.streamIdentifier)
                                    ToolTip.visible: hovered && !streamViewers.hovered
                                    ToolTip.text: modelData.name
                                    contentItem: RowLayout {
                                        spacing: 6
                                        Icon { name: root.typeIcon(streamRow.modelData.type); size: 14; color: Theme.textDim }
                                        Label {
                                            Layout.fillWidth: true
                                            text: streamRow.modelData.name
                                            color: Theme.textDim
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                        Rectangle {
                                            radius: 3
                                            color: Theme.live
                                            implicitWidth: liveLabel.implicitWidth + 10
                                            implicitHeight: 15
                                            Label {
                                                id: liveLabel
                                                anchors.centerIn: parent
                                                text: I18n.t("video.stream.live")
                                                color: "white"
                                                font.pixelSize: 9
                                                font.weight: Font.Bold
                                            }
                                        }
                                        ViewerCount {
                                            id: streamViewers
                                            streamId: streamRow.modelData.streamIdentifier
                                            count: streamRow.modelData.viewerCount
                                            own: streamRow.modelData.ownerClid === Server.ownClid
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
