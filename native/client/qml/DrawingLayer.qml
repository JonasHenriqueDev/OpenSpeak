import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Desenhos dos espectadores por cima do vídeo (STREAM_DRAW). As coordenadas são
// relativas ao quadro do vídeo (contentRect do VideoOutput), então o traço cai
// no mesmo ponto da imagem para todo mundo, em qualquer tamanho de janela.
// Quem transmite não recebe os desenhos: esta camada só existe nos assistidos.
Item {
    id: layer
    property string streamId
    // Área do vídeo dentro da camada (sem as faixas pretas).
    property rect content: Qt.rect(0, 0, width, height)
    property bool drawMode: false
    // Distância da barra de ferramentas ao topo (abaixo da barra do quadro).
    property real toolbarTop: 44

    signal pointerMoved()
    signal finished()

    readonly property var drawColors: ["#ff3b30", "#ffcc00", "#34c759", "#00c7ff", "#0a84ff", "#ff2d92", "#ffffff", "#000000"]

    onContentChanged: canvas.requestPaint()
    onVisibleChanged: if (visible) canvas.requestPaint()

    Connections {
        target: Screenshare
        function onDrawingsChanged(id) {
            if (id === layer.streamId && layer.visible)
                canvas.requestPaint()
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const r = layer.content
            if (r.width <= 0 || r.height <= 0)
                return
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            const strokes = Screenshare.drawings(layer.streamId)
            const X = v => r.x + v / 10000 * r.width
            const Y = v => r.y + v / 10000 * r.height
            for (let s = 0; s < strokes.length; ++s) {
                const st = strokes[s]
                const p = st.points
                if (p.length < 2)
                    continue
                // Opacidade do ponto pela idade, em degraus: trechos vizinhos com o mesmo
                // degrau saem num caminho só (sem as emendas mais escuras do fade).
                const level = i => {
                    const age = st.ages[i] || 0
                    const a = age <= st.hold ? 1 : Math.max(0, 1 - (age - st.hold) / st.fade)
                    return Math.ceil(a * 16) / 16
                }
                const w = Math.max(2.5, st.width / 1000 * r.height)
                ctx.strokeStyle = st.color
                ctx.fillStyle = st.color
                ctx.lineWidth = w
                if (p.length === 2) {
                    ctx.globalAlpha = level(0)
                    ctx.beginPath()
                    ctx.arc(X(p[0]), Y(p[1]), w / 2, 0, 2 * Math.PI)
                    ctx.fill()
                    continue
                }
                const n = p.length / 2
                let i = 0
                while (i < n - 1) {
                    const a = level(i)
                    let j = i + 1
                    while (j < n - 1 && level(j) === a)
                        ++j
                    if (a > 0) {
                        ctx.globalAlpha = a
                        ctx.beginPath()
                        ctx.moveTo(X(p[2 * i]), Y(p[2 * i + 1]))
                        for (let k = i + 1; k <= j; ++k)
                            ctx.lineTo(X(p[2 * k]), Y(p[2 * k + 1]))
                        ctx.stroke()
                    }
                    i = j
                }
            }
            ctx.globalAlpha = 1
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: layer.drawMode
        hoverEnabled: enabled
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        function drawAt(mouse, start) {
            const r = layer.content
            if (r.width > 0 && r.height > 0)
                Screenshare.drawTo(layer.streamId, (mouse.x - r.x) / r.width, (mouse.y - r.y) / r.height, start)
        }
        onPressed: mouse => drawAt(mouse, true)
        onPositionChanged: mouse => {
            layer.pointerMoved()
            if (pressed)
                drawAt(mouse, false)
        }
        onReleased: Screenshare.endDrawing(layer.streamId)
        onCanceled: Screenshare.endDrawing(layer.streamId)
    }

    // Paleta e ações enquanto desenha.
    Rectangle {
        objectName: "drawToolbar"
        visible: layer.drawMode
        anchors.horizontalCenter: parent.horizontalCenter
        y: layer.toolbarTop
        radius: height / 2
        color: Qt.rgba(0.05, 0.06, 0.09, 0.88)
        border.color: Theme.border
        implicitWidth: toolbarRow.implicitWidth + 16
        implicitHeight: 38
        RowLayout {
            id: toolbarRow
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: layer.drawColors
                delegate: Rectangle {
                    required property string modelData
                    readonly property bool current: Settings.drawColor.toLowerCase() === modelData
                    width: 22
                    height: 22
                    radius: 11
                    color: modelData
                    border.width: current ? 3 : 1
                    border.color: current ? Theme.accent : Qt.rgba(1, 1, 1, 0.35)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Settings.drawColor = parent.modelData
                    }
                }
            }
            Rectangle { width: 1; height: 22; color: Theme.border }
            ToolButton {
                padding: 3
                implicitWidth: 30
                implicitHeight: 30
                contentItem: Item {}
                Icon { anchors.centerIn: parent; name: "eraser"; color: "white"; size: 17 }
                ToolTip.visible: hovered
                ToolTip.text: I18n.t("video.draw.clear_mine")
                onClicked: Screenshare.clearMyDrawings(layer.streamId)
            }
        }
    }
}
