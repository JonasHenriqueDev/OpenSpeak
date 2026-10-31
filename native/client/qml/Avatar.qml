import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// Foto redonda do perfil; sem foto, as iniciais do nome.
Item {
    id: root
    property string source: ""
    property string animatedSource: "" // GIF; só o card e as configurações passam, a lista fica estática
    property string name: ""
    property int size: 20
    implicitWidth: size
    implicitHeight: size

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.panel3
        visible: photo.status !== Image.Ready && gif.status !== Image.Ready
        Label {
            anchors.centerIn: parent
            text: (root.name || "?").trim().split(/\s+/).slice(0, 2).map(p => p[0]).join("").toUpperCase()
            color: Theme.text
            font.pixelSize: root.size * 0.4
            font.weight: Font.Bold
        }
    }
    Image {
        id: photo
        anchors.fill: parent
        source: root.source
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectCrop
        visible: false
    }
    Item {
        id: mask
        anchors.fill: parent
        layer.enabled: true
        visible: false
        Rectangle { anchors.fill: parent; radius: width / 2 }
    }
    MultiEffect {
        anchors.fill: parent
        source: photo
        visible: photo.status === Image.Ready
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1
    }
    // Por cima da foto, que fica como capa até o GIF carregar. Precisa estar visível:
    // escondido, o AnimatedImage não gera quadros para um efeito usar como fonte.
    AnimatedImage {
        id: gif
        anchors.fill: parent
        source: root.animatedSource
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
    }
}
