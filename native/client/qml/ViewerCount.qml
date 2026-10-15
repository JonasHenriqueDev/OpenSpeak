import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Ícone + contagem de espectadores. Nas minhas transmissões, passar o mouse
// mostra quem está assistindo (lista local das sessões, não pública).
RowLayout {
    id: root
    property string streamId
    property int count: 0
    property bool own: false
    property color color: Theme.textDim
    property int iconSize: 13
    property int fontSize: 12
    readonly property bool hovered: hover.hovered
    property string tip: ""
    spacing: 4

    function refreshTip() {
        const names = (Screenshare.viewers(root.streamId).viewers || []).map(v => v.nickname)
        root.tip = names.length === 0
            ? I18n.t("video.viewers.active.empty")
            : I18n.t("video.viewers.active") + " (" + names.length + ")\n" + names.join("\n")
    }

    Icon { name: "users"; size: root.iconSize; color: root.color }
    Label { text: root.count; color: root.color; font.pixelSize: root.fontSize }

    HoverHandler {
        id: hover
        enabled: root.own
        onHoveredChanged: if (hovered) root.refreshTip()
    }
    Connections {
        target: Screenshare
        enabled: hover.hovered
        function onItemsChanged() { root.refreshTip() }
    }
    ToolTip.visible: hover.hovered
    ToolTip.delay: 250
    ToolTip.text: root.tip
}
