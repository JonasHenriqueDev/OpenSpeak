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
    visible: true
    title: Server.connected ? "OpenSpeak — " + Server.serverName : "OpenSpeak"
    color: Theme.background
    flags: Qt.Window | Qt.FramelessWindowHint

    header: Rectangle {
        height: 34
        color: Theme.contrast
        MouseArea {
            anchors.fill: parent
            anchors.rightMargin: 138
            onPressed: window.startSystemMove()
            onDoubleClicked: window.toggleMaximized()
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            spacing: 8
            Icon { name: "logo"; size: 16; color: Theme.accent }
            Label { text: "OpenSpeak"; color: Theme.textDim; font.pixelSize: 12; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
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
        anchors.fill: parent
        sourceComponent: Server.connected || UiSmoke ? mainView : loginView
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
