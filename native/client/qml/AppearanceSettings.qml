import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Preferências › Aparência: tema de cores e cor de destaque (Settings.theme/accentColor).
ColumnLayout {
    id: root
    objectName: "appearanceSettings"
    spacing: 10

    Label { text: I18n.t("app.prefs.theme"); color: Theme.textDim }
    Flow {
        objectName: "themeChoice"
        Layout.fillWidth: true
        spacing: 10
        Repeater {
            model: Theme.themes
            delegate: AbstractButton {
                id: themeCard
                required property var modelData
                readonly property bool selected: Theme.current.id === modelData.id
                width: 112; height: 78
                hoverEnabled: true
                Accessible.name: modelData.label
                ToolTip.visible: hovered
                ToolTip.text: modelData.label
                onClicked: Settings.theme = modelData.id
                background: Rectangle {
                    radius: Theme.radius
                    color: themeCard.modelData.background
                    border.width: themeCard.selected ? 2 : 1
                    border.color: themeCard.selected ? Theme.accent : themeCard.hovered ? Theme.borderStrong : Theme.border
                }
                contentItem: Item {
                    // Miniatura: barra lateral, painel e um traço de destaque.
                    Rectangle {
                        x: 8; y: 8; width: 22; height: 36; radius: 3
                        color: themeCard.modelData.contrast
                    }
                    Rectangle {
                        x: 34; y: 8; width: parent.width - 42; height: 36; radius: 3
                        color: themeCard.modelData.panel2
                        border.color: themeCard.modelData.border
                        Rectangle {
                            x: 6; y: 8; width: parent.width * 0.6; height: 4; radius: 2
                            color: themeCard.modelData.text; opacity: 0.8
                        }
                        Rectangle {
                            x: 6; y: 18; width: parent.width * 0.4; height: 4; radius: 2
                            color: Theme.currentAccent ? Theme.accent : themeCard.modelData.accent
                        }
                    }
                    Label {
                        x: 8; y: 50
                        text: themeCard.modelData.label
                        font.pixelSize: 11
                        color: themeCard.modelData.text
                    }
                }
            }
        }
    }

    Label { Layout.topMargin: 4; text: I18n.t("app.prefs.accent"); color: Theme.textDim }
    Flow {
        objectName: "accentChoice"
        Layout.fillWidth: true
        spacing: 8
        Repeater {
            // Primeiro item: a cor padrão do tema.
            model: [{ id: "", label: I18n.t("app.prefs.accent.default"), color: Theme.current.accent }].concat(Theme.accents)
            delegate: AbstractButton {
                id: swatch
                required property var modelData
                readonly property bool selected: Settings.accentColor === modelData.id
                width: 30; height: 30
                hoverEnabled: true
                Accessible.name: modelData.label
                ToolTip.visible: hovered
                ToolTip.text: modelData.label
                onClicked: Settings.accentColor = modelData.id
                background: Rectangle {
                    radius: width / 2
                    color: "transparent"
                    border.width: swatch.selected ? 2 : swatch.hovered ? 1 : 0
                    border.color: swatch.selected ? Theme.text : Theme.borderStrong
                }
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: 22; height: 22; radius: 11
                        color: swatch.modelData.color
                        // "Padrão do tema" fica com um anel interno para se distinguir.
                        border.width: swatch.modelData.id === "" ? 2 : 0
                        border.color: Theme.background
                    }
                }
            }
        }
    }
}
