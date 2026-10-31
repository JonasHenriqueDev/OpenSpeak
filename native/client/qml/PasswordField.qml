import QtQuick
import QtQuick.Controls
import "I18n.js" as I18n

// Campo de senha com botão para mostrar o que foi digitado.
TextField {
    id: field
    property bool revealed: false
    echoMode: revealed ? TextInput.Normal : TextInput.Password
    rightPadding: 40

    ToolButton {
        objectName: "revealPassword"
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 30; height: 30
        padding: 0
        focusPolicy: Qt.NoFocus
        Accessible.name: I18n.t(field.revealed ? "app.password.hide" : "app.password.show")
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
        contentItem: Item { Icon { anchors.centerIn: parent; name: field.revealed ? "eyeOff" : "eye"; size: 16; color: Theme.textDim } }
        onClicked: field.revealed = !field.revealed
    }
}
