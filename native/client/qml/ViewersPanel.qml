import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Gerenciar espectadores: pedidos, assistindo agora, bloqueados.
ScrollView {
    id: root
    property string streamId
    property var info: Screenshare.viewers(streamId)
    contentWidth: availableWidth
    clip: true

    Connections {
        target: Screenshare
        function onItemsChanged() { root.info = Screenshare.viewers(root.streamId) }
    }

    component SectionTitle: RowLayout {
        property string title
        property string count
        Layout.fillWidth: true
        Layout.topMargin: 6
        Label { text: parent.title.toUpperCase(); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.DemiBold }
        Rectangle {
            radius: 9
            color: Theme.panel3
            implicitWidth: countLabel.implicitWidth + 14
            implicitHeight: 18
            Label { id: countLabel; anchors.centerIn: parent; text: parent.parent.count; color: Theme.text; font.pixelSize: 11 }
        }
    }
    component Empty: Label { color: Theme.textDim; font.pixelSize: 13; leftPadding: 4 }
    component RowButton: ToolButton {
        padding: 3
        property string iconName
        property string tip
        property color iconColor: Theme.textDim
        implicitWidth: 30
        implicitHeight: 30
        contentItem: Item {}
        Icon { anchors.centerIn: parent; name: parent.iconName; size: 16; color: parent.iconColor }
        ToolTip.visible: hovered
        ToolTip.text: tip
    }

    ColumnLayout {
        width: parent.width
        spacing: 4

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 4

            SectionTitle { title: I18n.t("video.viewers.join_request"); count: (root.info.requests || []).length }
            Label {
                visible: !!root.info.limitReached && (root.info.requests || []).length > 0
                text: I18n.t("video.join.request.viewer_limit_reached")
                color: Theme.warn
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Repeater {
                model: root.info.requests || []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Icon { name: "user"; size: 16; color: Theme.textDim }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label { text: modelData.nickname; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label {
                            visible: !!modelData.identity
                            text: modelData.identity || ""
                            color: Theme.textFaint
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    RowButton {
                        iconName: "check"; iconColor: Theme.online; tip: I18n.t("video.join.request.accept")
                        enabled: !root.info.limitReached
                        onClicked: Screenshare.respondJoinRequest(root.streamId, modelData.clid, true, false)
                    }
                    RowButton {
                        iconName: "close"; tip: I18n.t("video.join.request.deny")
                        onClicked: Screenshare.respondJoinRequest(root.streamId, modelData.clid, false, false)
                    }
                    RowButton {
                        iconName: "ban"; iconColor: Theme.danger; tip: I18n.t("video.join.request.block.tooltip")
                        onClicked: Screenshare.respondJoinRequest(root.streamId, modelData.clid, false, true)
                    }
                }
            }
            Empty { visible: (root.info.requests || []).length === 0; text: I18n.t("video.viewers.join_request.empty") }

            SectionTitle {
                title: I18n.t("video.viewers.active")
                count: (root.info.viewers || []).length + (root.info.limit ? "/" + root.info.limit : "")
            }
            Repeater {
                model: root.info.viewers || []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: modelData.state === "connected" ? Theme.online : modelData.state === "failed" ? Theme.danger : Theme.warn
                        ToolTip.visible: stateHover.hovered
                        ToolTip.text: modelData.state
                        HoverHandler { id: stateHover }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label { text: modelData.nickname; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label {
                            visible: !!modelData.identity
                            text: modelData.identity || ""
                            color: Theme.textFaint
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        radius: 4
                        color: Theme.accentSoft
                        implicitWidth: codecLabel.implicitWidth + 10
                        implicitHeight: 18
                        Label { id: codecLabel; anchors.centerIn: parent; text: modelData.codec; color: Theme.accent; font.pixelSize: 11; font.weight: Font.DemiBold }
                    }
                    RowButton {
                        iconName: "kick"; tip: I18n.t("video.viewers.action.kick")
                        onClicked: Screenshare.removeViewer(root.streamId, modelData.clid, false)
                    }
                    RowButton {
                        iconName: "ban"; iconColor: Theme.danger; tip: I18n.t("video.viewers.action.ban")
                        onClicked: Screenshare.removeViewer(root.streamId, modelData.clid, true)
                    }
                }
            }
            Empty { visible: (root.info.viewers || []).length === 0; text: I18n.t("video.viewers.active.empty") }

            SectionTitle { title: I18n.t("video.viewers.banned"); count: (root.info.banned || []).length }
            Repeater {
                model: root.info.banned || []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Icon { name: "ban"; size: 16; color: Theme.textDim }
                    Label { text: modelData.nickname; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                    Button {
                        flat: true
                        text: I18n.t("video.viewers.action.unban")
                        onClicked: Screenshare.unban(root.streamId, modelData.uid)
                    }
                }
            }
            Empty { visible: (root.info.banned || []).length === 0; text: I18n.t("video.viewers.banned.empty") }
        }
    }
}
