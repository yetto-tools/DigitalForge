#pragma once

#include <cstdint>
#include <vector>

#include "Circuit.hpp"
#include "Event.hpp"
#include "EventQueue.hpp"
#include "LogicValue.hpp"
#include "Net.hpp"

namespace digitalforge::core {

// Motor de simulación dirigido por eventos. Posee todo el estado mutable en
// tiempo de ejecución (valores actuales de net/gate, eventos pendientes);
// Circuit proporciona la topología inmutable. Varias instancias de Simulator
// pueden ejecutar el mismo Circuit de forma independiente.
class Simulator {
public:
    struct Stats {
        uint64_t eventsProcessed = 0;
        uint64_t maxQueueSize = 0;
        uint64_t oscillationsSuppressed = 0;
        bool oscillationDetected = false;
    };

    explicit Simulator(const Circuit& circuit);

    // Limpia todo el estado en tiempo de ejecución: todas las nets quedan
    // flotantes (HighImpedance), las fuentes constantes se (re)afirman, la
    // cola de eventos se vacía y las estadísticas se reinician.
    void reset();

    void start() noexcept;
    void pause() noexcept;
    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    // Procesa exactamente un evento de la cola. Devuelve false si la cola ya
    // estaba vacía (no había nada que hacer).
    bool step();

    // Vacía la cola de eventos hasta que quede vacía (estable) o hasta que
    // se hayan procesado `maxEvents` eventos en esta llamada, lo que ocurra
    // primero. Devuelve true si el circuito alcanzó un estado estable,
    // false si se agotó el presupuesto de eventos (un indicio de un
    // circuito inestable/oscilante; ver stats().oscillationDetected para
    // confirmarlo).
    bool runUntilStable(uint64_t maxEvents = 0);

    // Impulsa un gate fuente externo (InputPin) al valor `value`,
    // programando la propagación hacia su fanout. Solo es válido para gates
    // con cero entradas.
    void setInput(uint32_t gateIndex, LogicValue value);

    [[nodiscard]] LogicValue getNetValue(NetId net) const { return netValues_.at(net); }
    [[nodiscard]] LogicValue getGateOutputValue(uint32_t gateIndex) const {
        return gateOutputValues_.at(gateIndex);
    }

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    static constexpr uint32_t kOscillationToggleThreshold = 64;

    [[nodiscard]] LogicValue evaluateGate(uint32_t gateIndex) const;
    void applyGateOutput(uint32_t gateIndex, LogicValue newValue);
    void seedSourceGates();

    const Circuit& circuit_;
    std::vector<LogicValue> netValues_;
    std::vector<LogicValue> gateOutputValues_;
    std::vector<LogicValue> gateInputValues_;
    std::vector<uint32_t> netToggleCount_;
    std::vector<bool> netOscillating_;
    EventQueue queue_;
    uint64_t clock_ = 0;
    bool running_ = false;
    Stats stats_{};
};

} // namespace digitalforge::core
