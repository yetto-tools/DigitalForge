#pragma once

#include <cstdint>
#include <span>

#include "GateType.hpp"
#include "LogicValue.hpp"

namespace digitalforge::core {

// Descripción compacta y favorable a la caché de una única instancia de
// gate. Los gates se almacenan de forma contigua en `std::vector<Gate>`; las
// nets de entrada viven en un arreglo plano separado indexado por
// [inputStart, inputStart + inputCount). Sin punteros, sin herencia: todo es
// un índice dentro de un vector propiedad de Circuit.
// El orden de los campos importa: agrupa los dos uint32_t y los dos
// uint16_t para minimizar el padding de alineacion (con GateType al final,
// sizeof(Gate) da exactamente 16; con el orden "natural" tipo/inputStart/
// inputCount/outputNet/delay quedaria en 20 y rompería el static_assert de
// abajo). Ver Circuit::addGate, cuyo inicializador designado debe listar
// los campos en este mismo orden.
struct Gate {
    uint32_t inputStart = 0;
    uint32_t outputNet = 0;
    uint16_t inputCount = 0;
    // Ticks del EventQueue entre que este gate recibe un cambio de entrada y
    // que ese cambio llega a su fanout (ver Simulator::applyGateOutput). 1
    // (el valor historico, fijo) para cualquier llamador que no pase uno
    // explicito a Circuit::addGate.
    uint16_t delay = 1;
    GateType type = GateType::InputPin;
};

static_assert(sizeof(Gate) <= 16, "Gate must stay compact for million-gate circuits");

// Combina los valores de entrada actuales del gate en su valor de salida.
// Los gates fuente (InputPin / ConstantZero / ConstantOne / WeakZero /
// WeakOne) ignoran `inputs` y son manejados por el llamador, que en su
// lugar proporciona el valor fijo o establecido externamente.
[[nodiscard]] inline LogicValue evaluateCombinationalGate(GateType type,
                                                           std::span<const LogicValue> inputs) noexcept {
    switch (type) {
        case GateType::Buffer:
            return degradeToUnknownIfIndeterminate(inputs[0]);
        case GateType::Not:
            return logicNot(inputs[0]);
        case GateType::And: {
            LogicValue acc = LogicValue::One;
            for (const LogicValue v : inputs) {
                acc = logicAnd2(acc, v);
            }
            return acc;
        }
        case GateType::Or: {
            LogicValue acc = LogicValue::Zero;
            for (const LogicValue v : inputs) {
                acc = logicOr2(acc, v);
            }
            return acc;
        }
        case GateType::Nand: {
            LogicValue acc = LogicValue::One;
            for (const LogicValue v : inputs) {
                acc = logicAnd2(acc, v);
            }
            return logicNot(acc);
        }
        case GateType::Nor: {
            LogicValue acc = LogicValue::Zero;
            for (const LogicValue v : inputs) {
                acc = logicOr2(acc, v);
            }
            return logicNot(acc);
        }
        case GateType::Xor: {
            LogicValue acc = LogicValue::Zero;
            for (const LogicValue v : inputs) {
                acc = logicXor2(acc, v);
            }
            return acc;
        }
        case GateType::Xnor: {
            LogicValue acc = LogicValue::Zero;
            for (const LogicValue v : inputs) {
                acc = logicXor2(acc, v);
            }
            return logicNot(acc);
        }
        case GateType::InputPin:
        case GateType::ConstantZero:
        case GateType::ConstantOne:
        case GateType::WeakZero:
        case GateType::WeakOne:
            return LogicValue::HighImpedance;
        case GateType::DFlipFlop:
            // Nunca se llega aca en la practica: DFlipFlop tiene estado
            // propio (isStatefulType) y Simulator::step() lo maneja aparte,
            // sin pasar por evaluateGate()/evaluateCombinationalGate(). Este
            // caso solo evita un warning de -Wswitch por enum no cubierto.
            return LogicValue::HighImpedance;
        case GateType::TriStateBuffer:
            // D en inputs[0], EN en inputs[1]. Solo un EN limpio en One
            // habilita la salida; cualquier otro valor (Zero/HighImpedance/
            // Unknown/Error) deja la salida en alta impedancia, igual que un
            // buffer triestado real deshabilitado.
            return inputs[1] == LogicValue::One ? degradeToUnknownIfIndeterminate(inputs[0])
                                                 : LogicValue::HighImpedance;
    }
    return LogicValue::Unknown;
}

} // namespace digitalforge::core
