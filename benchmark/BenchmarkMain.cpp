// Benchmark sin interfaz grafica para el nucleo de simulacion de
// DigitalForge. Construye circuitos con 10,000 / 100,000 / 1,000,000
// puertas usando tres topologias distintas y mide cuanto tarda el
// simulador dirigido por eventos en alcanzar un estado estable despues de
// conmutar cada entrada primaria una vez. Sin dependencia de GUI.

#include <array>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "core/Circuit.hpp"
#include "core/Gate.hpp"
#include "core/GateType.hpp"
#include "core/LogicValue.hpp"
#include "core/Net.hpp"
#include "core/Simulator.hpp"

namespace {

using digitalforge::core::Circuit;
using digitalforge::core::FanoutEntry;
using digitalforge::core::Gate;
using digitalforge::core::GateType;
using digitalforge::core::LogicValue;
using digitalforge::core::NetId;
using digitalforge::core::Simulator;

struct BuiltCircuit {
    Circuit circuit;
    std::vector<uint32_t> primaryInputGates;
};

// Una cadena de puertas NOT: entrada -> not -> not -> ... -> salida. Ejercita
// la cola de eventos con una cadena de dependencias larga y estrictamente
// secuencial.
BuiltCircuit buildInverterChain(std::size_t gateCount) {
    BuiltCircuit built;
    Circuit& circuit = built.circuit;
    const std::vector<NetId> none;

    NetId current = circuit.addNet();
    const uint32_t ip = circuit.addGate(GateType::InputPin, none, current);
    built.primaryInputGates.push_back(ip);

    for (std::size_t i = 0; i < gateCount; ++i) {
        const NetId next = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{current}, next);
        current = next;
    }
    return built;
}

// Un arbol binario balanceado de puertas NAND de 2 entradas alimentado por
// muchas entradas primarias independientes en las hojas. Ejercita la
// convergencia con fan-in amplio.
BuiltCircuit buildNandTree(std::size_t gateCount) {
    BuiltCircuit built;
    Circuit& circuit = built.circuit;
    const std::vector<NetId> none;

    std::size_t leafCount = 1;
    while (leafCount + (leafCount - 1) < gateCount) {
        leafCount *= 2;
    }

    std::vector<NetId> level;
    level.reserve(leafCount);
    for (std::size_t i = 0; i < leafCount; ++i) {
        const NetId leafNet = circuit.addNet();
        const uint32_t ip = circuit.addGate(GateType::InputPin, none, leafNet);
        built.primaryInputGates.push_back(ip);
        level.push_back(leafNet);
    }

    while (level.size() > 1) {
        std::vector<NetId> nextLevel;
        nextLevel.reserve((level.size() + 1) / 2);
        for (std::size_t i = 0; i + 1 < level.size(); i += 2) {
            const NetId outNet = circuit.addNet();
            (void)circuit.addGate(GateType::Nand, std::vector<NetId>{level[i], level[i + 1]}, outNet);
            nextLevel.push_back(outNet);
        }
        if (level.size() % 2 == 1) {
            nextLevel.push_back(level.back());
        }
        level = std::move(nextLevel);
    }
    return built;
}

// Un DAG combinacional aleatorio y reproducible: las entradas de cada nueva
// puerta se toman unicamente de nets ya creados, lo que garantiza una red
// aciclica (estable).
BuiltCircuit buildRandomCombinational(std::size_t gateCount, uint32_t seed) {
    BuiltCircuit built;
    Circuit& circuit = built.circuit;
    const std::vector<NetId> none;

    std::mt19937 rng(seed);
    constexpr std::size_t kPrimaryInputs = 16;

    std::vector<NetId> available;
    available.reserve(kPrimaryInputs + gateCount);
    for (std::size_t i = 0; i < kPrimaryInputs; ++i) {
        const NetId net = circuit.addNet();
        const uint32_t ip = circuit.addGate(GateType::InputPin, none, net);
        built.primaryInputGates.push_back(ip);
        available.push_back(net);
    }

    static constexpr std::array<GateType, 6> kBinaryTypes{
        GateType::And, GateType::Or, GateType::Nand, GateType::Nor, GateType::Xor, GateType::Xnor,
    };
    std::uniform_int_distribution<std::size_t> typeDist(0, kBinaryTypes.size() - 1);
    std::uniform_int_distribution<int> unaryChance(0, 9);

    for (std::size_t i = 0; i < gateCount; ++i) {
        std::uniform_int_distribution<std::size_t> netDist(0, available.size() - 1);
        const NetId outNet = circuit.addNet();

        if (unaryChance(rng) == 0) {
            const NetId in = available[netDist(rng)];
            (void)circuit.addGate(GateType::Not, std::vector<NetId>{in}, outNet);
        } else {
            const NetId inA = available[netDist(rng)];
            const NetId inB = available[netDist(rng)];
            const GateType type = kBinaryTypes[typeDist(rng)];
            (void)circuit.addGate(type, std::vector<NetId>{inA, inB}, outNet);
        }
        available.push_back(outNet);
    }
    return built;
}

// Estimacion aproximada del limite inferior de memoria a partir del numero
// de elementos en los contenedores; excluye la sobrecarga del asignador y
// de la contabilidad interna, pero es estable y portable entre sistemas
// operativos.
std::size_t estimateMemoryBytes(const Circuit& circuit) {
    std::size_t bytes = 0;
    bytes += circuit.gateCount() * sizeof(Gate);
    bytes += circuit.totalInputSlotCount() * sizeof(NetId);
    for (NetId n = 0; n < static_cast<NetId>(circuit.netCount()); ++n) {
        bytes += circuit.driversOf(n).size() * sizeof(uint32_t);
        bytes += circuit.fanoutOf(n).size() * sizeof(FanoutEntry);
    }
    bytes += circuit.netCount() * (sizeof(LogicValue) * 2 + sizeof(uint32_t) + sizeof(bool));
    bytes += circuit.totalInputSlotCount() * sizeof(LogicValue);
    return bytes;
}

struct BenchmarkResult {
    std::string name;
    std::size_t gateCount = 0;
    std::size_t netCount = 0;
    uint64_t eventsProcessed = 0;
    bool stable = false;
    double milliseconds = 0.0;
    std::size_t approxMemoryBytes = 0;
};

BenchmarkResult runOnce(const std::string& name, BuiltCircuit built) {
    Simulator sim(built.circuit);

    const auto start = std::chrono::steady_clock::now();
    for (const uint32_t gate : built.primaryInputGates) {
        sim.setInput(gate, LogicValue::One);
    }
    const bool stable = sim.runUntilStable();
    const auto end = std::chrono::steady_clock::now();

    BenchmarkResult result;
    result.name = name;
    result.gateCount = built.circuit.gateCount();
    result.netCount = built.circuit.netCount();
    result.eventsProcessed = sim.stats().eventsProcessed;
    result.stable = stable;
    result.milliseconds = std::chrono::duration<double, std::milli>(end - start).count();
    result.approxMemoryBytes = estimateMemoryBytes(built.circuit);
    return result;
}

void printResult(const BenchmarkResult& r) {
    std::cout << std::left << std::setw(28) << r.name << std::right << std::setw(10) << r.gateCount
              << std::setw(10) << r.netCount << std::setw(14) << r.eventsProcessed << std::setw(12)
              << std::fixed << std::setprecision(2) << r.milliseconds << std::setw(14)
              << (static_cast<double>(r.approxMemoryBytes) / (1024.0 * 1024.0)) << std::setw(10)
              << (r.stable ? "stable" : "UNSTABLE")
              << '\n';
}

} // namespace

int main() {
    const std::vector<std::size_t> sizes{10'000, 100'000, 1'000'000};

    std::cout << std::left << std::setw(28) << "Benchmark" << std::right << std::setw(10) << "gates"
              << std::setw(10) << "nets" << std::setw(14) << "events" << std::setw(12) << "time(ms)"
              << std::setw(14) << "mem(MB)" << std::setw(10) << "status" << '\n';
    std::cout << std::string(98, '-') << '\n';

    for (const std::size_t size : sizes) {
        printResult(runOnce("InverterChain[" + std::to_string(size) + "]", buildInverterChain(size)));
        printResult(runOnce("NandTree[" + std::to_string(size) + "]", buildNandTree(size)));
        printResult(runOnce("RandomCombinational[" + std::to_string(size) + "]",
                             buildRandomCombinational(size, /*seed=*/12345U)));
    }

    return 0;
}
