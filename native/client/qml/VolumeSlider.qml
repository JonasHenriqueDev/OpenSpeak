import QtQuick
import QtQuick.Controls

// Slider de volume (1 = 100%). Aceita roda do mouse e setas do teclado, e tem uma
// "trava" em 100%: para passar disso é preciso arrastar um pouco além ou insistir
// alguns cliques na roda/seta. volumeEdited() sai só de ações do usuário.
Slider {
    id: control

    signal volumeEdited(real volume)

    readonly property real detent: 1
    readonly property bool hasBoost: to > detent
    // Arrasto: quanto além de 100% o mouse precisa ir antes de soltar a trava.
    readonly property real dragDetent: (to - detent) * 0.08
    // Roda/setas: cliques extras parados em 100% antes de passar.
    readonly property int detentClicks: 3
    property int pushes: 0

    stepSize: 0.05
    wheelEnabled: false
    ToolTip.visible: hovered || pressed
    ToolTip.text: Math.round(value * 100) + "%"

    function emitValue(v) {
        v = Math.max(from, Math.min(to, Math.round(v * 100) / 100))
        if (v !== value) value = v
        volumeEdited(v)
    }

    function nudge(steps) {
        if (steps === 0) return
        let v = value + steps * stepSize
        if (hasBoost && steps > 0 && value <= detent + 0.001 && v > detent) {
            if (value < detent - 0.001) {
                pushes = 0
                v = detent
            } else if (++pushes < detentClicks) {
                return
            }
        } else {
            pushes = 0
        }
        if (hasBoost && steps < 0 && value > detent + 0.001 && v < detent) v = detent
        emitValue(v)
    }

    property bool startedBelow: true
    onPressedChanged: if (pressed) { startedBelow = value <= detent + 0.001; pushes = 0 }
    onMoved: {
        // Arrastando a partir de <= 100%: segura em 100% até passar da trava.
        if (hasBoost && startedBelow && value > detent && value < detent + dragDetent) {
            value = detent
        } else if (value > detent + dragDetent) {
            startedBelow = false
        } else if (value <= detent) {
            startedBelow = true
        }
        volumeEdited(value)
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Right) nudge(1)
        else if (event.key === Qt.Key_Down || event.key === Qt.Key_Left) nudge(-1)
        else if (event.key === Qt.Key_PageUp) nudge(4)
        else if (event.key === Qt.Key_PageDown) nudge(-4)
        else return
        event.accepted = true
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        property real acc: 0
        onWheel: event => {
            acc += event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
            const steps = Math.trunc(acc / 120)
            if (steps === 0) return
            acc -= steps * 120
            for (let i = 0; i < Math.abs(steps); ++i) control.nudge(steps > 0 ? 1 : -1)
            event.accepted = true
        }
    }
}
