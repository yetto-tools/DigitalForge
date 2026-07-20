#include "Simulator.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace digitalforge::core {

Simulator::Simulator(const Circuit& circuit) : circuit_(circuit) {
    reset();
}

void Simulator::reset() {
    netValues_.assign(circuit_.netCount(), LogicValue::HighImpedance);
    gateOutputValues_.assign(circuit_.gateCount(), LogicValue::HighImpedance);
    gateInputValues_.assign(circuit_.totalInputSlotCount(), LogicValue::HighImpedance);
    netToggleCount_.assign(circuit_.netCount(), 0);
    netOscillating_.assign(circuit_.netCount(), false);
    queue_.clear();
    clock_ = 0;
    stats_ = Stats{};
    running_ = false;

    seedSourceGates();
}

void Simulator::start() noexcept { running_ = true; }
void Simulator::pause() noexcept { running_ = false; }

void Simulator::seedSourceGates() {
    for (uint32_t i = 0; i < static_cast<uint32_t>(circuit_.gateCount()); ++i) {
        switch (circuit_.gate(i).type) {
            case GateType::ConstantZero:
                applyGateOutput(i, LogicValue::Zero);
                break;
            case GateType::ConstantOne:
                applyGateOutput(i, LogicValue::One);
                break;
            case GateType::WeakZero:
                applyGateOutput(i, LogicValue::Zero);
                break;
            case GateType::WeakOne:
                applyGateOutput(i, LogicValue::One);
                break;
            default:
                break;
        }
    }
}

LogicValue Simulator::evaluateGate(uint32_t gateIndex) const {
    const Gate& g = circuit_.gate(gateIndex);
    if (isSourceType(g.type)) {
        return gateOutputValues_[gateIndex];
    }
    const auto inputs = std::span<const LogicValue>(gateInputValues_).subspan(g.inputStart, g.inputCount);
    return evaluateCombinationalGate(g.type, inputs);
}

void Simulator::applyGateOutput(uint32_t gateIndex, LogicValue newValue) {
    if (gateOutputValues_[gateIndex] == newValue) {
        return;
    }
    gateOutputValues_[gateIndex] = newValue;

    const Gate& gate = circuit_.gate(gateIndex);
    const NetId net = gate.outputNet;
    if (netOscillating_[net]) {
        // Esta net ya quedó fijada en Unknown tras superar el umbral de
        // conmutación; se absorbe la actividad adicional en lugar de
        // volver a propagarla.
        return;
    }

    // Los drivers debiles (WeakZero/WeakOne, ver isWeakType()) se resuelven
    // en un segundo nivel, aparte: solo entran en juego si ningun driver
    // normal esta impulsando activamente esta net (resolved ==
    // HighImpedance tras resolver solo los normales) - asi una resistencia
    // pull-up/pull-down nunca genera un conflicto (Error) contra un driver
    // real, siempre pierde limpio.
    const auto& drivers = circuit_.driversOf(net);
    std::vector<LogicValue> strongValues;
    std::vector<LogicValue> weakValues;
    strongValues.reserve(drivers.size());
    for (const uint32_t driver : drivers) {
        if (isWeakType(circuit_.gate(driver).type)) {
            weakValues.push_back(gateOutputValues_[driver]);
        } else {
            strongValues.push_back(gateOutputValues_[driver]);
        }
    }
    LogicValue resolved = resolveNetValue(strongValues);
    if (resolved == LogicValue::HighImpedance) {
        resolved = resolveNetValue(weakValues);
    }

    if (resolved == netValues_[net]) {
        return;
    }

    if (++netToggleCount_[net] > kOscillationToggleThreshold) {
        netOscillating_[net] = true;
        stats_.oscillationDetected = true;
        ++stats_.oscillationsSuppressed;
        resolved = LogicValue::Unknown;
    }

    netValues_[net] = resolved;
    for (const FanoutEntry& entry : circuit_.fanoutOf(net)) {
        queue_.push(SimulationEvent{clock_ + gate.delay, entry.gateIndex, entry.inputPin, resolved});
    }
    stats_.maxQueueSize = std::max(stats_.maxQueueSize, static_cast<uint64_t>(queue_.size()));
}

bool Simulator::step() {
    if (queue_.empty()) {
        return false;
    }
    const SimulationEvent event = queue_.pop();
    clock_ = event.timestamp;
    ++stats_.eventsProcessed;

    const Gate& g = circuit_.gate(event.componentId);
    const uint32_t slot = g.inputStart + event.inputPin;
    const LogicValue previousValue = gateInputValues_[slot];
    if (previousValue == event.value) {
        return true; // sin cambio real en este pin: no hay nada que reevaluar
    }
    gateInputValues_[slot] = event.value;

    if (g.type == GateType::DFlipFlop) {
        // Estado propio: a diferencia de todo lo demas, la salida no es una
        // funcion pura de las entradas actuales - depende de si CLK (pin 1)
        // acaba de subir. Un cambio en D por si solo (pin 0) no se propaga;
        // eso es exactamente lo que distingue un flip-flop de un latch
        // transparente. `previousValue` (todavia sin sobrescribir arriba)
        // es el valor de CLK justo antes de este evento.
        if (event.inputPin == 1 && event.value == LogicValue::One) {
            const LogicValue d = gateInputValues_[g.inputStart + 0];
            const LogicValue q = (previousValue == LogicValue::Zero)
                                      ? degradeToUnknownIfIndeterminate(d)
                                      : LogicValue::Unknown; // flanco no confirmado (CLK previo era X/Z/Error)
            applyGateOutput(event.componentId, q);
        }
        return true;
    }

    const LogicValue newOutput = evaluateGate(event.componentId);
    applyGateOutput(event.componentId, newOutput);
    return true;
}

bool Simulator::runUntilStable(uint64_t maxEvents) {
    const uint64_t defaultBudget =
        64ULL * (static_cast<uint64_t>(circuit_.gateCount()) + static_cast<uint64_t>(circuit_.netCount()) + 1ULL) +
        10000ULL;
    const uint64_t budget = (maxEvents == 0) ? defaultBudget : maxEvents;
    const uint64_t startEvents = stats_.eventsProcessed;

    while (!queue_.empty()) {
        if (stats_.eventsProcessed - startEvents >= budget) {
            return false;
        }
        step();
    }
    return true;
}

void Simulator::setInput(uint32_t gateIndex, LogicValue value) {
    const Gate& g = circuit_.gate(gateIndex);
    if (g.type != GateType::InputPin) {
        throw std::invalid_argument("setInput: gate is not an InputPin source");
    }
    applyGateOutput(gateIndex, value);
}

} // namespace digitalforge::core
