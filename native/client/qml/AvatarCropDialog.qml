import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "I18n.js" as I18n

// Ajuste da foto antes do envio: arrastar move, a roda do mouse ou o controle dão zoom.
// O recorte é um quadrado; (x, y, lado) saem em frações da imagem, como em avatarFromFile.
Popup {
    id: root
    signal cropped(url file, real x, real y, real side)
    signal failed()

    property url file
    readonly property int view: 280
    property real centerX: 0.5 // centro do recorte, em frações da imagem
    property real centerY: 0.5
    property real zoom: 1
    // Tamanho da imagem na tela: no zoom 1, o lado menor preenche o recorte.
    readonly property real baseScale: photo.implicitWidth > 0 ? view / Math.min(photo.implicitWidth, photo.implicitHeight) : 1
    readonly property real shownWidth: photo.implicitWidth * baseScale * zoom
    readonly property real shownHeight: photo.implicitHeight * baseScale * zoom

    function openFor(selected) {
        file = selected
        centerX = centerY = 0.5
        zoom = 1
        open()
    }
    function clamped(center, shown) {
        const half = view / 2 / shown
        return Math.max(half, Math.min(1 - half, center))
    }
    function setZoom(value) {
        zoom = Math.max(1, Math.min(4, value))
        centerX = clamped(centerX, shownWidth)
        centerY = clamped(centerY, shownHeight)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    z: 10
    padding: 20
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { radius: Theme.radius; color: Theme.panel; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 12
        Label { text: I18n.t("app.profile.photo.adjust"); color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
        Item {
            objectName: "avatarCropView"
            Layout.preferredWidth: root.view
            Layout.preferredHeight: root.view
            clip: true
            Image {
                id: photo
                source: root.file
                sourceSize: Qt.size(1280, 1280)
                autoTransform: true
                x: root.view / 2 - root.centerX * root.shownWidth
                y: root.view / 2 - root.centerY * root.shownHeight
                width: root.shownWidth
                height: root.shownHeight
                onStatusChanged: if (status === Image.Error && root.visible) { root.close(); root.failed() }
            }
            // Escurece o que fica fora do círculo (os cantos também: o aro passa da borda).
            Rectangle {
                readonly property real outer: root.view * Math.SQRT2
                anchors.centerIn: parent
                width: outer; height: outer
                radius: outer / 2
                color: "transparent"
                border.width: (outer - root.view) / 2
                border.color: Qt.rgba(0, 0, 0, 0.55)
            }
            MouseArea {
                property point last
                anchors.fill: parent
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                onPressed: mouse => last = Qt.point(mouse.x, mouse.y)
                onPositionChanged: mouse => {
                    root.centerX = root.clamped(root.centerX - (mouse.x - last.x) / root.shownWidth, root.shownWidth)
                    root.centerY = root.clamped(root.centerY - (mouse.y - last.y) / root.shownHeight, root.shownHeight)
                    last = Qt.point(mouse.x, mouse.y)
                }
                onWheel: wheel => root.setZoom(root.zoom * (wheel.angleDelta.y > 0 ? 1.1 : 1 / 1.1))
            }
        }
        RowLayout {
            spacing: 8
            Label { text: I18n.t("app.profile.photo.zoom"); color: Theme.textDim }
            AppSlider {
                objectName: "avatarZoom"
                Layout.fillWidth: true
                from: 1; to: 4
                value: root.zoom
                onMoved: root.setZoom(value)
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Button { flat: true; text: I18n.t("app.profile.photo.cancel"); onClicked: root.close() }
            Button {
                objectName: "avatarCropSave"
                highlighted: true
                enabled: photo.status === Image.Ready
                text: I18n.t("app.profile.photo.apply")
                onClicked: {
                    root.cropped(root.file, root.centerX - root.view / 2 / root.shownWidth,
                                 root.centerY - root.view / 2 / root.shownHeight, root.view / root.shownWidth)
                    root.close()
                }
            }
        }
    }
}
