import QtQuick
import QtQuick.Controls
import "I18n.js" as I18n

ApplicationWindow {
    id: window
    width: 1360
    height: 840
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: Server.connected ? "OpenSpeak — " + Server.serverName : "OpenSpeak"
    color: Theme.background

    Loader {
        anchors.fill: parent
        sourceComponent: Server.connected ? mainView : loginView
    }

    Component {
        id: loginView
        LoginView {
            message: window.loginMessage
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
        function onJoinRequest(id, clid, nickname) {
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
