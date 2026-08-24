#include "WireRouting.hpp"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace digitalforge::editor {

qreal distanceToSegment(QPointF p, QPointF a, QPointF b, QPointF* projectionOut) {
    const QPointF ab = b - a;
    const qreal lengthSquared = QPointF::dotProduct(ab, ab);
    QPointF projection = a;
    if (lengthSquared > 0.0) {
        qreal t = QPointF::dotProduct(p - a, ab) / lengthSquared;
        t = std::clamp(t, 0.0, 1.0);
        projection = a + t * ab;
    }
    if (projectionOut != nullptr) {
        *projectionOut = projection;
    }
    return QLineF(p, projection).length();
}

std::vector<QPointF> wireVertices(const std::vector<QPointF>& points) {
    if (points.size() < 2) {
        return points;
    }
    std::vector<QPointF> vertices;
    vertices.reserve(points.size() * 2);
    vertices.push_back(points.front());
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        // Se parte del ultimo vertice ya emitido (no de points[i]) para que la
        // lista describa exactamente la polilinea dibujada: cuando un tramo se
        // endereza por estar dentro de la tolerancia, el punto corregido es el
        // que vale para el tramo siguiente.
        const QPointF from = vertices.back();
        const QPointF to = points[i + 1];
        const bool isLast = i + 2 == points.size();
        if (std::abs(from.y() - to.y()) <= kWireAlignTolerance) {
            if (isLast) {
                // Un cable entra SIEMPRE al centro del pin. Enderezar el tramo
                // sobre la y del quiebre lo dejaria corto por el resto
                // sub-tolerancia (los pines no siempre caen en la grilla), asi
                // que se corre el quiebre a la y del pin -- nunca al reves --
                // y se termina exactamente en el. Si no hay quiebre que correr
                // (el vertice previo es el otro extremo, tambien anclado), el
                // resto queda como una inclinacion de <=3px, imperceptible.
                if (vertices.size() >= 2) {
                    vertices.back().setY(to.y());
                }
                vertices.push_back(to);
            } else {
                vertices.emplace_back(to.x(), from.y()); // horizontal
            }
        } else if (std::abs(from.x() - to.x()) <= kWireAlignTolerance) {
            if (isLast) {
                if (vertices.size() >= 2) {
                    vertices.back().setX(to.x());
                }
                vertices.push_back(to);
            } else {
                vertices.emplace_back(from.x(), to.y()); // vertical
            }
        } else {
            // Codo en L horizontal-primero: una sola forma posible, siempre la
            // misma. Nunca un codo en Z centrado (que "saltaba" de lado al
            // moverse los extremos) ni un desvio por obstaculos.
            vertices.emplace_back(to.x(), from.y());
            vertices.push_back(to);
        }
    }
    return vertices;
}

QPainterPath buildWirePath(const std::vector<QPointF>& points) {
    QPainterPath path;
    const std::vector<QPointF> vertices = wireVertices(points);
    if (vertices.size() < 2) {
        return path;
    }
    path.moveTo(vertices.front());
    for (std::size_t i = 1; i < vertices.size(); ++i) {
        path.lineTo(vertices[i]);
    }
    return path;
}

std::vector<QPointF> simplifyOrthogonalPolyline(const std::vector<QPointF>& vertices) {
    if (vertices.size() <= 2) {
        return vertices;
    }
    // 1) Fusiona duplicados consecutivos (o casi).
    std::vector<QPointF> deduped;
    for (const QPointF& p : vertices) {
        if (deduped.empty() || QLineF(deduped.back(), p).length() > kWireAlignTolerance) {
            deduped.push_back(p);
        }
    }
    if (deduped.size() <= 2) {
        return deduped;
    }
    // 2) Descarta vertices colineales (no son esquinas): un punto cuyo vecino
    // previo YA CONSERVADO y el siguiente comparten con el la misma x o la
    // misma y forma un tramo recto, asi que el punto del medio es redundante.
    std::vector<QPointF> result;
    result.push_back(deduped.front());
    for (std::size_t i = 1; i + 1 < deduped.size(); ++i) {
        const QPointF prev = result.back();
        const QPointF cur = deduped[i];
        const QPointF next = deduped[i + 1];
        const bool collinearHorizontal =
            std::abs(prev.y() - cur.y()) <= kWireAlignTolerance && std::abs(cur.y() - next.y()) <= kWireAlignTolerance;
        const bool collinearVertical =
            std::abs(prev.x() - cur.x()) <= kWireAlignTolerance && std::abs(cur.x() - next.x()) <= kWireAlignTolerance;
        if (collinearHorizontal || collinearVertical) {
            continue;
        }
        result.push_back(cur);
    }
    result.push_back(deduped.back());
    return result;
}

std::vector<QPointF> moveWireSegment(const std::vector<QPointF>& points, std::size_t segmentIndex,
                                      QPointF cursorPos) {
    if (points.size() < 2 || segmentIndex + 1 >= points.size()) {
        return points;
    }
    const std::size_t i = segmentIndex;
    const std::size_t j = segmentIndex + 1;
    const bool horizontal = std::abs(points[i].y() - points[j].y()) <= kWireAlignTolerance;

    // Perpendicular al propio segmento: un tramo horizontal solo cambia de y y
    // uno vertical solo de x, asi los dos vecinos siguen encontrandolo en
    // angulo recto sin tener que tocarlos.
    const auto shifted = [horizontal, cursorPos](QPointF p) {
        return horizontal ? QPointF(p.x(), cursorPos.y()) : QPointF(cursorPos.x(), p.y());
    };

    std::vector<QPointF> result = points;
    result[i] = shifted(points[i]);
    result[j] = shifted(points[j]);

    // Un extremo esta anclado a su pin/union y no se puede mover: se lo
    // devuelve a su lugar y se inserta el vertice que absorbe el quiebre. El
    // lado final se trata primero para que insertar ahi no corra el indice 0.
    if (j == points.size() - 1) {
        result[j] = points[j];
        result.insert(result.begin() + static_cast<std::ptrdiff_t>(j), shifted(points[j]));
    }
    if (i == 0) {
        result[0] = points[0];
        result.insert(result.begin() + 1, shifted(points[0]));
    }
    return simplifyOrthogonalPolyline(result);
}

namespace {

// Si `value` esta a kCornerAlignSnapTolerance o menos de alguno de los dos
// candidatos, devuelve ese candidato (el mas cercano de los dos); si no,
// devuelve `value` sin tocar.
qreal snapToNearestNeighbor(qreal value, qreal neighborA, qreal neighborB) {
    const qreal distA = std::abs(value - neighborA);
    const qreal distB = std::abs(value - neighborB);
    const qreal bestDist = std::min(distA, distB);
    if (bestDist > kCornerAlignSnapTolerance) {
        return value;
    }
    return distA <= distB ? neighborA : neighborB;
}

} // namespace

std::vector<QPointF> moveWireCorner(const std::vector<QPointF>& points, std::size_t cornerIndex,
                                     QPointF cursorPos) {
    if (cornerIndex == 0 || cornerIndex + 1 >= points.size()) {
        return points; // los extremos los ancla su pin/union
    }
    const QPointF prev = points[cornerIndex - 1];
    const QPointF next = points[cornerIndex + 1];
    const QPointF snapped(snapToNearestNeighbor(cursorPos.x(), prev.x(), next.x()),
                           snapToNearestNeighbor(cursorPos.y(), prev.y(), next.y()));

    std::vector<QPointF> result = points;
    result[cornerIndex] = snapped;
    return simplifyOrthogonalPolyline(result);
}

WireSplit splitWireWaypoints(const std::vector<QPointF>& polyline, QPointF splitPoint) {
    WireSplit result;
    if (polyline.size() < 2) {
        return result;
    }
    // Mismo criterio de distancia punto-segmento que usa WireItem para hit-
    // testing: el segmento cuyo punto mas cercano a splitPoint sea el mas
    // proximo es donde cae el corte.
    std::size_t bestIndex = 0;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (std::size_t i = 0; i + 1 < polyline.size(); ++i) {
        const qreal distance = distanceToSegment(splitPoint, polyline[i], polyline[i + 1]);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    for (std::size_t i = 1; i <= bestIndex; ++i) {
        result.before.push_back(polyline[i]);
    }
    for (std::size_t i = bestIndex + 1; i + 1 < polyline.size(); ++i) {
        result.after.push_back(polyline[i]);
    }

    // Si splitPoint cae (casi) exactamente sobre un vertice ya existente, ese
    // vertice ES el nuevo punto de union: no se lo duplica como ultimo/primer
    // waypoint de su propio tramo.
    if (!result.before.empty() && QLineF(result.before.back(), splitPoint).length() <= kWireAlignTolerance) {
        result.before.pop_back();
    }
    if (!result.after.empty() && QLineF(result.after.front(), splitPoint).length() <= kWireAlignTolerance) {
        result.after.erase(result.after.begin());
    }
    return result;
}

} // namespace digitalforge::editor
