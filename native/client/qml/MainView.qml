import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

Rectangle {
    id: root
    color: Theme.background

    // Painel lateral: { kind: "viewers" | "stats", id }
    property string panelKind: ""
    property string panelStream: ""

    function openPanel(kind, id) {
        if (panelKind === kind && panelStream === id) {
            closePanel()
            return
        }
        panelKind = kind
        panelStream = id
    }
    function closePanel() {
        panelKind = ""
        panelStream = ""
    }
    function openSetup(editId, sourceOnly) {
        setupDialog.editId = editId || ""
        setupDialog.sourceOnly = !!sourceOnly
        setupDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------ barra de título
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: Theme.contrast
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 10
                // Ícone do servidor que também oculta/mostra a lista de canais e usuários.
                ToolButton {
                    id: userListButton
                    objectName: "toggleUserList"
                    implicitWidth: 28
                    implicitHeight: 28
                    Layout.leftMargin: -6
                    Layout.rightMargin: -6
                    padding: 0
                    contentItem: Item {}
                    background: Rectangle {
                        radius: 6
                        color: userListButton.down ? Theme.border
                             : userListButton.hovered ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                             : "transparent"
                    }
                    Icon {
                        anchors.centerIn: parent
                        name: "server"
                        size: 16
                        color: userListButton.hovered ? Theme.text : Theme.textDim
                    }
                    readonly property string tip: Settings.showUserList ? "Ocultar lista de usuários (Ctrl+B)" : "Mostrar lista de usuários (Ctrl+B)"
                    Accessible.name: tip
                    ToolTip.visible: hovered
                    ToolTip.text: tip
                    hoverEnabled: true
                    onClicked: Settings.showUserList = !Settings.showUserList
                }
                Shortcut {
                    sequence: "Ctrl+B"
                    context: Qt.WindowShortcut
                    onActivated: Settings.showUserList = !Settings.showUserList
                }
                Label {
                    text: Server.serverName; color: Theme.textDim
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    ToolTip.visible: serverHover.hovered
                    ToolTip.text: text
                    HoverHandler { id: serverHover }
                }
                Label { visible: Server.reconnecting; text: "Reconectando…"; color: Theme.textDim }
                Label {
                    text: Server.serverPingMs >= 0 ? "Ping: " + Math.round(Server.serverPingMs) + " ms" : "Ping: —"
                    color: Theme.textDim
                    font.pixelSize: 12
                    ToolTip.visible: serverPingHover.hovered
                    ToolTip.text: "Seu ping até o servidor (RTT)"
                    HoverHandler { id: serverPingHover }
                }
                Rectangle { width: 8; height: 8; radius: 4; color: Server.reconnecting ? Theme.textDim : Theme.online }
                Label {
                    text: Server.clientName(Server.ownClid)
                    color: Theme.text
                }
                ToolButton {
                    objectName: "disconnect"
                    implicitWidth: 34
                    implicitHeight: 34
                    contentItem: Item {}
                    Icon { anchors.centerIn: parent; name: "power"; color: parent.hovered ? Theme.danger : Theme.text }
                    Accessible.name: I18n.t("app.disconnect")
                    ToolTip.visible: hovered
                    ToolTip.text: I18n.t("app.disconnect")
                    hoverEnabled: true
                    onClicked: Server.disconnectFromServer()
                }
            }
        }

        // ------------------------------------------------------ corpo
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Lista de canais/usuários; o botão da barra de título (ou Ctrl+B) oculta.
            Item {
                Layout.preferredWidth: Settings.showUserList ? 280 : 0
                Layout.fillHeight: true
                visible: Layout.preferredWidth > 0
                clip: true
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                ChannelTree {
                    width: 280
                    height: parent.height
                    onStreamClicked: function(id) { area.focusStream(id) }
                }
            }

            StreamArea {
                id: area
                Layout.fillWidth: true
                Layout.fillHeight: true
                onStartStreamRequested: root.openSetup("", false)
                onEditStreamRequested: function(id) { root.openSetup(id, false) }
                onChangeSourceRequested: function(id) { root.openSetup(id, true) }
                onViewersRequested: function(id) { root.openPanel("viewers", id) }
                onStatsRequested: function(id) { root.openPanel("stats", id) }
            }

            Rectangle {
                Layout.preferredWidth: root.panelKind ? 360 : 0
                Layout.fillHeight: true
                visible: Layout.preferredWidth > 0
                color: Theme.panel
                clip: true
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                Rectangle { width: 1; height: parent.height; color: Theme.border }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 1
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 14
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                text: root.panelKind === "viewers" ? I18n.t("video.action.manage_viewers") : I18n.t("video.stream.stats.title")
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                color: Theme.text
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: {
                                    const items = Screenshare.items
                                    for (let i = 0; i < items.length; ++i)
                                        if (items[i].streamIdentifier === root.panelStream)
                                            return items[i].name
                                    return ""
                                }
                                color: Theme.textDim
                            }
                        }
                        ToolButton {
                            padding: 3
                            contentItem: Item {}
        Icon { anchors.centerIn: parent; name: "close"; color: Theme.textDim }
                            onClicked: root.closePanel()
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: root.panelKind !== ""
                        sourceComponent: root.panelKind === "viewers" ? viewersComp : statsComp
                    }
                }
            }
        }
    }

    Component {
        id: viewersComp
        ViewersPanel { streamId: root.panelStream }
    }
    Component {
        id: statsComp
        StatsPanel { streamId: root.panelStream }
    }

    SetupStreamDialog {
        id: setupDialog
        anchors.centerIn: parent
    }

    // Depuração (--open): abre um painel/diálogo assim que houver um stream.
    property bool debugOpened: false
    Component.onCompleted: {
        if (UiSmoke && DebugOpen === "source") Qt.callLater(() => openSetup("ui-smoke", true))
        if (DebugOpen.indexOf("setup") === 0) Qt.callLater(() => {
            openSetup("", false)
        })
    }
    Connections {
        target: Screenshare
        enabled: !root.debugOpened && (DebugOpen === "stats" || DebugOpen === "viewers" || DebugOpen === "source" || DebugOpen === "settings")
        function onItemsChanged() {
            const items = Screenshare.items
            if (items.length > 0 && items[0].status === "live" || items.length > 0 && items[0].status === "playing") {
                root.debugOpened = true
                if (DebugOpen === "source" || DebugOpen === "settings")
                    root.openSetup(items[0].streamIdentifier, DebugOpen === "source")
                else root.openPanel(DebugOpen, items[0].streamIdentifier)
            }
        }
    }

    // Fecha o painel se o stream sumir.
    Connections {
        target: Screenshare
        function onItemsChanged() {
            if (!root.panelStream)
                return
            const items = Screenshare.items
            for (let i = 0; i < items.length; ++i)
                if (items[i].streamIdentifier === root.panelStream)
                    return
            root.closePanel()
        }
    }
}
