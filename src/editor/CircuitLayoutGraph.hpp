#pragma once

#include <cstdint>
#include <vector>

#include "CircuitDocument.hpp"

// Helpers de recorrido de redes electricas compartidos entre
// CircuitLayoutGraph.cpp (computeCircuitLayers(), puro -- solo necesita
// CircuitDocument) y CircuitAutoLayout.cpp (autoArrangeCircuit(), que ademas
// necesita CircuitScene/ComponentItem para medir tamanos reales en pantalla).
// Separado en su propio header/.cpp para que el algoritmo de capas se pueda
// compilar y testear (tests_qt/) sin arrastrar toda la pila de Qt Widgets del
// editor grafico -- ver el comentario de CircuitAutoLayout.hpp.
namespace digitalforge::editor {

// Todos los OTROS pines conectados electricamente a `startPin` (directo o a
// traves de una cadena de puntos de union).
[[nodiscard]] std::vector<PinRef> pinsOnNetOf(const CircuitDocument& document, PinRef startPin);

// Todos los pines alcanzables desde un punto de union (atravesando cadenas
// de otras uniones).
[[nodiscard]] std::vector<PinRef> pinsReachableFromJunction(const CircuitDocument& document, uint32_t junctionId);

} // namespace digitalforge::editor
