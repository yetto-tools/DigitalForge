#pragma once

#include <QPointF>
#include <cmath>

namespace digitalforge::editor {

// Ajusta un punto (en coordenadas de escena) a la grilla mas cercana en
// ambos ejes. Compartida entre JunctionItem (arrastre de un punto de union)
// y WireItem (arrastre de un waypoint) -- el snap por componente que ya
// existe en ComponentItem.cpp es privado a ese archivo y no se toca aqui.
[[nodiscard]] inline QPointF snapToGrid(QPointF point, qreal grid) {
    return QPointF(std::round(point.x() / grid) * grid, std::round(point.y() / grid) * grid);
}

} // namespace digitalforge::editor
