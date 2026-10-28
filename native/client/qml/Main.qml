import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

ApplicationWindow {
    id: window
    width: 1360
    height: 840
    minimumWidth: 900
    minimumHeight: 560
    // Aberto pelo Windows com "Iniciar minimizado na bandeja": começa escondido.
    visible: !StartHidden
    title: Server.connected ? "OpenSpeak — " + Server.serverName : "OpenSpeak"
    color: Theme.background
    // Sem moldura, mas com WS_SYSMENU/WS_MINIMIZEBOX: sem eles o Windows ignora o
    // clique no ícone da barra de tarefas e não minimiza o app.
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint

    header: Rectangle {
        height: 34
        color: Theme.contrast
        MouseArea {
            anchors.fill: parent
            anchors.rightMargin: 246
            // Maximizada: o arrasto só começa depois de alguns pixels, restaurando a
            // janela sob o cursor (como no Windows); assim o duplo clique continua livre.
            property point pressPoint
            property bool fromMaximized: false
            onPressed: mouse => {
                fromMaximized = window.visibility === Window.Maximized
                pressPoint = Qt.point(mouse.x, mouse.y)
                if (!fromMaximized) window.startSystemMove()
            }
            onPositionChanged: mouse => {
                if (!fromMaximized || !pressed) return
                if (Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y) < 6) return
                fromMaximized = false
                const ratio = mouse.x / width
                const cursor = mapToGlobal(mouse.x, mouse.y)
                window.showNormal()
                window.x = Math.round(cursor.x - ratio * window.width)
                window.y = Math.round(cursor.y - mouse.y)
                window.startSystemMove()
            }
            onReleased: fromMaximized = false
            onDoubleClicked: window.toggleMaximized()
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            spacing: 8
            Icon { name: "logo"; size: 16; color: Theme.accent }
            Label { text: "OpenSpeak"; color: Theme.textDim; font.pixelSize: 12; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            ToolButton {
                id: repoButton
                objectName: "repoLink"
                Layout.preferredWidth: 36
                Layout.preferredHeight: 34
                Accessible.name: "Repositório do OpenSpeak no GitHub"
                ToolTip.visible: hovered
                ToolTip.text: "Código-fonte no GitHub"
                contentItem: Item {
                    Icon { anchors.centerIn: parent; name: "github"; size: 14; color: repoButton.hovered ? Theme.textDim : Theme.textFaint }
                }
                onClicked: Qt.openUrlExternally("https://github.com/JonasHenriqueDev/OpenSpeak")
            }
            ToolButton {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 34
                Accessible.name: "Atualizações do OpenSpeak"
                ToolTip.visible: hovered
                ToolTip.text: "Atualizações · " + Updater.version
                // Centralizado: como contentItem direto, o ícone estica e fica achatado.
                contentItem: Item { Icon { anchors.centerIn: parent; name: "download"; size: 16 } }
                onClicked: updatesDialog.open()
            }
            ToolButton {
                objectName: "preferencesButton"
                Layout.preferredWidth: 36
                Layout.preferredHeight: 34
                Accessible.name: I18n.t("app.prefs.tooltip")
                ToolTip.visible: hovered
                ToolTip.text: I18n.t("app.prefs.tooltip")
                // Centralizado: como contentItem direto, o ícone estica e fica achatado.
                contentItem: Item { Icon { anchors.centerIn: parent; name: "settings"; size: 16 } }
                onClicked: preferencesDialog.open()
            }
            Repeater {
                model: ["minimize", "maximize", "close"]
                delegate: ToolButton {
                    id: windowButton
                    required property string modelData
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 34
                    padding: 0
                    Accessible.name: modelData === "minimize" ? "Minimizar" : modelData === "maximize" ? "Maximizar ou restaurar" : "Fechar"
                    background: Rectangle {
                        color: windowButton.hovered ? (windowButton.modelData === "close" ? Theme.danger : Theme.panel3) : "transparent"
                    }
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 10; height: windowButton.modelData === "minimize" ? 1 : 10
                            visible: windowButton.modelData !== "close"
                            color: windowButton.modelData === "minimize" ? Theme.text : "transparent"
                            border.width: windowButton.modelData === "maximize" ? 1 : 0
                            border.color: Theme.text
                        }
                        Icon { anchors.centerIn: parent; name: "close"; size: 16; visible: windowButton.modelData === "close" }
                    }
                    onClicked: {
                        if (modelData === "minimize") window.showMinimized()
                        else if (modelData === "maximize") window.toggleMaximized()
                        else window.close()
                    }
                }
            }
        }
    }
    function toggleMaximized() {
        if (visibility === Window.Maximized) showNormal()
        else showMaximized()
    }

    // ------------------------------------------------------ bandeja do sistema
    // O X esconde a janela na bandeja (Settings.minimizeToTray); "Sair" encerra de fato.
    property bool quitting: false
    property bool restoreMaximized: false
    onVisibilityChanged: function() {
        if (window.visibility === Window.Maximized) restoreMaximized = true
        else if (window.visibility === Window.Windowed) restoreMaximized = false
    }
    onClosing: function(close) {
        if (quitting) return
        if (Settings.minimizeToTray && Tray.visible) {
            close.accepted = false
            hideToTray()
            return
        }
        quitApp()
    }
    function hideToTray() {
        window.hide()
        if (!Settings.trayNoticeShown) {
            Tray.showMessage(I18n.t("app.tray.notice.title"), I18n.t("app.tray.notice.text"))
            Settings.trayNoticeShown = true
        }
    }
    function showFromTray() {
        if (!window.visible || window.visibility === Window.Minimized) {
            if (restoreMaximized) window.showMaximized()
            else window.showNormal()
        }
        window.raise()
        window.requestActivate()
    }
    function quitApp() {
        quitting = true
        // Encerra as transmissões antes da conexão (aboutToQuit desconecta do servidor).
        Screenshare.stopAllStreams()
        Qt.quit()
    }
    Binding { target: Tray; property: "visible"; value: Tray.available && !UiSmoke }
    Binding { target: Tray; property: "streaming"; value: Screenshare.streaming }
    Binding { target: Tray; property: "audioMuted"; value: Screenshare.audioMuted }
    Binding { target: Tray; property: "soundsMuted"; value: !Settings.soundEffects }
    Connections {
        target: Tray
        function onOpenRequested() { window.showFromTray() }
        function onMessageClicked() { window.showFromTray() }
        function onStopStreamingRequested() { Screenshare.stopAllStreams() }
        function onToggleMuteRequested() { Screenshare.audioMuted = !Screenshare.audioMuted }
        function onToggleSoundsRequested() { Settings.soundEffects = !Settings.soundEffects }
        function onSettingsRequested() { window.showFromTray(); preferencesDialog.open() }
        function onQuitRequested() { window.quitApp() }
    }
    Dialog {
        id: preferencesDialog
        objectName: "preferencesDialog"
        title: I18n.t("app.prefs.title")
        anchors.centerIn: parent
        width: Math.min(480, window.width - 40)
        height: Math.min(implicitHeight, parent.height - 40)
        modal: true
        standardButtons: Dialog.Close
        // Rola quando a janela é baixa demais para todas as seções.
        contentItem: ScrollView {
            id: preferencesScroll
            contentWidth: availableWidth
            ColumnLayout {
            width: preferencesScroll.availableWidth
            spacing: 10
            Label { text: I18n.t("app.prefs.section.sounds"); font.weight: Font.DemiBold }
            CheckBox {
                objectName: "soundEffects"
                text: I18n.t("app.prefs.sounds")
                checked: Settings.soundEffects
                onToggled: Settings.soundEffects = checked
            }
            RowLayout {
                objectName: "soundTheme"
                Layout.leftMargin: 24
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
            RowLayout {
                Layout.leftMargin: 24
                Layout.fillWidth: true
                enabled: Settings.soundEffects
                Label { text: I18n.t("app.prefs.sounds.volume"); color: Theme.textDim }
                AppSlider {
                    objectName: "soundVolume"
                    Layout.fillWidth: true
                    from: 0; to: 100; step: 5
                    value: Settings.soundVolume
                    onMoved: Settings.soundVolume = value
                    onPressedChanged: if (!pressed) Sounds.preview(Settings.soundTheme, "alguem_entrou")
                }
            }
            Label { Layout.topMargin: 6; text: I18n.t("app.prefs.section.watching"); font.weight: Font.DemiBold }
            CheckBox {
                objectName: "volumeBoost"
                text: I18n.t("app.prefs.volume_boost")
                checked: Settings.volumeBoost
                onToggled: Settings.volumeBoost = checked
            }
            Label { Layout.fillWidth: true; Layout.leftMargin: 24; wrapMode: Text.WordWrap; color: Theme.textDim
                text: I18n.t("app.prefs.volume_boost.hint") }
            CheckBox {
                objectName: "fullscreenTaskbar"
                text: I18n.t("app.prefs.fullscreen_taskbar")
                checked: Settings.fullscreenTaskbar
                onToggled: Settings.fullscreenTaskbar = checked
            }
            Label { Layout.fillWidth: true; Layout.leftMargin: 24; wrapMode: Text.WordWrap; color: Theme.textDim
                text: I18n.t("app.prefs.fullscreen_taskbar.hint") }
            Label { Layout.topMargin: 6; text: I18n.t("app.prefs.section.tray"); font.weight: Font.DemiBold }
            CheckBox {
                objectName: "minimizeToTray"
                text: I18n.t("app.prefs.minimize_to_tray")
                enabled: Tray.available
                checked: Settings.minimizeToTray
                onToggled: Settings.minimizeToTray = checked
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim
                text: Tray.available ? I18n.t("app.prefs.minimize_to_tray.hint") : I18n.t("app.prefs.tray_unavailable") }
            Label { Layout.topMargin: 6; text: I18n.t("app.prefs.section.startup"); font.weight: Font.DemiBold }
            CheckBox {
                objectName: "launchAtStartup"
                text: I18n.t("app.prefs.launch_at_startup")
                checked: Settings.launchAtStartup
                onToggled: Settings.launchAtStartup = checked
            }
            CheckBox {
                objectName: "startMinimized"
                Layout.leftMargin: 24
                text: I18n.t("app.prefs.start_minimized")
                enabled: Settings.launchAtStartup && Tray.available
                checked: Settings.startMinimized
                onToggled: Settings.startMinimized = checked
            }
            Label { Layout.fillWidth: true; Layout.leftMargin: 24; wrapMode: Text.WordWrap; color: Theme.textDim
                text: I18n.t("app.prefs.start_minimized.hint") }
            Label { Layout.topMargin: 6; text: I18n.t("app.prefs.section.changelog"); font.weight: Font.DemiBold }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim
                    text: I18n.t("app.prefs.changelog.version", { version: Updater.version }) }
                Button {
                    objectName: "openChangelog"
                    text: I18n.t("app.prefs.changelog.open")
                    onClicked: changelogDialog.open()
                }
            }
            Button {
                Layout.topMargin: 6
                text: I18n.t("app.prefs.quit")
                flat: true
                onClicked: window.quitApp()
            }
            }
        }
    }
    Dialog {
        id: changelogDialog
        objectName: "changelogDialog"
        title: I18n.t("app.changelog.title")
        anchors.centerIn: parent
        width: Math.min(620, window.width - 40)
        height: Math.min(560, parent.height - 40)
        modal: true
        standardButtons: Dialog.Close
        contentItem: ScrollView {
            id: changelogScroll
            contentWidth: availableWidth
            clip: true
            Label {
                width: changelogScroll.availableWidth
                wrapMode: Text.WordWrap
                textFormat: Text.MarkdownText
                text: Changelog
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }
    }
    Dialog {
        id: updatesDialog
        objectName: "updatesDialog"
        title: "Atualizações · OpenSpeak " + Updater.version + (Updater.edition === "lite" ? " Lite" : "")
        anchors.centerIn: parent
        width: Math.min(480, window.width - 40)
        // Em janelas baixas, as novidades encolhem (e rolam) para o diálogo caber.
        height: Math.min(implicitHeight, parent.height - 40)
        modal: true
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            objectName: "updatesContent"
            spacing: 12
            CheckBox {
                text: "Verificar também a cada 6 horas"
                checked: Settings.automaticUpdates
                onToggled: Settings.automaticUpdates = checked
            }
            CheckBox {
                text: "Receber pré-releases"
                checked: Settings.prereleaseUpdates
                onToggled: Settings.prereleaseUpdates = checked
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim
                text: Settings.prereleaseUpdates ? "Canal pré: releases estáveis e versões de teste mais novas." : "Canal estável: recebe somente releases, sem pré-releases." }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: Updater.status }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim
                text: Updater.installed ? "Ao abrir, o app baixa e instala atualizações automaticamente antes de conectar. A instalação por usuário preserva suas configurações." : "O app verifica ao abrir. Na versão portátil, baixe o novo instalador ou pacote na página de downloads." }
            RowLayout {
                Button { text: "Verificar agora"; enabled: !Updater.busy; onClicked: Updater.check() }
                Button { text: "Baixar atualização"; visible: Updater.installed && Updater.available && !Updater.ready; enabled: !Updater.busy; onClicked: Updater.downloadUpdate() }
                Button { text: "Aplicar e reiniciar"; visible: Updater.ready; onClicked: installDialog.open() }
            }
            Label { Layout.topMargin: 6; text: I18n.t("app.updates.changelog"); font.weight: Font.DemiBold }
            ScrollView {
                id: updatesChangelogScroll
                objectName: "updatesChangelog"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(updatesChangelogText.implicitHeight, 200)
                Layout.fillHeight: true
                Layout.minimumHeight: 48
                contentWidth: availableWidth
                clip: true
                Label {
                    id: updatesChangelogText
                    width: updatesChangelogScroll.availableWidth
                    wrapMode: Text.WordWrap
                    textFormat: Text.MarkdownText
                    color: Theme.textDim
                    // Só a seção da versão atual (até o próximo "# OpenSpeak").
                    text: Changelog.split(/\r?\n(?=# )/)[0]
                    onLinkActivated: link => Qt.openUrlExternally(link)
                }
            }
            RowLayout {
                Button { text: I18n.t("app.updates.changelog.all"); onClicked: changelogDialog.open() }
                Button { text: "Página de downloads"; flat: true; onClicked: Qt.openUrlExternally("https://github.com/JonasHenriqueDev/OpenSpeak/releases") }
            }
        }
    }
    Dialog {
        id: installDialog
        title: "Atualizar OpenSpeak"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "O app será fechado e reaberto após a atualização.\nAs conexões e transmissões atuais serão encerradas."; wrapMode: Text.WordWrap }
        onAccepted: Updater.installNow()
    }
    Component.onCompleted: {
        NativeWindow.enableSnapToTop(window)
        if (DebugOpen === "updates") Qt.callLater(() => updatesDialog.open())
        if (DebugOpen === "preferences") Qt.callLater(() => preferencesDialog.open())
        if (DebugOpen === "changelog") Qt.callLater(() => changelogDialog.open())
        if (JustUpdated) {
            const text = I18n.t("app.updated", { version: Updater.version })
            if (StartHidden)
                Tray.showMessage("OpenSpeak", text)
            Qt.callLater(() => toasts.show({
                key: "updated", kind: "info", icon: "download", timeout: 15000, text: text,
                actions: [{ label: I18n.t("app.updated.open"), primary: true, run: function() { changelogDialog.open() } }]
            }))
        }
        Tray.texts = {
            open: I18n.t("app.tray.open"),
            stop: I18n.t("app.tray.stop"),
            mute: I18n.t("app.tray.mute"),
            muteSounds: I18n.t("app.tray.mute_sounds"),
            settings: I18n.t("app.tray.settings"),
            quit: I18n.t("app.tray.quit"),
            tooltip: I18n.t("app.tray.tooltip"),
            tooltipStreaming: I18n.t("app.tray.tooltip.streaming")
        }

    }
    // Delegate resizing to Windows so edge drags retain native behavior.
    Repeater {
        model: [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge,
                Qt.TopEdge | Qt.LeftEdge, Qt.TopEdge | Qt.RightEdge,
                Qt.BottomEdge | Qt.LeftEdge, Qt.BottomEdge | Qt.RightEdge]
        delegate: MouseArea {
            required property int modelData
            readonly property bool edgeLeft: !!(modelData & Qt.LeftEdge)
            readonly property bool edgeRight: !!(modelData & Qt.RightEdge)
            readonly property bool edgeTop: !!(modelData & Qt.TopEdge)
            readonly property bool edgeBottom: !!(modelData & Qt.BottomEdge)
            parent: window.contentItem
            z: 1000
            visible: window.visibility !== Window.Maximized && window.visibility !== Window.FullScreen
            x: edgeRight ? parent.width - width : 0
            y: edgeTop ? -window.header.height : edgeBottom ? parent.height - height : 0
            width: edgeLeft || edgeRight ? 5 : parent.width
            height: edgeTop || edgeBottom ? 5 : parent.height
            cursorShape: (edgeLeft || edgeRight) && (edgeTop || edgeBottom)
                         ? (edgeLeft === edgeTop ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                         : edgeLeft || edgeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: window.startSystemResize(modelData)
        }
    }

    Loader {
        id: mainLoader
        anchors.fill: parent
        active: (UiSmoke && DebugOpen !== "startup") || (!UiSmoke && !Updater.startupBusy)
        sourceComponent: Server.connected || (UiSmoke && DebugOpen.indexOf("login") !== 0) ? mainView : loginView
    }
    Rectangle {
        objectName: "startupScreen"
        anchors.fill: parent
        color: Theme.background
        visible: UiSmoke ? DebugOpen === "startup" : Updater.startupBusy
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(420, parent.width - 40)
            spacing: 20
            Icon { Layout.alignment: Qt.AlignHCenter; name: "logo"; size: 54; color: Theme.accent }
            Label { Layout.alignment: Qt.AlignHCenter; text: "OpenSpeak"; font.pixelSize: 26; color: Theme.text }
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: parent.parent.visible }
            Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: Updater.status; color: Theme.textDim }
        }
    }

    Component {
        id: loginView
        LoginView {
            message: window.loginMessage
            onConnecting: window.loginMessage = I18n.t("app.connecting")
        }
    }
    Component {
        id: mainView
        MainView {}
    }

    property string loginMessage: ""

    Toasts {
        id: toasts
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 54
        anchors.rightMargin: 14
        z: 100
    }

    function errorText(key) {
        const t = I18n.t(key)
        return t
    }

    Connections {
        target: Server
        function onConnectFailed(error) { window.loginMessage = error }
        function onDisconnected(reason) { window.loginMessage = I18n.t("app.disconnected") }
        function onConnectionChanged() { if (Server.connected) window.loginMessage = "" }
    }

    Connections {
        target: Screenshare
        function onErrorOccurred(message) {
            toasts.show({ kind: "error", text: window.errorText(message) })
        }
        function onNoticeOccurred(key) {
            toasts.show({ key: key, kind: "info", text: I18n.t(key) })
        }
        function onJoinRequest(id, clid, nickname) {
            if (!window.visible || window.visibility === Window.Minimized)
                Tray.showMessage(I18n.t("app.tray.join_request.title"), I18n.t("video.join.request.notification", { user: nickname }))
            toasts.show({
                key: "join:" + id + ":" + clid,
                kind: "request",
                icon: "users",
                timeout: 30000,
                text: I18n.t("video.join.request.notification", { user: nickname })
                      + (Server.clientIdentity(clid) ? "
" + Server.clientIdentity(clid) : ""),
                actions: [
                    { label: I18n.t("video.join.request.accept"), primary: true,
                      run: function() { Screenshare.respondJoinRequest(id, clid, true, false) } },
                    { label: I18n.t("video.join.request.deny"),
                      run: function() { Screenshare.respondJoinRequest(id, clid, false, false) } },
                    { label: I18n.t("video.join.request.block"), danger: true,
                      run: function() { Screenshare.respondJoinRequest(id, clid, false, true) } }
                ]
            })
        }
        function onJoinRequestResolved(id, clid) { toasts.dismiss("join:" + id + ":" + clid) }
    }
}
