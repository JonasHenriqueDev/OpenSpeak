import QtQuick

// Banner do perfil. GIF só toca enquanto este item existe: fechado o card, nada anima.
Item {
    id: root
    property string source: ""
    clip: true

    Rectangle { anchors.fill: parent; color: Theme.panel3 }
    Loader {
        anchors.fill: parent
        active: !!root.source
        sourceComponent: root.source.startsWith("data:image/gif") ? animated : still
    }
    Component {
        id: still
        Image {
            source: root.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
        }
    }
    Component {
        id: animated
        AnimatedImage {
            source: root.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true // com cache: o data: URL não volta ao início, e sem ele o GIF toca uma vez só
        }
    }
}
