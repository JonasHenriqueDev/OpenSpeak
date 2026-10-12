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

LayoutResult gridLayout(int count, double width, double height, double gap = 8, int offset = 0, int minHeight = MinTileHeight);
LayoutResult focusLayout(int count, int focusIndex, double width, double height, double gap = 8, int offset = 0);

/// Exposto à QML como singleton "StreamLayout".
class StreamLayout : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    /// Retorna {perPage, tiles:[{x,y,w,h,index,focused}]}.
    Q_INVOKABLE QVariantMap compute(int count, int focusIndex, double width, double height, double gap, int offset) const;
};

} // namespace openspeak
