import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Preferências no estilo das configurações do Discord: ocupa a janela (abaixo da barra
// de título), com as categorias à esquerda e a página escolhida à direita. Cada página
// tem título, seções com cabeçalho e divisores, e linhas "texto + controle".
Popup {
    id: root

    property string page: "appearance"
    readonly property real topOffset: 34 // altura da barra de título do Main.qml
    signal quitRequested()

    function openAt(p) {
        page = p
        open()
    }

    parent: Overlay.overlay
    x: 0
    y: topOffset
    width: parent ? parent.width : 0
    height: parent ? parent.height - topOffset : 0
    // O FluentWinUI3 define padding e insets por lado; zera todos.
    padding: 0
    leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
    leftInset: 0; rightInset: 0; topInset: 0; bottomInset: 0
    margins: 0
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 1.02; to: 1; duration: 160; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
    }

    readonly property var categories: [
        { header: I18n.t("app.prefs.group.app") },
        { id: "appearance", label: I18n.t("app.prefs.section.appearance"), icon: "palette" },
        { id: "sounds", label: I18n.t("app.prefs.section.sounds"), icon: "volume" },
        { id: "watching", label: I18n.t("app.prefs.section.watching"), icon: "eye" },
        { id: "system", label: I18n.t("app.prefs.section.system"), icon: "pin" },
        { header: I18n.t("app.prefs.group.about") },
        { id: "about", label: I18n.t("app.prefs.section.about"), icon: "info" }
    ]
    readonly property var pages: ({
        appearance: appearancePage, sounds: soundsPage, watching: watchingPage,
        system: systemPage, about: aboutPage
    })

    background: Rectangle { color: Theme.background }

    contentItem: RowLayout {
        spacing: 0

        // ------------------------------------------------------------ categorias
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Math.max(220, Math.min(300, root.width * 0.26))
            color: Theme.contrast
            Flickable {
                anchors.fill: parent
                contentHeight: sidebar.implicitHeight + 40
                clip: true
                ColumnLayout {
                    id: sidebar
                    x: 16; y: 24
                    width: parent.width - 32
                    spacing: 2
                    Repeater {
                        model: root.categories
                        delegate: Loader {
                            required property var modelData
                            Layout.fillWidth: true
                            sourceComponent: modelData.header !== undefined ? sidebarHeader : sidebarItem
                            property var entry: modelData
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; Layout.bottomMargin: 8; height: 1; color: Theme.border }
                    SidebarButton {
                        objectName: "quitApp"
                        Layout.fillWidth: true
                        label: I18n.t("app.prefs.quit")
                        iconName: "power"
                        textColor: Theme.danger
                        onClicked: root.quitRequested()
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        leftPadding: 10
                        text: "OpenSpeak " + Updater.version + (Updater.edition === "lite" ? " Lite" : "")
                        color: Theme.textFaint
                        font.pixelSize: 11
                    }
                }
            }
        }

        // --------------------------------------------------------------- página
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                id: pageScroll
                anchors.fill: parent
                contentWidth: availableWidth
                clip: true
                Item {
                    width: pageScroll.availableWidth
                    implicitHeight: pageLoader.implicitHeight + 80
                    Loader {
                        id: pageLoader
                        objectName: "settingsPage"
                        x: 40; y: 40
                        width: Math.min(parent.width - 120, 720)
                        sourceComponent: root.pages[root.page] || appearancePage
                    }
                }
            }

            // Fechar (X + "ESC"), como no Discord.
            Column {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 40
                anchors.rightMargin: 24
                spacing: 6
                RoundButton {
                    id: closeButton
                    objectName: "closeSettings"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 36; height: 36
                    padding: 0
                    Accessible.name: I18n.t("app.prefs.close")
                    background: Rectangle {
                        radius: width / 2
                        color: closeButton.hovered ? Theme.panel3 : "transparent"
                        border.width: 2
                        border.color: closeButton.hovered ? Theme.textDim : Theme.textFaint
                    }
                    contentItem: Item { Icon { anchors.centerIn: parent; name: "close"; size: 16; color: Theme.textDim } }
                    onClicked: root.close()
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "ESC"
                    color: Theme.textFaint
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    // ------------------------------------------------------------------ peças

    component SidebarButton: AbstractButton {
        id: sideButton
        property string label
        property string iconName
        property bool selected: false
        property color textColor: selected || hovered ? Theme.text : Theme.textDim
        implicitHeight: 34
        hoverEnabled: true
        Accessible.name: label
        background: Rectangle {
            radius: 6
            color: sideButton.selected ? Theme.panel3 : sideButton.hovered ? Theme.panel2 : "transparent"
        }
        contentItem: RowLayout {
            spacing: 10
            Item { Layout.preferredWidth: 0 }
            Icon { name: sideButton.iconName; size: 16; color: sideButton.textColor }
            Label {
                Layout.fillWidth: true
                text: sideButton.label
                color: sideButton.textColor
                font.pixelSize: 14
                font.weight: sideButton.selected ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
        }
    }

    Component {
        id: sidebarHeader
        Label {
            topPadding: parent && parent.entry === root.categories[0] ? 0 : 14
            bottomPadding: 4
            leftPadding: 10
            text: parent ? parent.entry.header.toUpperCase() : ""
            color: Theme.textFaint
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 0.4
        }
    }
    Component {
        id: sidebarItem
        SidebarButton {
            objectName: "category-" + (parent ? parent.entry.id : "")
            label: parent ? parent.entry.label : ""
            iconName: parent ? parent.entry.icon : ""
            selected: parent ? root.page === parent.entry.id : false
            onClicked: root.page = parent.entry.id
        }
    }

    // Título da página.
    component PageTitle: Label {
        Layout.fillWidth: true
        Layout.bottomMargin: 8
        color: Theme.text
        font.pixelSize: 20
        font.weight: Font.Bold
    }
    // Cabeçalho de seção: maiúsculas pequenas, com divisor acima (exceto a primeira).
    component SectionHeader: ColumnLayout {
        property string text
        property bool first: false
        Layout.fillWidth: true
        Layout.topMargin: first ? 4 : 18
        spacing: 14
        Rectangle { visible: !parent.first; Layout.fillWidth: true; height: 1; color: Theme.border }
        Label {
            text: parent.text.toUpperCase()
            color: Theme.textFaint
            font.pixelSize: 12
            font.weight: Font.Bold
            font.letterSpacing: 0.4
        }
    }
    // Linha: título + descrição à esquerda, interruptor à direita.
    component SwitchRow: RowLayout {
        id: switchRow
        property string title
        property string description
        property alias checked: toggle.checked
        property alias switchObjectName: toggle.objectName
        signal toggled(bool checked)
        Layout.fillWidth: true
        Layout.topMargin: 4
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: switchRow.title
                color: switchRow.enabled ? Theme.text : Theme.textFaint
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: switchRow.description
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
        Switch {
            id: toggle
            Layout.alignment: Qt.AlignTop
            Accessible.name: switchRow.title
            onToggled: switchRow.toggled(checked)
        }
    }

    // ---------------------------------------------------------------- páginas

    Component {
        id: appearancePage
        ColumnLayout {
            spacing: 10
            PageTitle { text: I18n.t("app.prefs.section.appearance") }
            AppearanceSettings { Layout.fillWidth: true }
        }
    }

    Component {
        id: soundsPage
        ColumnLayout {
            spacing: 10
            PageTitle { text: I18n.t("app.prefs.section.sounds") }
            SectionHeader { first: true; text: I18n.t("app.prefs.sounds.effects") }
            SwitchRow {
                switchObjectName: "soundEffects"
                title: I18n.t("app.prefs.sounds")
                description: I18n.t("app.prefs.sounds.hint")
                checked: Settings.soundEffects
                onToggled: c => Settings.soundEffects = c
            }
            SectionHeader { text: I18n.t("app.prefs.sounds.theme") }
            RowLayout {
                objectName: "soundTheme"
                spacing: 12
                enabled: Settings.soundEffects
                RadioButton {
                    text: I18n.t("app.prefs.sounds.suave")
                    checked: Settings.soundTheme !== "bolha"
                    onClicked: { Settings.soundTheme = "suave"; Sounds.preview("suave", "entrar_sala") }
                }
                RadioButton {
                    text: I18n.t("app.prefs.sounds.bolha")
                    checked: Settings.soundTheme === "bolha"
                    onClicked: { Settings.soundTheme = "bolha"; Sounds.preview("bolha", "entrar_sala") }
                }
            }
            SectionHeader { text: I18n.t("app.prefs.sounds.volume") }
            RowLayout {
                Layout.fillWidth: true
                enabled: Settings.soundEffects
                spacing: 12
                AppSlider {
                    id: soundVolume
                    objectName: "soundVolume"
                    Layout.fillWidth: true
                    from: 0; to: 100; step: 5
                    value: Settings.soundVolume
                    onMoved: Settings.soundVolume = value
                    onPressedChanged: if (!pressed) Sounds.preview(Settings.soundTheme, "alguem_entrou")
                }
                Label {
                    Layout.preferredWidth: 40
                    horizontalAlignment: Text.AlignRight
                    text: Math.round(soundVolume.value) + "%"
                    color: Theme.textDim
                }
            }
        }
    }

    Component {
        id: watchingPage
        ColumnLayout {
            spacing: 10
            PageTitle { text: I18n.t("app.prefs.section.watching") }
            SectionHeader { first: true; text: I18n.t("app.prefs.section.audio") }
            SwitchRow {
                switchObjectName: "volumeBoost"
                title: I18n.t("app.prefs.volume_boost")
                description: I18n.t("app.prefs.volume_boost.hint")
                checked: Settings.volumeBoost
                onToggled: c => Settings.volumeBoost = c
            }
            SectionHeader { text: I18n.t("app.prefs.section.fullscreen") }
            SwitchRow {
                switchObjectName: "fullscreenTaskbar"
                title: I18n.t("app.prefs.fullscreen_taskbar")
                description: I18n.t("app.prefs.fullscreen_taskbar.hint")
                checked: Settings.fullscreenTaskbar
                onToggled: c => Settings.fullscreenTaskbar = c
            }
        }
    }

    Component {
        id: systemPage
        ColumnLayout {
            spacing: 10
            PageTitle { text: I18n.t("app.prefs.section.system") }
            SectionHeader { first: true; text: I18n.t("app.prefs.section.tray") }
            SwitchRow {
                switchObjectName: "minimizeToTray"
                enabled: Tray.available
                title: I18n.t("app.prefs.minimize_to_tray")
                description: Tray.available ? I18n.t("app.prefs.minimize_to_tray.hint") : I18n.t("app.prefs.tray_unavailable")
                checked: Settings.minimizeToTray
                onToggled: c => Settings.minimizeToTray = c
            }
            SectionHeader { text: I18n.t("app.prefs.section.startup") }
            SwitchRow {
                switchObjectName: "launchAtStartup"
                title: I18n.t("app.prefs.launch_at_startup")
                checked: Settings.launchAtStartup
                onToggled: c => Settings.launchAtStartup = c
            }
            SwitchRow {
                switchObjectName: "startMinimized"
                enabled: Settings.launchAtStartup && Tray.available
                title: I18n.t("app.prefs.start_minimized")
                description: I18n.t("app.prefs.start_minimized.hint")
                checked: Settings.startMinimized
                onToggled: c => Settings.startMinimized = c
            }
        }
    }

    Component {
        id: aboutPage
        ColumnLayout {
            spacing: 10
            PageTitle { text: I18n.t("app.prefs.section.about") }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                text: I18n.t("app.prefs.about.text")
            }
            Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; height: 1; color: Theme.border }
            Label {
                objectName: "licensesText"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.MarkdownText
                // Créditos + THIRD_PARTY_LICENSES.md (título rebaixado ao nível das seções).
                // A página é criada ao abrir, então o Markdown já pega a cor de link do tema.
                text: I18n.t("app.about.credits") + ThirdPartyLicenses.replace(/^# /, "## ")
                      .replace("](LICENSE)", "](https://github.com/JonasHenriqueDev/OpenSpeak/blob/main/LICENSE)")
                onLinkActivated: link => { if (/^https?:/.test(link)) Qt.openUrlExternally(link) }
            }
        }
    }
}
