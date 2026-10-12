import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Cartão com gráfico de linha (últimos 60 s), como as camadas de estatística do TS 6.
Rectangle {
    id: root
    property string title
    property string valueText
    property var samples: []   // números
    property real minimumMax: 1
    property int severity: 0

    implicitHeight: 82
    radius: 6
    color: Theme.contrast
    border.color: severity >= 2 ? Qt.rgba(0.94, 0.22, 0.29, 0.6) : severity === 1 ? Qt.rgba(0.96, 0.65, 0.14, 0.5) : Theme.border

    onSamplesChanged: canvas.requestPaint()
    onSeverityChanged: canvas.requestPaint()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.title; color: Theme.textDim; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            Label { text: root.valueText; color: Theme.text; font.pixelSize: 11 }
        }
        Canvas {
            id: canvas
            Layout.fillWidth: true
            Layout.fillHeight: true
            onPaint: {
                const ctx = getContext("2d")
                const w = width, h = height
                ctx.reset()
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.06)
                ctx.lineWidth = 1
                for (let i = 1; i < 4; ++i) {
                    const y = Math.round(h * i / 4) + 0.5
                    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke()
                }
                const values = root.samples
                if (!values || values.length < 2)
                    return
                const max = Math.max(root.minimumMax, ...values) * 1.15
                const step = w / 59
                const x0 = w - (values.length - 1) * step
                const color = Theme.severityColor(root.severity)
                ctx.beginPath()
                for (let i = 0; i < values.length; ++i) {
                    const x = x0 + i * step
                    const y = h - (values[i] / max) * (h - 4) - 2
                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                }
                ctx.strokeStyle = color
                ctx.lineWidth = 1.6
                ctx.stroke()
                ctx.lineTo(x0 + (values.length - 1) * step, h)
                ctx.lineTo(x0, h)
                ctx.closePath()
                ctx.globalAlpha = 0.15
                ctx.fillStyle = color
                ctx.fill()
                ctx.globalAlpha = 1
            }
        }
    }
}
