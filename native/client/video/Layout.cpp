#include "Layout.h"

#include <QVariantList>
#include <algorithm>
#include <cmath>
#include <optional>

namespace openspeak {

namespace {

struct Fit {
    int rows = 1, cols = 1;
    double w = 0, h = 0;
};

Fit fit(int count, int rows, double width, double height, double gap)
{
    Fit f;
    f.rows = rows;
    f.cols = int(std::ceil(double(count) / rows));
    const double cellW = (width - (f.cols - 1) * gap) / f.cols;
    const double cellH = (height - (rows - 1) * gap) / rows;
    f.w = std::floor(std::min(cellW, cellH * LayoutAspect));
    f.h = std::floor(f.w / LayoutAspect);
    return f;
}

QList<QRectF> place(int count, const Fit &f, double width, double height, double gap)
{
    QList<QRectF> out;
    const double usedH = f.rows * f.h + (f.rows - 1) * gap;
    const double top = std::max(0.0, (height - usedH) / 2);
    for (int i = 0; i < count; ++i) {
        const int row = i / f.cols;
        const int inRow = row == f.rows - 1 ? count - f.cols * (f.rows - 1) : f.cols;
        const int col = i % f.cols;
        const double rowW = inRow * f.w + (inRow - 1) * gap;
        out.append(QRectF(std::round((width - rowW) / 2 + col * (f.w + gap)), std::round(top + row * (f.h + gap)), f.w, f.h));
    }
    return out;
}

QVariantMap toMap(const QRectF &r)
{
    return {{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}};
}

QRectF toRect(const QVariantMap &m)
{
    return QRectF(m.value("x").toDouble(), m.value("y").toDouble(), m.value("w").toDouble(), m.value("h").toDouble());
}

constexpr double Epsilon = 0.01;

bool overlapping(const QRectF &a, const QRectF &b, double gap)
{
    return a.x() < b.right() + gap - Epsilon && b.x() < a.right() + gap - Epsilon
        && a.y() < b.bottom() + gap - Epsilon && b.y() < a.bottom() + gap - Epsilon;
}

// Menor deslocamento que tira `mover` de cima de `other` sem sair da área.
void pushAway(QRectF &mover, const QRectF &other, double width, double height, double gap)
{
    const QPointF options[] = {{other.x() - gap - mover.width(), mover.y()},
                               {other.right() + gap, mover.y()},
                               {mover.x(), other.y() - gap - mover.height()},
                               {mover.x(), other.bottom() + gap}};
    std::optional<QPointF> best;
    const auto cost = [&](const QPointF &p) { return std::abs(p.x() - mover.x()) + std::abs(p.y() - mover.y()); };
    for (const QPointF &p : options) {
        if (p.x() < 0 || p.y() < 0 || p.x() + mover.width() > width || p.y() + mover.height() > height)
            continue;
        if (!best || cost(p) < cost(*best))
            best = p;
    }
    if (best)
        mover.moveTo(*best);
}

bool settle(QList<QRectF> &rects, int fixed, double width, double height, double gap)
{
    for (int pass = 0; pass < 32; ++pass) {
        bool clear = true;
        for (int i = 0; i < rects.size(); ++i) {
            for (int j = i + 1; j < rects.size(); ++j) {
                if (!overlapping(rects[i], rects[j], gap))
                    continue;
                clear = false;
                const bool moverIsI = j == fixed;
                pushAway(moverIsI ? rects[i] : rects[j], moverIsI ? rects[j] : rects[i], width, height, gap);
            }
        }
        if (clear)
            return true;
    }
    return false;
}

} // namespace

LayoutResult gridLayout(int count, double width, double height, double gap, int offset, int minHeight)
{
    LayoutResult r;
    if (count <= 0 || width <= 0 || height <= 0)
        return r;
    std::optional<Fit> best;
    for (int rows = 1; rows <= count; ++rows) {
        const Fit f = fit(count, rows, width, height, gap);
        if (f.h < minHeight)
            continue;
        if (!best || f.w * f.h > best->w * best->h)
            best = f;
    }
    int shown = count;
    if (!best) {
        // Paginação: maior quantidade que cabe com a altura mínima.
        const int maxRows = std::max(1, int((height + gap) / (minHeight + gap)));
        const int maxCols = std::max(1, int((width + gap) / (minHeight * LayoutAspect + gap)));
        shown = std::clamp(maxRows * maxCols, 1, count);
        for (int rows = 1; rows <= maxRows; ++rows) {
            const Fit f = fit(shown, rows, width, height, gap);
            if (f.cols > maxCols)
                continue;
            if (!best || f.w * f.h > best->w * best->h)
                best = f;
        }
        if (!best)
            best = fit(shown, 1, width, height, gap);
    }
    const int start = std::clamp(offset, 0, std::max(0, count - shown));
    const auto rects = place(std::min(shown, count - start), *best, width, height, gap);
    for (int i = 0; i < rects.size(); ++i)
        r.tiles.append({rects[i], start + i, false});
    r.perPage = shown;
    return r;
}

LayoutResult focusLayout(int count, int focusIndex, double width, double height, double gap, int offset)
{
    constexpr double StripHeight = 100;
    LayoutResult r;
    if (count <= 0)
        return r;
    QList<int> others;
    for (int i = 0; i < count; ++i)
        if (i != focusIndex)
            others.append(i);
    const bool hasStrip = !others.isEmpty() && height - StripHeight - gap >= 140;
    const double mainH = hasStrip ? height - StripHeight - gap : height;
    const double w = std::floor(std::min(width, mainH * LayoutAspect));
    const double h = std::floor(w / LayoutAspect);
    r.tiles.append({QRectF(std::round((width - w) / 2), 0, w, h), focusIndex, true});
    if (!hasStrip)
        return r;
    const double sw = std::floor(StripHeight * LayoutAspect);
    r.perPage = std::max(1, int((width + gap) / (sw + gap)));
    const int start = std::clamp(offset, 0, std::max(0, int(others.size()) - r.perPage));
    const auto visible = others.mid(start, r.perPage);
    const double rowW = visible.size() * sw + (visible.size() - 1) * gap;
    for (int i = 0; i < visible.size(); ++i)
        r.tiles.append({QRectF(std::round((width - rowW) / 2 + i * (sw + gap)), h + gap, sw, StripHeight), visible[i], false});
    return r;
}

QRectF moveRect(const QRectF &rect, double dx, double dy, double width, double height)
{
    return QRectF(std::min(std::max(0.0, rect.x() + dx), width - rect.width()),
                  std::min(std::max(0.0, rect.y() + dy), height - rect.height()), rect.width(), rect.height());
}

QRectF resizeRect(const QRectF &rect, bool left, bool top, double dx, double dy, double width, double height, double minWidth)
{
    const double aspect = rect.width() / rect.height();
    const double anchorX = left ? rect.right() : rect.x();
    const double anchorY = top ? rect.bottom() : rect.y();
    // Vale o eixo que mais se afastou do tamanho inicial.
    const double byWidth = rect.width() + (left ? -dx : dx);
    const double byHeight = (rect.height() + (top ? -dy : dy)) * aspect;
    const double wanted = std::abs(byWidth - rect.width()) >= std::abs(byHeight - rect.width()) ? byWidth : byHeight;
    const double room = std::min(left ? anchorX : width - anchorX, (top ? anchorY : height - anchorY) * aspect);
    const double w = std::min(std::max(wanted, minWidth), room);
    const double h = w / aspect;
    return QRectF(left ? anchorX - w : anchorX, top ? anchorY - h : anchorY, w, h);
}

QList<QRectF> resolveOverlaps(QList<QRectF> rects, int fixed, double width, double height, double gap, double minWidth)
{
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (settle(rects, fixed, width, height, gap))
            return rects;
        bool shrunk = false;
        for (int i = 0; i < rects.size(); ++i) {
            QRectF &r = rects[i];
            if (i == fixed || r.width() <= minWidth)
                continue;
            const double w = std::max(minWidth, r.width() * 0.94);
            const double h = r.height() * w / r.width();
            r = QRectF(r.x() + (r.width() - w) / 2, r.y() + (r.height() - h) / 2, w, h);
            shrunk = true;
        }
        if (!shrunk)
            return {};
    }
    return {};
}

QRectF rectFromFraction(double fx, double fy, double fw, double aspect, double width, double height)
{
    const double w = std::min({fw * width, width, height * aspect});
    const double h = w / aspect;
    return QRectF(std::min(std::max(0.0, fx * width), width - w), std::min(std::max(0.0, fy * height), height - h), w, h);
}

QVariantMap StreamLayout::compute(int count, int focusIndex, double width, double height, double gap, int offset) const
{
    const LayoutResult r = focusIndex >= 0 ? focusLayout(count, focusIndex, width, height, gap, offset)
                                           : gridLayout(count, width, height, gap, offset);
    QVariantList tiles;
    for (const LayoutTile &t : r.tiles)
        tiles.append(QVariantMap{{"x", t.rect.x()}, {"y", t.rect.y()}, {"w", t.rect.width()}, {"h", t.rect.height()},
                                 {"index", t.index}, {"focused", t.focused}});
    return {{"perPage", r.perPage}, {"tiles", tiles}};
}

QVariantMap StreamLayout::moveRect(const QVariantMap &rect, double dx, double dy, double width, double height) const
{
    return toMap(openspeak::moveRect(toRect(rect), dx, dy, width, height));
}

QVariantMap StreamLayout::resizeRect(const QVariantMap &rect, bool left, bool top, double dx, double dy, double width, double height) const
{
    return toMap(openspeak::resizeRect(toRect(rect), left, top, dx, dy, width, height));
}

QVariantList StreamLayout::resolveOverlaps(const QVariantList &rects, int fixed, double width, double height, double gap) const
{
    QList<QRectF> in;
    for (const QVariant &r : rects)
        in.append(toRect(r.toMap()));
    QVariantList out;
    for (const QRectF &r : openspeak::resolveOverlaps(in, fixed, width, height, gap))
        out.append(toMap(r));
    return out;
}

QVariantMap StreamLayout::rectToFraction(const QVariantMap &rect, double width, double height) const
{
    const QRectF r = toRect(rect);
    return {{"x", r.x() / width}, {"y", r.y() / height}, {"w", r.width() / width}, {"aspect", r.width() / r.height()}};
}

QVariantMap StreamLayout::rectFromFraction(const QVariantMap &fraction, double width, double height) const
{
    return toMap(openspeak::rectFromFraction(fraction.value("x").toDouble(), fraction.value("y").toDouble(),
                                             fraction.value("w").toDouble(), fraction.value("aspect").toDouble(), width, height));
}

} // namespace openspeak
