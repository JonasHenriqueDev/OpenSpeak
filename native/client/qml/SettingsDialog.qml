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
    // Atualizações › Novidades: só a versão atual ou o histórico inteiro.
    property bool allChangelog: false
    readonly property real topOffset: 34 // altura da barra de título do Main.qml
    signal quitRequested()
    signal installRequested()

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
        { id: "updates", label: I18n.t("app.prefs.section.updates"), icon: "download" },
        { id: "about", label: I18n.t("app.prefs.section.about"), icon: "info" }
    ]
    // Mesma ordem de SoundEffects.cpp / scripts/gen_sounds.py.
    readonly property var soundThemes: ["suave", "bolha", "cristal", "retro", "kalimba", "espacial", "discreto", "nostalgia"]
    readonly property var pages: ({
        appearance: appearancePage, sounds: soundsPage, watching: watchingPage,
        system: systemPage, updates: updatesPage, about: aboutPage
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
            // Cartões: clicar escolhe e toca uma prévia; o botão de play só ouve.
            Flow {
                objectName: "soundTheme"
                Layout.fillWidth: true
                spacing: 10
                enabled: Settings.soundEffects
                opacity: enabled ? 1 : 0.5
                Repeater {
                    model: root.soundThemes
                    delegate: AbstractButton {
                        id: soundCard
                        required property string modelData
                        readonly property bool selected: Settings.soundTheme === modelData
                            || (modelData === "suave" && root.soundThemes.indexOf(Settings.soundTheme) < 0)
                        width: 168; height: 58
                        hoverEnabled: true
                        Accessible.name: I18n.t("app.prefs.sounds." + modelData)
                        onClicked: { Settings.soundTheme = modelData; Sounds.preview(modelData, "entrar_sala") }
                        background: Rectangle {
                            radius: Theme.radius
                            color: soundCard.selected ? Theme.accentSoft : soundCard.hovered ? Theme.panel2 : Theme.panel
                            border.width: soundCard.selected ? 2 : 1
                            border.color: soundCard.selected ? Theme.accent : Theme.border
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            ColumnLayout {
                                Layout.leftMargin: 12
                                Layout.fillWidth: true
                                spacing: 1
                                Label {
                                    text: I18n.t("app.prefs.sounds." + soundCard.modelData)
                                    color: Theme.text
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: I18n.t("app.prefs.sounds." + soundCard.modelData + ".hint")
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                            ToolButton {
                                Layout.rightMargin: 4
                                Layout.preferredWidth: 30; Layout.preferredHeight: 30
                                Accessible.name: I18n.t("app.prefs.sounds.preview")
                                ToolTip.visible: hovered
                                ToolTip.text: I18n.t("app.prefs.sounds.preview")
                                contentItem: Item { Icon { anchors.centerIn: parent; name: "play"; size: 14; color: Theme.textDim } }
                                onClicked: Sounds.preview(soundCard.modelData, "entrar_sala")
                            }
                        }
                    }
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
        id: updatesPage
        ColumnLayout {
            objectName: "updatesContent"
            spacing: 10
            PageTitle { text: I18n.t("app.prefs.section.updates") }

            // Cartão com a versão instalada, o estado e as ações.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: versionCard.implicitHeight + 36
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border
                ColumnLayout {
                    id: versionCard
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Rectangle {
                            Layout.preferredWidth: 44; Layout.preferredHeight: 44
                            radius: 12
                            color: Theme.accentSoft
                            Icon { anchors.centerIn: parent; name: "logo"; size: 24; color: Theme.accent }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                text: "OpenSpeak " + Updater.version + (Updater.edition === "lite" ? " Lite" : "")
                                color: Theme.text
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }
                            Label {
                                objectName: "updateStatus"
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: Updater.status
                                color: Updater.available || Updater.ready ? Theme.accent : Theme.textDim
                            }
                        }
                        BusyIndicator {
                            Layout.preferredWidth: 28; Layout.preferredHeight: 28
                            visible: Updater.busy
                            running: visible
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            text: "Aplicar e reiniciar"
                            highlighted: true
                            visible: Updater.ready
                            onClicked: root.installRequested()
                        }
                        Button {
                            text: "Baixar atualização"
                            highlighted: true
                            visible: Updater.installed && Updater.available && !Updater.ready
                            enabled: !Updater.busy
                            onClicked: Updater.downloadUpdate()
                        }
                        Button { text: "Verificar agora"; enabled: !Updater.busy; onClicked: Updater.check() }
                        Button {
                            text: "Página de downloads"
                            flat: true
                            onClicked: Qt.openUrlExternally("https://github.com/JonasHenriqueDev/OpenSpeak/releases")
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: 12
                        text: Updater.installed
                              ? "Ao abrir, o app baixa e instala atualizações automaticamente antes de conectar. A instalação por usuário preserva suas configurações."
                              : "O app verifica ao abrir. Na versão portátil, baixe o novo instalador ou pacote na página de downloads."
                    }
                }
            }

            SectionHeader { text: I18n.t("app.updates.options") }
            SwitchRow {
                switchObjectName: "automaticUpdates"
                title: "Verificar também a cada 6 horas"
                description: "A verificação ao abrir o app acontece sempre."
                checked: Settings.automaticUpdates
                onToggled: c => Settings.automaticUpdates = c
            }
            SwitchRow {
                switchObjectName: "prereleaseUpdates"
                title: "Receber pré-releases"
                description: Settings.prereleaseUpdates
                             ? "Canal pré: releases estáveis e versões de teste mais novas."
                             : "Canal estável: recebe somente releases, sem pré-releases."
                checked: Settings.prereleaseUpdates
                onToggled: c => Settings.prereleaseUpdates = c
            }

            SectionHeader { text: root.allChangelog ? I18n.t("app.changelog.title") : I18n.t("app.updates.changelog") }
            Label {
                objectName: "updatesChangelog"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.MarkdownText
                color: Theme.textDim
                // Só a seção da versão atual (até o próximo "# OpenSpeak"), ou tudo.
                // A página é criada ao abrir, então o Markdown já pega a cor de link do tema.
                // Cabeçalhos rebaixados dois níveis para caber no tamanho da página.
                text: (root.allChangelog ? Changelog : Changelog.split(/\r?\n(?=# )/)[0]).replace(/^(#+) /gm, "##$1 ")
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
            Button {
                objectName: "toggleChangelog"
                flat: true
                text: root.allChangelog ? I18n.t("app.updates.changelog.current") : I18n.t("app.updates.changelog.all")
                onClicked: root.allChangelog = !root.allChangelog
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
