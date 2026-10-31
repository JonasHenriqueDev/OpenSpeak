#pragma once
// Layouts da área de streams (estratégia do TS 6, análise §5):
//  - grade: maximiza a área mantendo 16:9 e altura mínima; pagina se não couber;
//  - foco: um stream grande e uma faixa de miniaturas abaixo.
// Porta de public/js/video/layout.js.

#include <QList>
#include <QObject>
#include <QRectF>
#include <QVariantMap>

namespace openspeak {

struct LayoutTile {
    QRectF rect;
    int index = 0;
    bool focused = false;
};

struct LayoutResult {
    QList<LayoutTile> tiles;
    int perPage = 0;
};

constexpr double LayoutAspect = 16.0 / 9.0;
constexpr int MinTileHeight = 100;
constexpr double MinCustomWidth = 160;

LayoutResult gridLayout(int count, double width, double height, double gap = 8, int offset = 0, int minHeight = MinTileHeight);
LayoutResult focusLayout(int count, int focusIndex, double width, double height, double gap = 8, int offset = 0);
/// Move um preview sem deixá-lo sair da área.
QRectF moveRect(const QRectF &rect, double dx, double dy, double width, double height);
/// Redimensiona por um canto (left/top dizem qual), mantendo a proporção e o canto oposto no lugar.
QRectF resizeRect(const QRectF &rect, bool left, bool top, double dx, double dy, double width, double height, double minWidth = MinCustomWidth);
/// Volta às coordenadas da área um preview guardado em frações, sempre inteiro dentro dela.
QRectF rectFromFraction(double fx, double fy, double fw, double aspect, double width, double height);

/// Exposto à QML como singleton "StreamLayout".
class StreamLayout : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    /// Retorna {perPage, tiles:[{x,y,w,h,index,focused}]}.
    Q_INVOKABLE QVariantMap compute(int count, int focusIndex, double width, double height, double gap, int offset) const;
    /// Os retângulos abaixo são {x,y,w,h}, como os de compute.
    Q_INVOKABLE QVariantMap moveRect(const QVariantMap &rect, double dx, double dy, double width, double height) const;
    Q_INVOKABLE QVariantMap resizeRect(const QVariantMap &rect, bool left, bool top, double dx, double dy, double width, double height) const;
    /// Guarda um preview em frações da área, para acompanhar o tamanho da janela.
    Q_INVOKABLE QVariantMap rectToFraction(const QVariantMap &rect, double width, double height) const;
    Q_INVOKABLE QVariantMap rectFromFraction(const QVariantMap &fraction, double width, double height) const;
};

} // namespace openspeak
