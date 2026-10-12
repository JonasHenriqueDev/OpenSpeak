import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Notificações flutuantes (pedidos de entrada, erros).
Column {
    id: root
    width: 380
    spacing: 8

    ListModel { id: model }
    property var actionsByKey: ({})
    property int nextId: 1

    function show(o) {
        const key = o.key || ("t" + nextId++)
        dismiss(key)
        actionsByKey[key] = o.actions || []
        model.append({ key: key, kind: o.kind || "info", icon: o.icon || (o.kind === "error" ? "warning" : "info"),
                       text: o.text || "", timeout: o.timeout === undefined ? 6000 : o.timeout })
    }

    function dismiss(key) {
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i).key === key) {
                model.remove(i)
                break
            }
        }
        delete actionsByKey[key]
    }

    Repeater {
        model: model
        delegate: Pane {
            id: toast
            required property string key
            required property string kind
            required property string icon
            required property string text
            required property int timeout
            width: root.width
            padding: 12
            background: Rectangle {
                radius: Theme.radius
                color: Theme.panel2
                border.color: Theme.borderStrong
                Rectangle {
                    width: 3
                    height: parent.height
                    radius: 2
                    color: toast.kind === "error" ? Theme.danger : toast.kind === "request" ? Theme.warn : Theme.accent
                }
            }
            Timer {
                running: toast.timeout > 0
                interval: toast.timeout
                onTriggered: root.dismiss(toast.key)
            }
            ColumnLayout {
                width: parent.width
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Icon {
                        name: toast.icon
                        color: toast.kind === "error" ? Theme.danger : toast.kind === "request" ? Theme.warn : Theme.accent
                    }
                    Label {
                        Layout.fillWidth: true
                        text: toast.text
                        wrapMode: Text.WordWrap
                        color: Theme.text
                    }
                    ToolButton {
                        padding: 3
                        implicitWidth: 26
                        implicitHeight: 26
                        contentItem: Item {}
        Icon { anchors.centerIn: parent; name: "close"; size: 14; color: Theme.textDim }
                        onClicked: root.dismiss(toast.key)
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: (root.actionsByKey[toast.key] || []).length > 0
                    Repeater {
                        model: root.actionsByKey[toast.key] || []
                        delegate: Button {
                            required property var modelData
                            text: modelData.label
                            highlighted: !!modelData.primary
                            palette.buttonText: modelData.danger ? Theme.danger : Theme.text
                            onClicked: {
                                const fn = modelData.run
                                root.dismiss(toast.key)
                                fn()
                            }
                        }
                    }
                }
            }
        }
    }
}
