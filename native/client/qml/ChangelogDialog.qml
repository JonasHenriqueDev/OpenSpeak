import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Novidades da versão, na primeira abertura depois de atualizar.
Dialog {
    id: root
    objectName: "changelogDialog"
    property bool all: false
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, parent.width - 80)
    height: Math.min(560, parent.height - 60)
    modal: true
    title: all ? I18n.t("app.changelog.title") : I18n.t("app.updated", { version: Updater.version })
    standardButtons: Dialog.Close

    contentItem: ScrollView {
        id: scroll
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 8
            Label {
                objectName: "changelogText"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.MarkdownText
                color: Theme.textDim
                // Mesmo recorte de Atualizações › Novidades: a seção da versão atual, ou tudo.
                text: (root.all ? Changelog : Changelog.split(/\r?\n(?=# )/)[0]).replace(/^(#+) /gm, "##$1 ")
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
            Button {
                objectName: "changelogToggle"
                flat: true
                text: root.all ? I18n.t("app.updates.changelog.current") : I18n.t("app.updates.changelog.all")
                onClicked: root.all = !root.all
            }
        }
    }
}
