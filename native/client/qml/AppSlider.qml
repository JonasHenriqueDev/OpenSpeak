import QtQuick
import QtQuick.Controls

// Slider sem os traços de escala: o estilo FluentWinUI3 desenha um traço por passo
// sempre que stepSize divide o intervalo. Aqui stepSize fica em 0 e o passo (`step`)
// é aplicado à mão, com o mesmo arredondamento.
Slider {
    id: control

    property real step: 0

    function snap(v) {
        if (step <= 0) return v
        const s = from + Math.round((v - from) / step) * step
        return Math.max(Math.min(from, to), Math.min(Math.max(from, to), Math.round(s * 1e6) / 1e6))
    }

    stepSize: 0
    onValueChanged: {
        const s = snap(value)
        if (s !== value) value = s
    }
}
