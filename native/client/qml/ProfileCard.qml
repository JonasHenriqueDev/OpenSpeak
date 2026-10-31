import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Card de perfil ao clicar em alguém na lista, como no Discord.
Popup {
    id: root
    property var person: ({})

    function openFor(client, anchor) {
        person = client
        const at = anchor.mapToItem(Overlay.overlay, anchor.width + 6, 0)
        x = Math.min(at.x, Overlay.overlay.width - width - 8)
        y = Math.max(8, Math.min(at.y, Overlay.overlay.height - implicitHeight - 8))
        open()
    }

    parent: Overlay.overlay
    width: 280
    padding: 16
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: Theme.radius; color: Theme.panel; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 8
        Avatar {
            source: root.person.avatar || ""
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
