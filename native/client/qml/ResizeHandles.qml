import QtQuick

// Bordas invisíveis que entregam o redimensionamento ao Windows (startSystemResize),
// mantendo o comportamento nativo do arrasto pelas bordas. Usado pela janela principal
// e por painéis que a cobrem inteira (as Preferências), que senão engoliriam o clique.
Item {
    id: root

    property Window window
    // Quais bordas (e cantos formados por elas) ganham alça.
    property int edges: Qt.LeftEdge | Qt.RightEdge | Qt.TopEdge | Qt.BottomEdge
    // Onde começa a alça de cima (a barra de título fica fora do contentItem).
    property real topOffset: 0
    readonly property int thickness: 5

    anchors.fill: parent
    z: 1000
    visible: !!window && window.visibility !== Window.Maximized && window.visibility !== Window.FullScreen

    Repeater {
        model: [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge,
                Qt.TopEdge | Qt.LeftEdge, Qt.TopEdge | Qt.RightEdge,
                Qt.BottomEdge | Qt.LeftEdge, Qt.BottomEdge | Qt.RightEdge]
                .filter(e => (e & root.edges) === e)
        delegate: MouseArea {
            required property int modelData
            readonly property bool edgeLeft: !!(modelData & Qt.LeftEdge)
            readonly property bool edgeRight: !!(modelData & Qt.RightEdge)
            readonly property bool edgeTop: !!(modelData & Qt.TopEdge)
            readonly property bool edgeBottom: !!(modelData & Qt.BottomEdge)
            x: edgeRight ? root.width - width : 0
            y: edgeTop ? root.topOffset : edgeBottom ? root.height - height : 0
            width: edgeLeft || edgeRight ? root.thickness : root.width
            height: edgeTop || edgeBottom ? root.thickness : root.height
            cursorShape: (edgeLeft || edgeRight) && (edgeTop || edgeBottom)
                         ? (edgeLeft === edgeTop ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                         : edgeLeft || edgeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: root.window.startSystemResize(modelData)
        }
    }
}
