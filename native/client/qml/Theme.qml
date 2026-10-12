pragma Singleton
import QtQuick

// Cores próprias do OpenSpeak sobre o estilo FluentWinUI3 (tema escuro).
// Base extraída do TS 6: contraste #131824, tint #16bcfb, online #13f78d, dourado #daa520.
QtObject {
    readonly property color background: "#0f131c"
    readonly property color contrast: "#131824"
    readonly property color panel: "#171d2b"
    readonly property color panel2: "#1c2333"
    readonly property color panel3: "#232b3e"
    readonly property color border: "#262e42"
    readonly property color borderStrong: "#333d55"
    readonly property color text: "#e3e7ef"
    readonly property color textDim: "#9aa3b5"
    readonly property color textFaint: "#6b7489"
    readonly property color accent: "#16bcfb"
    readonly property color accentSoft: Qt.rgba(0.086, 0.737, 0.984, 0.14)
    readonly property color online: "#13f78d"
    readonly property color gold: "#daa520"
    readonly property color live: "#f0384b"
    readonly property color warn: "#f5a623"
    readonly property color danger: "#f0384b"
    readonly property int radius: 8

    function severityColor(sev) {
        return sev >= 2 ? danger : sev === 1 ? warn : accent
    }
}
