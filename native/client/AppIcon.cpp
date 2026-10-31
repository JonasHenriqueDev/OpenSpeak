#include "AppIcon.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

// Coordenadas em 512×512, as mesmas de public/favicon.svg.
QPainterPath bubblePath()
{
    QPainterPath p;
    p.moveTo(130, 176);
    p.lineTo(382, 176);
    p.arcTo(302, 176, 160, 160, 90, -90);
    p.lineTo(462, 352);
    p.arcTo(302, 272, 160, 160, 0, -90);
    p.lineTo(200, 432);
    p.lineTo(92, 486);
    p.lineTo(104, 426);
    p.cubicTo(72, 415, 50, 386, 50, 352);
    p.lineTo(50, 256);
    p.arcTo(50, 176, 160, 160, 180, -90);
    p.closeSubpath();
    return p;
}

QPainterPath eyePath()
{
    QPainterPath p;
    p.moveTo(122, 250);
    p.lineTo(232, 304);
    p.lineTo(232, 334);
    p.arcTo(156, 296, 76, 76, 0, -90);
    p.lineTo(160, 372);
    p.arcTo(122, 296, 76, 76, 270, -90);
    p.closeSubpath();
    return p;
}

QPixmap renderSize(int size, const QColor &accent)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 512.0, size / 512.0);

    painter.save();
    painter.translate(256, 190);
    painter.scale(1.25, 1.15);
    painter.translate(-256, -190);
    painter.setPen(QPen(accent, 34, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    QPainterPath antennas;
    antennas.moveTo(250, 170);
    antennas.cubicTo(236, 150, 206, 140, 190, 128);
    antennas.moveTo(262, 170);
    antennas.cubicTo(286, 140, 330, 105, 372, 62);
    painter.drawPath(antennas);
    painter.restore();

    painter.setPen(QPen(accent, 38, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(bubblePath());

    const QPainterPath eye = eyePath();
    painter.setPen(QPen(accent, 18, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(accent);
    painter.drawPath(eye);
    painter.translate(512, 0);
    painter.scale(-1, 1);
    painter.drawPath(eye);
    painter.end();
    return QPixmap::fromImage(image);
}

}

QIcon AppIcon::render(const QColor &accent)
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48, 64, 128, 256})
        icon.addPixmap(renderSize(size, accent));
    return icon;
}
