import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Ícone + contagem de espectadores. Nas minhas transmissões e nas que estou
// assistindo, passar o mouse mostra quem está assistindo (a lista só chega a quem
// transmite e a quem assiste; não é pública).
RowLayout {
    id: root
    property string streamId
    property int count: 0
    property color color: Theme.textDim
    property int iconSize: 13
    property int fontSize: 12
    readonly property bool hovered: hover.hovered && tip !== ""
    property string tip: ""
    spacing: 4

    function refreshTip() {
        const names = Screenshare.viewerNames(root.streamId)
        if (!names) {
            root.tip = ""
            return
        }
        root.tip = names.length === 0
            ? I18n.t("video.viewers.active.empty")
            : I18n.t("video.viewers.active") + " (" + names.length + ")\n" + names.join("\n")
    }

    Icon { name: "users"; size: root.iconSize; color: root.color }
    Label { text: root.count; color: root.color; font.pixelSize: root.fontSize }

    HoverHandler {
        id: hover
        onHoveredChanged: if (hovered) root.refreshTip()
    }
    Connections {
        target: Screenshare
        enabled: hover.hovered
        function onItemsChanged() { root.refreshTip() }
    }
    ToolTip.visible: root.hovered
    ToolTip.delay: 250
    ToolTip.text: root.tip
}
