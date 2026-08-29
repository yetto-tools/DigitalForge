#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/Circuit.hpp"
#include "core/GateType.hpp"
#include "core/LogicValue.hpp"
#include "core/Simulator.hpp"

using digitalforge::core::Circuit;
using digitalforge::core::GateType;
using digitalforge::core::LogicValue;
using digitalforge::core::NetId;
using digitalforge::core::Simulator;

TEST_CASE("Two-input AND gate driven by input pins", "[simulator][combinational]") {
    Circuit circuit;
    const NetId a = circuit.addNet();
    const NetId b = circuit.addNet();
    const NetId y = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ipA = circuit.addGate(GateType::InputPin, none, a);
    const uint32_t ipB = circuit.addGate(GateType::InputPin, none, b);
    (void)circuit.addGate(GateType::And, std::vector<NetId>{a, b}, y);

    Simulator sim(circuit);
    sim.setInput(ipA, LogicValue::One);
    sim.setInput(ipB, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::One);

    sim.setInput(ipA, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::Zero);
}

TEST_CASE("A small OR-of-ANDs combinational network", "[simulator][combinational]") {
    // y = (a & b) | (c & d)  -- red combinacional "OR de ANDs"
    Circuit circuit;
    const NetId a = circuit.addNet();
    const NetId b = circuit.addNet();
    const NetId c = circuit.addNet();
    const NetId d = circuit.addNet();
    const NetId ab = circuit.addNet();
    const NetId cd = circuit.addNet();
    const NetId y = circuit.addNet();
    const std::vector<NetId> none;

    const uint32_t ipA = circuit.addGate(GateType::InputPin, none, a);
    const uint32_t ipB = circuit.addGate(GateType::InputPin, none, b);
    const uint32_t ipC = circuit.addGate(GateType::InputPin, none, c);
    const uint32_t ipD = circuit.addGate(GateType::InputPin, none, d);
    (void)circuit.addGate(GateType::And, std::vector<NetId>{a, b}, ab);
    (void)circuit.addGate(GateType::And, std::vector<NetId>{c, d}, cd);
    (void)circuit.addGate(GateType::Or, std::vector<NetId>{ab, cd}, y);

    Simulator sim(circuit);
    sim.setInput(ipA, LogicValue::One);
    sim.setInput(ipB, LogicValue::One);
    sim.setInput(ipC, LogicValue::Zero);
    sim.setInput(ipD, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::One);

    sim.setInput(ipA, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::Zero);
}

TEST_CASE("Undriven net floats as HighImpedance before any stimulus", "[simulator][highz]") {
    Circuit circuit;
    const NetId isolated = circuit.addNet();
    const std::vector<NetId> none;
    (void)circuit.addGate(GateType::InputPin, none, isolated);

    Simulator sim(circuit);
    CHECK(sim.getNetValue(isolated) == LogicValue::HighImpedance);
}

TEST_CASE("A floating input degrades AND gate output to Unknown", "[simulator][unknown]") {
    Circuit circuit;
    const NetId a = circuit.addNet();
    const NetId b = circuit.addNet(); // intencionalmente nunca manejado
    const NetId y = circuit.addNet();
    const std::vector<NetId> none;

    const uint32_t ipA = circuit.addGate(GateType::InputPin, none, a);
    (void)circuit.addGate(GateType::InputPin, none, b);
    (void)circuit.addGate(GateType::And, std::vector<NetId>{a, b}, y);

    Simulator sim(circuit);
    sim.setInput(ipA, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::Unknown);
}

TEST_CASE("Two constant drivers disagreeing on the same net produce Error", "[simulator][conflict]") {
    Circuit circuit;
    const NetId shared = circuit.addNet();
    const std::vector<NetId> none;
    (void)circuit.addGate(GateType::ConstantZero, none, shared);
    (void)circuit.addGate(GateType::ConstantOne, none, shared);

    Simulator sim(circuit); // reset() inicializa ambas constantes durante la construccion
    CHECK(sim.getNetValue(shared) == LogicValue::Error);
}

TEST_CASE("Inverter chain of 50 stages resolves according to parity", "[simulator][chain]") {
    Circuit circuit;
    const std::vector<NetId> none;
    NetId current = circuit.addNet();
    const uint32_t ip = circuit.addGate(GateType::InputPin, none, current);

    constexpr int kStages = 50;
    for (int i = 0; i < kStages; ++i) {
        const NetId next = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{current}, next);
        current = next;
    }

    Simulator sim(circuit);
    sim.setInput(ip, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(current) == LogicValue::One); // numero par de inversiones
}

TEST_CASE("step() processes exactly one event at a time", "[simulator][step][order]") {
    Circuit circuit;
    const NetId a = circuit.addNet();
    const NetId y = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ip = circuit.addGate(GateType::InputPin, none, a);
    (void)circuit.addGate(GateType::Not, std::vector<NetId>{a}, y);

    Simulator sim(circuit);
    CHECK_FALSE(sim.step()); // todavia no hay nada en cola

    sim.setInput(ip, LogicValue::Zero);
    CHECK(sim.getNetValue(y) == LogicValue::HighImpedance); // aun no se ha propagado

    CHECK(sim.step()); // procesa el unico evento en cola
    CHECK(sim.getNetValue(y) == LogicValue::One);
    CHECK_FALSE(sim.step()); // la cola quedo vacia
}

TEST_CASE("reset() clears runtime state back to floating", "[simulator][reset]") {
    Circuit circuit;
    const NetId a = circuit.addNet();
    const NetId y = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ip = circuit.addGate(GateType::InputPin, none, a);
    (void)circuit.addGate(GateType::Not, std::vector<NetId>{a}, y);

    Simulator sim(circuit);
    sim.setInput(ip, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::Zero);

    sim.reset();
    CHECK(sim.getNetValue(a) == LogicValue::HighImpedance);
    CHECK(sim.getNetValue(y) == LogicValue::HighImpedance);
    CHECK(sim.stats().eventsProcessed == 0);
}

TEST_CASE("A gate's delay controls how many ticks downstream logic takes to observe a change",
          "[simulator][delay]") {
    // Dos cadenas paralelas Entrada -> NOT -> BUFFER, una con NOT de delay=1
    // (rapida) y otra con NOT de delay=10 (lenta); ambas alimentadas por el
    // mismo InputPin, asi que reciben el estimulo en el mismo instante
    // logico - solo el delay de cada NOT determina cuando su propio BUFFER
    // (y por lo tanto la net final) observa el cambio.
    Circuit circuit;
    const NetId a = circuit.addNet();
    const NetId midFast = circuit.addNet();
    const NetId midSlow = circuit.addNet();
    const NetId fastOut = circuit.addNet();
    const NetId slowOut = circuit.addNet();
    const std::vector<NetId> none;

    const uint32_t ip = circuit.addGate(GateType::InputPin, none, a);
    (void)circuit.addGate(GateType::Not, std::vector<NetId>{a}, midFast, /*delay=*/1);
    (void)circuit.addGate(GateType::Not, std::vector<NetId>{a}, midSlow, /*delay=*/10);
    (void)circuit.addGate(GateType::Buffer, std::vector<NetId>{midFast}, fastOut);
    (void)circuit.addGate(GateType::Buffer, std::vector<NetId>{midSlow}, slowOut);

    Simulator sim(circuit);
    sim.setInput(ip, LogicValue::Zero);

    REQUIRE(sim.step()); // evalua el NOT rapido -> agenda su buffer 1 tick despues
    REQUIRE(sim.step()); // evalua el NOT lento -> agenda su buffer 10 ticks despues
    REQUIRE(sim.step()); // procesa el evento del buffer rapido (el mas cercano en el tiempo)

    CHECK(sim.getNetValue(fastOut) == LogicValue::One);
    CHECK(sim.getNetValue(slowOut) == LogicValue::HighImpedance); // el buffer lento todavia no se proceso

    REQUIRE(sim.step()); // ahora si, el evento del buffer lento, agendado mucho mas tarde
    CHECK(sim.getNetValue(slowOut) == LogicValue::One);
    CHECK_FALSE(sim.step()); // la cola quedo vacia
}

TEST_CASE("DFlipFlop captures D only on a clean rising edge of CLK, ignoring D-only changes and falling edges",
          "[simulator][dflipflop]") {
    Circuit circuit;
    const NetId d = circuit.addNet();
    const NetId clk = circuit.addNet();
    const NetId q = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ipD = circuit.addGate(GateType::InputPin, none, d);
    const uint32_t ipClk = circuit.addGate(GateType::InputPin, none, clk);
    (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d, clk}, q);

    Simulator sim(circuit);
    sim.setInput(ipD, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    // Q arranca en Zero, no flotante (ver Simulator::seedSourceGates()) -
    // sin flanco todavia sigue en ese valor inicial, D=1 no lo toco.
    CHECK(sim.getNetValue(q) == LogicValue::Zero);

    sim.setInput(ipClk, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::Zero); // CLK bajo no dispara nada

    sim.setInput(ipClk, LogicValue::One); // flanco ascendente confirmado (CLK previo era Zero)
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::One); // captura D=1

    sim.setInput(ipD, LogicValue::Zero); // D cambia solo, CLK se mantiene en 1
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::One); // Q no se mueve: no es un latch transparente

    sim.setInput(ipClk, LogicValue::Zero); // flanco descendente
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::One); // los flancos descendentes tampoco disparan

    sim.setInput(ipClk, LogicValue::One); // segundo flanco ascendente, ahora con D=0
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::Zero);
}

TEST_CASE("DFlipFlop yields Unknown on a first rising edge without a confirmed prior Zero on CLK",
          "[simulator][dflipflop][unknown]") {
    Circuit circuit;
    const NetId d = circuit.addNet();
    const NetId clk = circuit.addNet();
    const NetId q = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ipD = circuit.addGate(GateType::InputPin, none, d);
    const uint32_t ipClk = circuit.addGate(GateType::InputPin, none, clk);
    (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d, clk}, q);

    Simulator sim(circuit);
    sim.setInput(ipD, LogicValue::One);
    REQUIRE(sim.runUntilStable());

    // CLK nunca se vio en Zero (arranca flotante) - el "flanco" no es confiable.
    sim.setInput(ipClk, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::Unknown);
}

TEST_CASE("DFlipFlop PRE/CLR (4-input form) force Q asynchronously, without waiting for a CLK edge, and Q holds "
          "the forced value after release",
          "[simulator][dflipflop][presetclear]") {
    Circuit circuit;
    const NetId d = circuit.addNet();
    const NetId clk = circuit.addNet();
    const NetId pre = circuit.addNet();
    const NetId clr = circuit.addNet();
    const NetId q = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ipD = circuit.addGate(GateType::InputPin, none, d);
    const uint32_t ipClk = circuit.addGate(GateType::InputPin, none, clk);
    const uint32_t ipPre = circuit.addGate(GateType::InputPin, none, pre);
    const uint32_t ipClr = circuit.addGate(GateType::InputPin, none, clr);
    (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d, clk, pre, clr}, q);

    Simulator sim(circuit);
    sim.setInput(ipD, LogicValue::Zero);
    sim.setInput(ipClk, LogicValue::Zero);
    sim.setInput(ipPre, LogicValue::Zero);
    sim.setInput(ipClr, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::Zero); // PRE/CLR inactivos, Q ya arranca en Zero

    sim.setInput(ipPre, LogicValue::One); // PRE fuerza Q=1 de inmediato, CLK sigue en 0
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::One);

    sim.setInput(ipPre, LogicValue::Zero); // se suelta PRE sin ningun flanco nuevo
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::One); // Q retiene el valor forzado

    sim.setInput(ipClr, LogicValue::One); // CLR fuerza Q=0 de inmediato
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::Zero);

    sim.setInput(ipClr, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::Zero); // idem, retiene
}

TEST_CASE("DFlipFlop PRE active overrides a CLK edge: D is not captured while PRE holds Q at One",
          "[simulator][dflipflop][presetclear]") {
    Circuit circuit;
    const NetId d = circuit.addNet();
    const NetId clk = circuit.addNet();
    const NetId pre = circuit.addNet();
    const NetId clr = circuit.addNet();
    const NetId q = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ipD = circuit.addGate(GateType::InputPin, none, d);
    const uint32_t ipClk = circuit.addGate(GateType::InputPin, none, clk);
    const uint32_t ipPre = circuit.addGate(GateType::InputPin, none, pre);
    const uint32_t ipClr = circuit.addGate(GateType::InputPin, none, clr);
    (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d, clk, pre, clr}, q);

    Simulator sim(circuit);
    sim.setInput(ipD, LogicValue::Zero); // D=0: si el flanco capturara, Q bajaria a 0
    sim.setInput(ipClk, LogicValue::Zero);
    sim.setInput(ipClr, LogicValue::Zero);
    sim.setInput(ipPre, LogicValue::One); // PRE ya activo antes del flanco
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::One);

    sim.setInput(ipClk, LogicValue::One); // flanco ascendente confirmado, con PRE todavia activo
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::One); // PRE manda: D no se captura
}

TEST_CASE("DFlipFlop PRE and CLR active at the same time yields Error", "[simulator][dflipflop][presetclear]") {
    Circuit circuit;
    const NetId d = circuit.addNet();
    const NetId clk = circuit.addNet();
    const NetId pre = circuit.addNet();
    const NetId clr = circuit.addNet();
    const NetId q = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ipD = circuit.addGate(GateType::InputPin, none, d);
    const uint32_t ipClk = circuit.addGate(GateType::InputPin, none, clk);
    const uint32_t ipPre = circuit.addGate(GateType::InputPin, none, pre);
    const uint32_t ipClr = circuit.addGate(GateType::InputPin, none, clr);
    (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d, clk, pre, clr}, q);

    Simulator sim(circuit);
    sim.setInput(ipD, LogicValue::Zero);
    sim.setInput(ipClk, LogicValue::Zero);
    sim.setInput(ipPre, LogicValue::Zero);
    sim.setInput(ipClr, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());

    sim.setInput(ipPre, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::One);

    sim.setInput(ipClr, LogicValue::One); // PRE y CLR activos a la vez: combinacion invalida clasica
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(q) == LogicValue::Error);
}

TEST_CASE("TriStateBuffer passes D through only while EN is a clean One", "[simulator][tristate]") {
    Circuit circuit;
    const NetId d = circuit.addNet();
    const NetId en = circuit.addNet();
    const NetId y = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ipD = circuit.addGate(GateType::InputPin, none, d);
    const uint32_t ipEn = circuit.addGate(GateType::InputPin, none, en);
    (void)circuit.addGate(GateType::TriStateBuffer, std::vector<NetId>{d, en}, y);

    Simulator sim(circuit);
    sim.setInput(ipD, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::HighImpedance); // EN flotante -> deshabilitado

    sim.setInput(ipEn, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::HighImpedance); // EN=0 -> deshabilitado

    sim.setInput(ipEn, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::One); // EN=1 -> repite D

    sim.setInput(ipD, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(y) == LogicValue::Zero);
}

TEST_CASE("Two enabled TriStateBuffers disagreeing on the same net produce Error", "[simulator][tristate][conflict]") {
    Circuit circuit;
    const NetId d1 = circuit.addNet();
    const NetId en1 = circuit.addNet();
    const NetId d2 = circuit.addNet();
    const NetId en2 = circuit.addNet();
    const NetId shared = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ipD1 = circuit.addGate(GateType::InputPin, none, d1);
    const uint32_t ipEn1 = circuit.addGate(GateType::InputPin, none, en1);
    const uint32_t ipD2 = circuit.addGate(GateType::InputPin, none, d2);
    const uint32_t ipEn2 = circuit.addGate(GateType::InputPin, none, en2);
    (void)circuit.addGate(GateType::TriStateBuffer, std::vector<NetId>{d1, en1}, shared);
    (void)circuit.addGate(GateType::TriStateBuffer, std::vector<NetId>{d2, en2}, shared);

    Simulator sim(circuit);
    sim.setInput(ipD1, LogicValue::One);
    sim.setInput(ipEn1, LogicValue::One);
    sim.setInput(ipD2, LogicValue::Zero);
    sim.setInput(ipEn2, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(shared) == LogicValue::Error);
}

TEST_CASE("A self-feeding inverter oscillation is detected and safely latched", "[simulator][oscillation]") {
    // netA es manejado por un InputPin (usado solo para inyectar un pulso
    // breve) y por una puerta NOT cuya propia entrada es netA, asi que una
    // vez perturbada sigue alternando para siempre a menos que el simulador
    // intervenga.
    Circuit circuit;
    const NetId netA = circuit.addNet();
    const std::vector<NetId> none;
    const uint32_t ip = circuit.addGate(GateType::InputPin, none, netA);
    (void)circuit.addGate(GateType::Not, std::vector<NetId>{netA}, netA);

    Simulator sim(circuit);
    sim.setInput(ip, LogicValue::Zero);
    sim.setInput(ip, LogicValue::HighImpedance);

    const bool stable = sim.runUntilStable();

    CHECK(stable);
    CHECK(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(netA) == LogicValue::Unknown);
}
