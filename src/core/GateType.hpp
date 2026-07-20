#pragma once

#include <cstddef>
#include <cstdint>

namespace digitalforge::core {

// Los tipos de elemento primitivo que el simulador central puede evaluar.
// La mayoria son combinacionales puros (la salida es una funcion pura de las
// entradas actuales); DFlipFlop es la excepcion - tiene estado propio (ver
// isStatefulType) y Simulator::step() lo maneja aparte, detectando el flanco
// de CLK en vez de llamar a evaluateCombinationalGate(). Los elementos
// estructurales (subcircuitos) se añaden encima en fases posteriores.
enum class GateType : uint8_t {
    InputPin,        // fuente impulsada externamente, 0 entradas
    ConstantZero,    // fuente fija, 0 entradas
    ConstantOne,     // fuente fija, 0 entradas
    Buffer,          // 1 entrada
    Not,             // 1 entrada
    And,             // >= 2 entradas
    Or,              // >= 2 entradas
    Nand,            // >= 2 entradas
    Nor,             // >= 2 entradas
    Xor,             // >= 2 entradas
    Xnor,            // >= 2 entradas
    DFlipFlop,       // 2 entradas fijas: D (pin 0), CLK (pin 1); con estado propio
    TriStateBuffer,  // 2 entradas fijas: D (pin 0), EN (pin 1); combinacional puro
    WeakZero,        // fuente debil, 0 entradas (resistencia pull-down)
    WeakOne,         // fuente debil, 0 entradas (resistencia pull-up)
};

[[nodiscard]] constexpr bool isSourceType(GateType type) noexcept {
    return type == GateType::InputPin || type == GateType::ConstantZero || type == GateType::ConstantOne ||
           type == GateType::WeakZero || type == GateType::WeakOne;
}

// Un driver "debil" (resistencia pull-up/pull-down): a diferencia de
// ConstantZero/One, Simulator::applyGateOutput() lo resuelve en un segundo
// nivel, aparte de los drivers normales - solo toma el control de la net si
// ningun driver normal la esta impulsando activamente, y nunca genera un
// conflicto (Error) contra uno normal (siempre pierde limpio).
[[nodiscard]] constexpr bool isWeakType(GateType type) noexcept {
    return type == GateType::WeakZero || type == GateType::WeakOne;
}

[[nodiscard]] constexpr bool isUnaryType(GateType type) noexcept {
    return type == GateType::Buffer || type == GateType::Not;
}

[[nodiscard]] constexpr bool isVariadicType(GateType type) noexcept {
    return type == GateType::And || type == GateType::Or || type == GateType::Nand ||
           type == GateType::Nor || type == GateType::Xor || type == GateType::Xnor;
}

// Tipos cuya salida depende de algo mas que las entradas actuales (aca, el
// valor anterior de CLK para detectar el flanco) - por eso no pasan por
// evaluateCombinationalGate(), a diferencia de todos los demas.
[[nodiscard]] constexpr bool isStatefulType(GateType type) noexcept {
    return type == GateType::DFlipFlop;
}

// Valida que `inputCount` sea una aridad legal para `type`. Se usa tanto al
// construir circuitos de forma programática como al validar definiciones de
// componentes en JSON, de modo que los datos malformados se rechacen en el
// momento de la construcción en lugar de causar comportamiento indefinido
// durante la simulación.
[[nodiscard]] constexpr bool isValidInputCount(GateType type, std::size_t inputCount) noexcept {
    if (isSourceType(type)) {
        return inputCount == 0;
    }
    if (isUnaryType(type)) {
        return inputCount == 1;
    }
    if (isVariadicType(type)) {
        return inputCount >= 2;
    }
    if (isStatefulType(type)) {
        return inputCount == 2;
    }
    if (type == GateType::TriStateBuffer) {
        return inputCount == 2;
    }
    return false;
}

[[nodiscard]] constexpr const char* toString(GateType type) noexcept {
    switch (type) {
        case GateType::InputPin:     return "InputPin";
        case GateType::ConstantZero: return "ConstantZero";
        case GateType::ConstantOne:  return "ConstantOne";
        case GateType::Buffer:       return "Buffer";
        case GateType::Not:          return "Not";
        case GateType::And:          return "And";
        case GateType::Or:           return "Or";
        case GateType::Nand:         return "Nand";
        case GateType::Nor:          return "Nor";
        case GateType::Xor:          return "Xor";
        case GateType::Xnor:         return "Xnor";
        case GateType::DFlipFlop:    return "DFlipFlop";
        case GateType::TriStateBuffer: return "TriStateBuffer";
        case GateType::WeakZero:     return "WeakZero";
        case GateType::WeakOne:      return "WeakOne";
    }
    return "Unknown";
}

} // namespace digitalforge::core
