#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "ComponentDefinition.hpp"
#include "core/Circuit.hpp"
#include "core/Net.hpp"

namespace digitalforge::components {

// Vista de solo lectura de "otro documento", usada unicamente por
// structural.subcircuit para derivar sus pines y aplanar su simulacion sin
// que components:: dependa de editor::CircuitDocument (evitaria un ciclo:
// editor:: ya depende de components::). editor::CircuitDocument es su unica
// implementacion real; los tests pueden proveer otras mas simples.
class ExternalDocumentView {
public:
    virtual ~ExternalDocumentView() = default;

    // Pines de frontera ya ordenados y nombrados (ver
    // CircuitDocument::boundaryPins(): por posicion en el lienzo, Y y luego
    // X; wiring.input -> Input, wiring.output -> Output).
    [[nodiscard]] virtual std::vector<PinTemplate> boundaryPins() const = 0;

    // Registra en `circuit` una copia de todo el grafo interno de este
    // documento (cada ComponentInstance interno clonado con nets frescas),
    // aliando las fronteras a `pinNets` (mismo orden que boundaryPins()).
    // Devuelve el gateIndex de alguna puerta interna para
    // ComponentSimBinding (0 si el documento esta vacio) - mismo patron que
    // ya usan Plexers/Aritmetica con "firstGateIndex".
    virtual uint32_t flattenInto(core::Circuit& circuit, std::span<const core::NetId> pinNets) const = 0;

    // true si este documento ya contiene, a su vez, algun
    // structural.subcircuit - usado para vetar mas de un nivel de
    // anidamiento al elegir este documento como destino de otro.
    [[nodiscard]] virtual bool containsSubcircuit() const = 0;
};

// Contexto opcional que ComponentInstance le pasa a un componente
// estructural (hoy solo structural.subcircuit). `resolve` devuelve nullptr
// si `targetPath` no corresponde a ningun documento vivo todavia.
struct ExternalContext {
    std::function<const ExternalDocumentView*(const std::string& targetPath)> resolve;
};

} // namespace digitalforge::components
