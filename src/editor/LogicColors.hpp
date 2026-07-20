#pragma once

#include <QColor>
#include <QString>

#include "core/LogicValue.hpp"

namespace digitalforge::editor {

// Esquema de colores original (no copiado de ninguna herramienta de
// terceros): verde oscuro/brillante para un nivel definido 0/1, azul para
// una X indeterminada, gris para una Z flotante, rojo para un conflicto de
// manejadores (driver conflict).
[[nodiscard]] inline QColor logicValueColor(core::LogicValue value) {
    switch (value) {
        case core::LogicValue::Zero:          return QColor(0, 100, 0);
        case core::LogicValue::One:           return QColor(40, 200, 40);
        case core::LogicValue::Unknown:       return QColor(40, 80, 220);
        case core::LogicValue::HighImpedance: return QColor(150, 150, 150);
        case core::LogicValue::Error:         return QColor(220, 30, 30);
    }
    return QColor(0, 0, 0);
}

// Glifo corto para el estado logico de cinco valores, usado en elementos
// interactivos/etiquetados (por ejemplo, el conmutador wiring.input) donde el
// color solo no basta para leer el valor de un vistazo.
[[nodiscard]] inline QString logicValueGlyph(core::LogicValue value) {
    switch (value) {
        case core::LogicValue::Zero:          return QStringLiteral("0");
        case core::LogicValue::One:            return QStringLiteral("1");
        case core::LogicValue::Unknown:        return QStringLiteral("X");
        case core::LogicValue::HighImpedance:  return QStringLiteral("Z");
        case core::LogicValue::Error:          return QStringLiteral("!");
    }
    return QString();
}

} // namespace digitalforge::editor
