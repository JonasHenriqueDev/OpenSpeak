pragma Singleton
import QtQuick

// Cores próprias do OpenSpeak sobre o estilo FluentWinUI3.
// Base ("meia-noite") extraída do TS 6: contraste #131824, tint #16bcfb, online #13f78d, dourado #daa520.
// `name` e `accentName` vêm de Settings.theme/accentColor (Binding no Main.qml).
QtObject {
    id: theme

    property string name: "meia-noite"
    property string accentName: "" // vazio = destaque padrão do tema

    // Ordem de exibição em Preferências › Aparência.
    readonly property var themes: [
        { id: "meia-noite", label: "Meia-noite", light: false, accent: "#16bcfb",
          background: "#0f131c", contrast: "#131824", panel: "#171d2b", panel2: "#1c2333", panel3: "#232b3e",
          border: "#262e42", borderStrong: "#333d55", text: "#e3e7ef", textDim: "#9aa3b5", textFaint: "#6b7489", glow: "#18213a" },
        { id: "grafite", label: "Grafite", light: false, accent: "#4c9dff",
          background: "#111111", contrast: "#161616", panel: "#1b1b1b", panel2: "#222222", panel3: "#2b2b2b",
          border: "#2f2f2f", borderStrong: "#404040", text: "#e8e8e8", textDim: "#a6a6a6", textFaint: "#737373", glow: "#1d1d1d" },
        { id: "oled", label: "Preto OLED", light: false, accent: "#16bcfb",
          background: "#000000", contrast: "#050505", panel: "#0a0a0a", panel2: "#111111", panel3: "#1a1a1a",
          border: "#1f1f1f", borderStrong: "#2e2e2e", text: "#e6e6e6", textDim: "#9c9c9c", textFaint: "#6a6a6a", glow: "#000000" },
        { id: "ameixa", label: "Ameixa", light: false, accent: "#b07cff",
          background: "#14101c", contrast: "#191423", panel: "#1e182a", panel2: "#251e34", panel3: "#2e2640",
          border: "#332a46", borderStrong: "#44385b", text: "#ece6f5", textDim: "#a99fbd", textFaint: "#776d8c", glow: "#271c3c" },
        { id: "floresta", label: "Floresta", light: false, accent: "#2fd47f",
          background: "#0d1411", contrast: "#111a16", panel: "#15201b", panel2: "#1a2822", panel3: "#21322a",
          border: "#25362e", borderStrong: "#32483e", text: "#e2eee8", textDim: "#98ada3", textFaint: "#6a7f75", glow: "#14281f" },
        { id: "claro", label: "Claro", light: true, accent: "#0a84c6",
          background: "#f2f4f8", contrast: "#e4e8ef", panel: "#ffffff", panel2: "#f6f7fa", panel3: "#e8ecf3",
          border: "#d8dde7", borderStrong: "#c1c8d6", text: "#1b2130", textDim: "#525d72", textFaint: "#848da0", glow: "#dfe8f6" }
    ]
    readonly property var accents: [
        { id: "ciano", label: "Ciano", color: "#16bcfb" },
        { id: "azul", label: "Azul", color: "#4c7dff" },
        { id: "roxo", label: "Roxo", color: "#9b6bff" },
        { id: "rosa", label: "Rosa", color: "#ff5fa2" },
        { id: "vermelho", label: "Vermelho", color: "#f0384b" },
        { id: "laranja", label: "Laranja", color: "#ff8a3d" },
        { id: "dourado", label: "Dourado", color: "#daa520" },
        { id: "verde", label: "Verde", color: "#22c55e" }
    ]

    function find(list, id) {
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id) return list[i]
        return null
    }
    readonly property var current: find(themes, name) || themes[0]
    readonly property var currentAccent: find(accents, accentName)

    readonly property bool light: current.light
    readonly property color background: current.background
    readonly property color contrast: current.contrast
    readonly property color panel: current.panel
    readonly property color panel2: current.panel2
    readonly property color panel3: current.panel3
    readonly property color border: current.border
    readonly property color borderStrong: current.borderStrong
    readonly property color text: current.text
    readonly property color textDim: current.textDim
    readonly property color textFaint: current.textFaint
    // Brilho do topo da tela de login.
    readonly property color glow: current.glow
    readonly property color accent: currentAccent ? currentAccent.color : current.accent
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, 0.14)
    // Texto sobre um fundo `accent` (botão "Ao vivo", marcas de seleção).
    readonly property color onAccent: 0.2126 * accent.r + 0.7152 * accent.g + 0.0722 * accent.b > 0.5 ? "#09131c" : "#ffffff"
    readonly property color online: light ? "#0b9f5a" : "#13f78d"
    readonly property color gold: "#daa520"
    readonly property color live: "#f0384b"
    readonly property color warn: "#f5a623"
    readonly property color danger: "#f0384b"
    readonly property int radius: 8

    function severityColor(sev) {
        return sev >= 2 ? danger : sev === 1 ? warn : accent
    }
}
