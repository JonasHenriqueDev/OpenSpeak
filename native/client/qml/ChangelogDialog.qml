import QtQuick
import QtQuick.Controls
import "I18n.js" as I18n

// Novidades da versão, na primeira abertura depois de atualizar.
Dialog {
    id: root
    objectName: "changelogDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, parent.width - 80)
    height: Math.min(560, parent.height - 60)
    modal: true
    title: I18n.t("app.updated", { version: Updater.version })
    standardButtons: Dialog.Close

    contentItem: ScrollView {
        contentWidth: availableWidth
        clip: true
        Label {
            objectName: "changelogText"
            width: parent.width
            wrapMode: Text.WordWrap
            textFormat: Text.MarkdownText
            color: Theme.textDim
            // Mesmo recorte de Atualizações › Novidades: só a seção da versão atual.
            text: Changelog.split(/\r?\n(?=# )/)[0].replace(/^(#+) /gm, "##$1 ")
            onLinkActivated: link => Qt.openUrlExternally(link)
        }
    }
}
