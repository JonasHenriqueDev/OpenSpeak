import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

Rectangle {
    id: root
    property string message: ""
    property bool busy: false
    property bool autoTried: false
    property bool advancedExpanded: false
    color: Theme.background
    readonly property string status: Server.tailnetStatus
    readonly property bool tailnetReady: status === "running"
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#18213a" }
            GradientStop { position: 0.7; color: Theme.background }
        }
    }
    function enterTailnet() {
        const nick = nickField.text.trim()
        if (!nick.length || !tailnetReady) return
        Settings.nickname = nick; Settings.connectionMode = "tailnet"
        busy = true; message = I18n.t("app.connecting")
        Server.connectTailnet(nick)
    }
    function enterServer() {
        // Capture todos antes de alterar Settings: os bindings reavaliam no changed.
        const nick = nickField.text.trim(), url = serverField.text.trim(), password = passwordField.text
        if (!nick.length || !url.length) return
        Settings.serverUrl = url; Settings.serverPassword = password; Settings.nickname = nick; Settings.connectionMode = "server"
        busy = true; message = I18n.t("app.connecting")
        Server.connectTo(url, nick, Settings.uid, password)
    }
    function enterCloud() {
        const nick = nickField.text.trim()
        if (!nick.length || !tailnetReady) return
        Settings.nickname = nick; Settings.connectionMode = "cloud"
        busy = true; message = I18n.t("app.connecting")
        Server.connectCloud(nick, Settings.uid)
    }
    function tryAuto() {
        if (!AutoConnect || autoTried || !Settings.nickname.length) return
        if (Settings.connectionMode === "server") { autoTried = true; enterServer() }
        else if (Settings.connectionMode === "tailnet" && tailnetReady && !LiteEdition) { autoTried = true; enterTailnet() }
    }
    onMessageChanged: if (message !== I18n.t("app.connecting")) busy = false
    onTailnetReadyChanged: tryAuto()
    Component.onCompleted: { if (DebugOpen === "login-advanced") advancedExpanded = true; tryAuto() }
    Pane {
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 32)
        height: Math.min(implicitHeight, root.height - 32)
        padding: 28
        background: Rectangle { radius: 12; color: Theme.panel; border.color: Theme.border }
        contentItem: Flickable {
            implicitHeight: loginContents.implicitHeight
            contentHeight: loginContents.implicitHeight
            contentWidth: width
            clip: true
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: loginContents
                width: parent.width
                spacing: 14
                RowLayout {
                    spacing: 12
                    Icon { name: "logo"; size: 40; color: Theme.accent }
                    Label { text: "OpenSpeak"; font.pixelSize: 26; font.weight: Font.DemiBold; color: Theme.text }
                }
                Label { text: I18n.t("app.display_name"); color: Theme.textDim }
                TextField {
                    id: nickField
                    Layout.fillWidth: true
                    text: Settings.nickname
                    placeholderText: "Ex.: Jonas"
                    maximumLength: 32
                    focus: true
                    onAccepted: root.enterServer()
                }
                Label { text: "Endereço do servidor"; color: Theme.textDim }
                TextField {
                    id: serverField
                    objectName: "serverAddress"
                    Layout.fillWidth: true
                    text: Settings.serverUrl
                    placeholderText: "wss://seu-servidor/ws"
                    onAccepted: root.enterServer()
                }
                Label { text: "Senha do servidor"; color: Theme.textDim }
                TextField {
                    id: passwordField
                    objectName: "serverPassword"
                    Layout.fillWidth: true
                    text: Settings.serverPassword
                    echoMode: TextInput.Password
                    placeholderText: "Deixe vazio se o servidor não tiver senha"
                    onAccepted: root.enterServer()
                }
                Button {
                    objectName: "serverConnectButton"
                    Layout.fillWidth: true
                    text: I18n.t("app.connect")
                    highlighted: true
                    enabled: !root.busy && nickField.text.trim().length > 0 && serverField.text.trim().length > 0
                    onClicked: root.enterServer()
                }
                Button {
                    objectName: "advancedToggle"
                    visible: !LiteEdition
                    flat: true
                    text: (root.advancedExpanded ? "▾  " : "▸  ") + "Avançado"
                    onClicked: root.advancedExpanded = !root.advancedExpanded
                }
                ColumnLayout {
                    objectName: "advancedOptions"
                    Layout.fillWidth: true
                    visible: root.advancedExpanded && !LiteEdition
                    spacing: 10
                    Rectangle {
                        Layout.fillWidth: true
                        radius: 8
                        color: root.tailnetReady ? Qt.rgba(0.07, 0.97, 0.55, 0.06) : Qt.rgba(0.96, 0.65, 0.14, 0.08)
                        border.color: root.tailnetReady ? Qt.rgba(0.07, 0.97, 0.55, 0.25) : Qt.rgba(0.96, 0.65, 0.14, 0.3)
                        implicitHeight: statusCol.implicitHeight + 20
                        ColumnLayout {
                            id: statusCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.weight: Font.DemiBold
                                text: root.tailnetReady ? I18n.t("tailnet.ready", {name:Server.tailnetName}) : I18n.t("tailnet." + root.status) }
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                                text: root.tailnetReady ? I18n.t("tailnet.identity", {identity:Server.tailnetIdentity,n:Server.tailnetOnline}) : I18n.t("tailnet." + root.status + ".hint") }
                            Button { visible: root.status === "not_installed"; text: I18n.t("tailnet.download"); onClicked: Qt.openUrlExternally("https://tailscale.com/download/windows") }
                        }
                    }
                    Button {
                        objectName: "tailnetConnectButton"
                        Layout.fillWidth: true
                        text: "Entrar via Tailscale"
                        enabled: !root.busy && root.tailnetReady && nickField.text.trim().length > 0
                        onClicked: root.enterTailnet()
                    }
                    Label { Layout.fillWidth: true; text: "Sala web: Windows e navegadores na mesma tailnet."; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12 }
                    Button {
                        objectName: "cloudConnectButton"
                        Layout.fillWidth: true
                        text: "Entrar na sala web"
                        enabled: !root.busy && root.tailnetReady && nickField.text.trim().length > 0
                        onClicked: root.enterCloud()
                    }
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.message.startsWith("tailnet_") || root.message === "listen_failed" ? I18n.t("tailnet.error." + root.message) : root.message
                    color: Theme.textDim
                    visible: text.length > 0
                }
            }
        }
    }
}
