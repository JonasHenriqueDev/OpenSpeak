import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Entrada: por padrão entra na malha da tailnet (sem servidor). O servidor
// dedicado fica como opção avançada.
Rectangle {
    id: root
    property string message: ""
    property bool busy: false
    property bool autoTried: false
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
        if (!nick.length || !tailnetReady)
            return
        Settings.nickname = nick
        Settings.connectionMode = "tailnet"
        busy = true
        message = I18n.t("app.connecting")
        Server.connectTailnet(nick)
    }
    function enterServer() {
        const nick = nickField.text.trim()
        if (!nick.length)
            return
        Settings.nickname = nick
        Settings.serverUrl = serverField.text.trim()
        Settings.connectionMode = "server"
        busy = true
        message = I18n.t("app.connecting")
        Server.connectTo(Settings.serverUrl, nick, Settings.uid)
    }
    function enterCloud() {
        const nick = nickField.text.trim()
        if (!nick.length || !tailnetReady) return
        Settings.nickname = nick
        Settings.connectionMode = "cloud"
        busy = true
        message = I18n.t("app.connecting")
        Server.connectCloud(nick, Settings.uid)
    }
    function tryAuto() {
        if (!AutoConnect || autoTried || !Settings.nickname.length)
            return
        if (Settings.connectionMode === "server") {
            autoTried = true
            enterServer()
        } else if (Settings.connectionMode !== "cloud" && tailnetReady) {
            autoTried = true
            enterTailnet()
        }
    }

    onMessageChanged: if (message !== I18n.t("app.connecting")) busy = false
    onTailnetReadyChanged: tryAuto()
    Component.onCompleted: tryAuto()

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
                onAccepted: root.tailnetReady ? root.enterTailnet() : undefined
            }

            // ------------------------------------------------ status da tailnet
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
                    RowLayout {
                        spacing: 8
                        Rectangle {
                            width: 9; height: 9; radius: 5
                            color: root.tailnetReady ? Theme.online : Theme.warn
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.weight: Font.DemiBold
                            color: Theme.text
                            text: root.tailnetReady ? I18n.t("tailnet.ready", { name: Server.tailnetName })
                                                    : I18n.t("tailnet." + root.status)
                        }
                    }
                    Label {
                        visible: root.tailnetReady
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textDim
                        font.pixelSize: 12
                        text: I18n.t("tailnet.identity", { identity: Server.tailnetIdentity, n: Server.tailnetOnline })
                    }
                    Label {
                        visible: !root.tailnetReady
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textDim
                        font.pixelSize: 12
                        text: I18n.t("tailnet." + root.status + ".hint")
                    }
                    Button {
                        visible: root.status === "not_installed"
                        text: I18n.t("tailnet.download")
                        onClicked: Qt.openUrlExternally("https://tailscale.com/download/windows")
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: I18n.t("app.enter")
                highlighted: true
                enabled: !root.busy && root.tailnetReady && nickField.text.trim().length > 0
                onClicked: root.enterTailnet()
            }

            // ---------------------------------------- servidor dedicado (avançado)
            Button {
                Layout.fillWidth: true
                text: "Sala web • Windows e navegadores"
                onClicked: cloudBox.visible = !cloudBox.visible
            }
            ColumnLayout {
                id: cloudBox
                Layout.fillWidth: true
                visible: Settings.connectionMode === "cloud"
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: "Vercel negocia a conexão. Vídeo e áudio usam sua tailnet."
                    wrapMode: Text.WordWrap
                    color: Theme.textDim
                }
                Button {
                    Layout.fillWidth: true
                    text: "Entrar na sala web"
                    enabled: !root.busy && root.tailnetReady && nickField.text.trim().length > 0
                    onClicked: root.enterCloud()
                }
            }

            Button {
                flat: true
                text: (serverBox.visible ? "▾  " : "▸  ") + I18n.t("app.dedicated_server")
                onClicked: serverBox.visible = !serverBox.visible
            }
            ColumnLayout {
                id: serverBox
                Layout.fillWidth: true
                visible: Settings.connectionMode === "server"
                spacing: 8
                TextField {
                    id: serverField
                    Layout.fillWidth: true
                    text: Settings.serverUrl
                    onAccepted: root.enterServer()
                }
                Button {
                    Layout.fillWidth: true
                    text: I18n.t("app.connect")
                    enabled: !root.busy && nickField.text.trim().length > 0
                    onClicked: root.enterServer()
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
