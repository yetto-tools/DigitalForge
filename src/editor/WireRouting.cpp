#include "WireRouting.hpp"

#include <QLineF>

#include <cmath>

namespace digitalforge::editor {

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

std::vector<QPointF> moveWireCorner(const std::vector<QPointF>& points, std::size_t cornerIndex,
                                     QPointF cursorPos) {
    if (cornerIndex == 0 || cornerIndex + 1 >= points.size()) {
        return points; // los extremos los ancla su pin/union
    }
    std::vector<QPointF> result = points;
    result[cornerIndex] = cursorPos;
    return simplifyOrthogonalPolyline(result);
}

} // namespace digitalforge::editor
