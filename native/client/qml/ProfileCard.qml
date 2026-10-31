import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Card de perfil ao clicar em alguém na lista, como no Discord.
Popup {
    id: root
    property var person: ({})
    property var media: ({})
    property point anchorPoint

    function place() {
        x = Math.min(anchorPoint.x, Overlay.overlay.width - width - 8)
        y = Math.max(8, Math.min(anchorPoint.y, Overlay.overlay.height - implicitHeight - 8))
    }
    function openFor(client, anchor) {
        person = client
        media = {}
        anchorPoint = anchor.mapToItem(Overlay.overlay, anchor.width + 6, 0)
        place()
        open()
        Server.fetchProfileMedia(client.clid, client.mediaTag || "")
    }

    // Solta o GIF e a imagem pesada ao fechar.
    onClosed: media = {}

    Connections {
        target: Server
        function onProfileMediaReady(clid, tag, media) {
            // visible, não opened: com a mídia em cache a resposta chega ainda dentro do openFor.
            if (root.visible && clid === root.person.clid) {
                root.media = media
                Qt.callLater(root.place)
            }
        }
    }

    parent: Overlay.overlay
    width: 280
    padding: 16
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: Theme.radius; color: Theme.panel; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 8
        ProfileBanner {
            objectName: "profileCardBanner"
            visible: !!root.media.banner
            source: root.media.banner || ""
            Layout.fillWidth: true
            Layout.preferredHeight: 112
            Layout.leftMargin: -15
            Layout.rightMargin: -15
            Layout.topMargin: -15
        }
        Avatar {
            Layout.topMargin: root.media.banner ? -44 : 0
            source: root.person.avatar || ""
            animatedSource: root.media.avatarAnimated || ""
            name: root.person.name || ""
            size: 72
        }
        Label {
            objectName: "profileCardName"
            Layout.fillWidth: true
            text: (root.person.name || "") + (root.person.me ? " (" + I18n.t("app.you") + ")" : "")
            color: root.person.color || Theme.text
            font.pixelSize: 18
            font.weight: Font.Bold
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            visible: root.person.name !== root.person.nickname
            text: "@" + (root.person.nickname || "")
            color: Theme.textDim
            elide: Text.ElideRight
        }
        Rectangle {
            Layout.fillWidth: true
            visible: !!root.person.description
            height: 1
            color: Theme.border
        }
        Label {
            Layout.fillWidth: true
            visible: !!root.person.description
            text: I18n.t("app.profile.description").toUpperCase()
            color: Theme.textFaint
            font.pixelSize: 11
            font.weight: Font.Bold
        }
        Label {
            Layout.fillWidth: true
            visible: !!root.person.description
            text: root.person.description || ""
            color: Theme.text
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            visible: !!root.person.identity
            text: I18n.t("app.identity") + ": " + (root.person.identity || "")
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        Button {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: !root.person.me
            text: I18n.t(Settings.contacts.indexOf(root.person.uid) >= 0 ? "app.contacts.remove" : "app.contacts.add")
            onClicked: Settings.toggleContact(root.person.uid)
        }
    }
}
