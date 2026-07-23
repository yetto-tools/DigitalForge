#include "WireRouting.hpp"

#include <algorithm>
#include <cmath>

namespace digitalforge::editor {

bool verticalSegmentCrosses(qreal x, qreal yMin, qreal yMax, const QRectF& rect) {
    return x >= rect.left() && x <= rect.right() && yMax >= rect.top() && yMin <= rect.bottom();
}

qreal chooseClearMidX(QPointF from, QPointF to, const std::vector<QRectF>& obstacles) {
    const qreal naturalMidX = (from.x() + to.x()) / 2.0;
    if (obstacles.empty()) {
        return naturalMidX;
    }
    const qreal yMin = std::min(from.y(), to.y());
    const qreal yMax = std::max(from.y(), to.y());
    const auto isClear = [&](qreal x) {
        for (const QRectF& rect : obstacles) {
            if (verticalSegmentCrosses(x, yMin, yMax, rect)) {
                return false;
            }
        }
        return true;
    };
    if (isClear(naturalMidX)) {
        return naturalMidX;
    }
    constexpr qreal kStep = 16.0; // 2x ComponentItem::kGridSize
    constexpr int kMaxSteps = 24; // hasta 384px a cada lado del punto medio
    for (int i = 1; i <= kMaxSteps; ++i) {
        const qreal plus = naturalMidX + i * kStep;
        if (isClear(plus)) {
            return plus;
        }
        const qreal minus = naturalMidX - i * kStep;
        if (isClear(minus)) {
            return minus;
        }
    }
    return naturalMidX;
}

void appendElbow(QPainterPath& path, QPointF from, QPointF to, const std::vector<QRectF>& obstacles) {
    if (std::abs(from.y() - to.y()) <= kWireAlignTolerance) {
        path.lineTo(to.x(), from.y());
        path.lineTo(to);
        return;
    }
    if (std::abs(from.x() - to.x()) <= kWireAlignTolerance) {
        path.lineTo(from.x(), to.y());
        path.lineTo(to);
        return;
    }
    const qreal midX = chooseClearMidX(from, to, obstacles);
    path.lineTo(midX, from.y());
    path.lineTo(midX, to.y());
    path.lineTo(to);
}

void appendElbowVertices(std::vector<QPointF>& vertices, QPointF from, QPointF to,
                         const std::vector<QRectF>& obstacles) {
    if (std::abs(from.y() - to.y()) <= kWireAlignTolerance) {
        vertices.push_back(QPointF(to.x(), from.y()));
        vertices.push_back(to);
        return;
    }
    if (std::abs(from.x() - to.x()) <= kWireAlignTolerance) {
        vertices.push_back(QPointF(from.x(), to.y()));
        vertices.push_back(to);
        return;
    }
    const qreal midX = chooseClearMidX(from, to, obstacles);
    vertices.push_back(QPointF(midX, from.y()));
    vertices.push_back(QPointF(midX, to.y()));
    vertices.push_back(to);
}

QPainterPath buildOrthogonalPath(const std::vector<QPointF>& points, const std::vector<QRectF>& obstacles) {
    QPainterPath path;
    if (points.size() < 2) {
        return path;
    }
    path.moveTo(points.front());
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        appendElbow(path, points[i], points[i + 1], obstacles);
    }
    return path;
}

} // namespace digitalforge::editor
