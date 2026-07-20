#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "Gate.hpp"
#include "GateType.hpp"
#include "Net.hpp"

namespace digitalforge::core {

// Topología estática de un circuito: los gates, las nets que los conectan y
// la adyacencia derivada (drivers/fanout por net) que el simulador necesita
// para evitar evaluar todo el circuito en cada cambio. Circuit no contiene
// estado de simulación en tiempo de ejecución (valores actuales, cola de
// eventos); eso vive en Simulator, de modo que la misma topología pueda ser
// simulada por instancias independientes de Simulator.
class Circuit {
public:
    // Asigna una net nueva, actualmente sin driver, y devuelve su id.
    [[nodiscard]] NetId addNet() {
        netDrivers_.emplace_back();
        netFanout_.emplace_back();
        return static_cast<NetId>(netDrivers_.size() - 1);
    }

    // Registra un gate de tipo `type` que lee `inputNets` (en orden) e
    // impulsa `outputNet`, con un retardo de propagacion de `delay` ticks
    // (ver Gate::delay/Simulator::applyGateOutput; 1 = el comportamiento
    // historico, instantaneo tick-a-tick). Lanza std::invalid_argument si la
    // aridad no es correcta para `type` o si algún id de net referenciado
    // está fuera de rango.
    [[nodiscard]] uint32_t addGate(GateType type, std::span<const NetId> inputNets, NetId outputNet,
                                    uint16_t delay = 1) {
        if (!isValidInputCount(type, inputNets.size())) {
            throw std::invalid_argument(std::string("invalid input count for gate type ") +
                                         toString(type));
        }
        if (outputNet >= netDrivers_.size()) {
            throw std::invalid_argument("addGate: outputNet out of range");
        }
        for (const NetId net : inputNets) {
            if (net >= netFanout_.size()) {
                throw std::invalid_argument("addGate: input net out of range");
            }
        }

        const auto gateIndex = static_cast<uint32_t>(gates_.size());
        const auto inputStart = static_cast<uint32_t>(gateInputNets_.size());

        for (std::size_t i = 0; i < inputNets.size(); ++i) {
            const NetId net = inputNets[i];
            gateInputNets_.push_back(net);
            netFanout_[net].push_back(FanoutEntry{gateIndex, static_cast<uint16_t>(i)});
        }

        gates_.push_back(Gate{
            .inputStart = inputStart,
            .outputNet = outputNet,
            .inputCount = static_cast<uint16_t>(inputNets.size()),
            .delay = delay,
            .type = type,
        });
        netDrivers_[outputNet].push_back(gateIndex);

        return gateIndex;
    }

    [[nodiscard]] std::size_t gateCount() const noexcept { return gates_.size(); }
    [[nodiscard]] std::size_t netCount() const noexcept { return netDrivers_.size(); }

    // Número total de slots (gate, inputPin) a través de todos los gates.
    // Simulator usa esto para dimensionar un arreglo paralelo de los
    // últimos valores entregados por pin, indexado de la misma forma que
    // gateInputs() (gate.inputStart + índice de pin).
    [[nodiscard]] std::size_t totalInputSlotCount() const noexcept { return gateInputNets_.size(); }

    [[nodiscard]] const Gate& gate(uint32_t gateIndex) const { return gates_.at(gateIndex); }

    [[nodiscard]] std::span<const NetId> gateInputs(uint32_t gateIndex) const {
        const Gate& g = gate(gateIndex);
        return std::span<const NetId>(gateInputNets_).subspan(g.inputStart, g.inputCount);
    }

    [[nodiscard]] const std::vector<FanoutEntry>& fanoutOf(NetId net) const {
        return netFanout_.at(net);
    }

    [[nodiscard]] const std::vector<uint32_t>& driversOf(NetId net) const {
        return netDrivers_.at(net);
    }

private:
    std::vector<Gate> gates_;
    std::vector<NetId> gateInputNets_;
    std::vector<std::vector<uint32_t>> netDrivers_;
    std::vector<std::vector<FanoutEntry>> netFanout_;
};

} // namespace digitalforge::core
