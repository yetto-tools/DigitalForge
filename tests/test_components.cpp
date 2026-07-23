// Tests de componentes: los de FASE B1 son todos de 1 bit unicamente, asi
// que la cobertura de "ancho de bit" queda ejercitada implicitamente por
// cada test siguiente en vez de como un caso separado. Los Plexers y los
// componentes de arithmetic.* (fases posteriores) exponen un ancho
// configurable via pines individuales de 1 bit (selectBits/bits) - no un bus
// real, que sigue fuera de alcance - por eso sus tests si varian ese ancho
// explicitamente.

#include <array>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "components/BasicComponentLibrary.hpp"
#include "components/ComponentRegistry.hpp"
#include "core/Circuit.hpp"
#include "core/Simulator.hpp"

using digitalforge::components::ComponentInstance;
using digitalforge::components::ComponentRegistry;
using digitalforge::components::ComponentSimBinding;
using digitalforge::components::hexDigitSegments;
using digitalforge::components::hexDisplayState;
using digitalforge::components::inputInitialValue;
using digitalforge::components::ledIsLit;
using digitalforge::components::ledMatrixCellIsLit;
using digitalforge::components::ledMatrixMultiplexedCellIsLit;
using digitalforge::components::parseLogicValueEnum;
using digitalforge::components::PropertyValue;
using digitalforge::components::registerBasicComponentLibrary;
using digitalforge::components::segmentIsLit;
using digitalforge::components::terminalCharacter;
using digitalforge::core::Circuit;
using digitalforge::core::LogicValue;
using digitalforge::core::NetId;
using digitalforge::core::Simulator;

namespace {
ComponentRegistry makeRegistry() {
    ComponentRegistry registry;
    registerBasicComponentLibrary(registry);
    return registry;
}
} // namespace

TEST_CASE("Basic library registers exactly the current set of components", "[components][registry]") {
    const ComponentRegistry registry = makeRegistry();
    for (const char* id :
         {"wiring.input", "wiring.clock", "wiring.output", "wiring.constant", "wiring.pullResistor",
          "wiring.powerOnReset", "wiring.doNotConnect", "wiring.tunnel", "wiring.ground", "wiring.transistor",
          "wiring.transmissionGate", "gates.and", "gates.or", "gates.not", "gates.buffer", "gates.tristateBuffer",
          "gates.tristateInverter", "gates.nand", "gates.nor", "gates.xor", "gates.xnor", "plexers.decoder",
          "plexers.multiplexer", "plexers.demultiplexer", "plexers.priorityEncoder", "arithmetic.adder",
          "arithmetic.subtractor", "arithmetic.comparator", "memory.srLatch", "memory.dFlipFlop",
          "memory.jkFlipFlop", "memory.register", "structural.subcircuit", "io.led", "debug.probe",
          "io.seven_segment", "io.hexDisplay", "io.ledMatrix", "io.terminal", "ic74ls.bcdDriver",
          "ic74ls.quadNand2", "ic74ls.quadNor2", "ic74ls.quadAnd2", "ic74ls.quadOr2", "ic74ls.quadXor2",
          "ic74ls.hexInverter", "ic74ls.dualDFlipFlop", "ic74ls.dualJkFlipFlop", "ic74ls.decadeCounter",
          "ic74ls.mux8to1", "ic74ls.decoder3to8", "ic74ls.adder4bit", "ic74ls.comparator4bit"}) {
        CHECK(registry.contains(id));
    }
    CHECK(registry.registeredTypeIds().size() == 53);
}

TEST_CASE("BUFFER gate component passes its input through unchanged", "[components][gates][buffer]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance in0 = registry.create("wiring.input", 0);
    ComponentInstance bufferGate = registry.create("gates.buffer", 1);
    ComponentInstance out = registry.create("wiring.output", 2);

    const NetId netA = circuit.addNet();
    const NetId netY = circuit.addNet();
    in0.bindPin(0, netA);
    bufferGate.bindPin(0, netA);
    bufferGate.bindPin(1, netY);
    out.bindPin(0, netY);

    in0.buildSimulation(circuit);
    bufferGate.buildSimulation(circuit);
    out.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(in0.simBinding().gateIndex, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::One);

    sim.setInput(in0.simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::Zero);
}

TEST_CASE("Tri-state buffer component repeats D only while OE is enabled", "[components][gates][tristate]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance inD = registry.create("wiring.input", 0);
    ComponentInstance inOe = registry.create("wiring.input", 1);
    ComponentInstance tsBuffer = registry.create("gates.tristateBuffer", 2);
    ComponentInstance out = registry.create("wiring.output", 3);

    const NetId netD = circuit.addNet();
    const NetId netOe = circuit.addNet();
    const NetId netY = circuit.addNet();

    inD.bindPin(0, netD);
    inOe.bindPin(0, netOe);
    tsBuffer.bindPin(0, netD);
    tsBuffer.bindPin(1, netOe);
    tsBuffer.bindPin(2, netY);
    out.bindPin(0, netY);

    inD.buildSimulation(circuit);
    inOe.buildSimulation(circuit);
    tsBuffer.buildSimulation(circuit);
    out.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(inD.simBinding().gateIndex, LogicValue::One);
    sim.setInput(inOe.simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::HighImpedance);

    sim.setInput(inOe.simBinding().gateIndex, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::One);
}

TEST_CASE("Tri-state inverter component repeats NOT(D) only while OE is enabled", "[components][gates][tristate]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance inD = registry.create("wiring.input", 0);
    ComponentInstance inOe = registry.create("wiring.input", 1);
    ComponentInstance tsInverter = registry.create("gates.tristateInverter", 2);
    ComponentInstance out = registry.create("wiring.output", 3);

    const NetId netD = circuit.addNet();
    const NetId netOe = circuit.addNet();
    const NetId netY = circuit.addNet();

    inD.bindPin(0, netD);
    inOe.bindPin(0, netOe);
    tsInverter.bindPin(0, netD);
    tsInverter.bindPin(1, netOe);
    tsInverter.bindPin(2, netY);
    out.bindPin(0, netY);

    inD.buildSimulation(circuit);
    inOe.buildSimulation(circuit);
    tsInverter.buildSimulation(circuit);
    out.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(inD.simBinding().gateIndex, LogicValue::One);
    sim.setInput(inOe.simBinding().gateIndex, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::Zero); // NOT(1)

    sim.setInput(inOe.simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::HighImpedance);
}

TEST_CASE("invertMask negates only the selected inputs of a variadic gate before it evaluates",
          "[components][gates][invertMask]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    // AND de 2 entradas con bit 0 invertido se comporta como (NOT A) AND B.
    ComponentInstance in0 = registry.create("wiring.input", 0);
    ComponentInstance in1 = registry.create("wiring.input", 1);
    ComponentInstance andGate =
        registry.create("gates.and", 2, {{"inputCount", PropertyValue{uint64_t{2}}}, {"invertMask", PropertyValue{uint64_t{1}}}});
    ComponentInstance out = registry.create("wiring.output", 3);

    const NetId netA = circuit.addNet();
    const NetId netB = circuit.addNet();
    const NetId netY = circuit.addNet();

    in0.bindPin(0, netA);
    in1.bindPin(0, netB);
    andGate.bindPin(0, netA);
    andGate.bindPin(1, netB);
    andGate.bindPin(2, netY);
    out.bindPin(0, netY);

    in0.buildSimulation(circuit);
    in1.buildSimulation(circuit);
    andGate.buildSimulation(circuit);
    out.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(in0.simBinding().gateIndex, LogicValue::Zero); // A=0 -> NOT A=1
    sim.setInput(in1.simBinding().gateIndex, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::One);

    sim.setInput(in0.simBinding().gateIndex, LogicValue::One); // A=1 -> NOT A=0
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::Zero);
}

TEST_CASE("AND gate component drives the correct truth table", "[components][gates][combinational]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance in0 = registry.create("wiring.input", 0);
    ComponentInstance in1 = registry.create("wiring.input", 1);
    ComponentInstance andGate = registry.create("gates.and", 2);
    ComponentInstance out = registry.create("wiring.output", 3);

    const NetId netA = circuit.addNet();
    const NetId netB = circuit.addNet();
    const NetId netY = circuit.addNet();

    in0.bindPin(0, netA);
    in1.bindPin(0, netB);
    andGate.bindPin(0, netA);
    andGate.bindPin(1, netB);
    andGate.bindPin(2, netY);
    out.bindPin(0, netY);

    in0.buildSimulation(circuit);
    in1.buildSimulation(circuit);
    andGate.buildSimulation(circuit);
    out.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(in0.simBinding().gateIndex, LogicValue::One);
    sim.setInput(in1.simBinding().gateIndex, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::One);

    sim.setInput(in0.simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(out.simBinding().observedNets[0]) == LogicValue::Zero);
}

TEST_CASE("A floating (Z) input degrades AND output to Unknown (X)", "[components][unknown][highz]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance in0 = registry.create("wiring.input", 0, {{"initialValue", PropertyValue{std::string("1")}}});
    ComponentInstance in1 = registry.create("wiring.input", 1); // valor por defecto "Z", nunca manejado
    ComponentInstance andGate = registry.create("gates.and", 2);
    ComponentInstance probe = registry.create("debug.probe", 3);

    const NetId netA = circuit.addNet();
    const NetId netB = circuit.addNet();
    const NetId netY = circuit.addNet();

    in0.bindPin(0, netA);
    in1.bindPin(0, netB);
    andGate.bindPin(0, netA);
    andGate.bindPin(1, netB);
    andGate.bindPin(2, netY);
    probe.bindPin(0, netY);

    in0.buildSimulation(circuit);
    in1.buildSimulation(circuit);
    andGate.buildSimulation(circuit);
    probe.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(in0.simBinding().gateIndex, inputInitialValue(in0));
    // in1 se deja en su valor por defecto Z: nunca se llama a setInput para el.
    REQUIRE(sim.runUntilStable());

    CHECK(sim.getNetValue(netB) == LogicValue::HighImpedance);
    CHECK(sim.getNetValue(probe.simBinding().observedNets[0]) == LogicValue::Unknown);
}

TEST_CASE("Two constants disagreeing on the same net produce Error, and the LED reflects it", "[components][conflict]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance zero = registry.create("wiring.constant", 0, {{"value", PropertyValue{std::string("0")}}});
    ComponentInstance one = registry.create("wiring.constant", 1, {{"value", PropertyValue{std::string("1")}}});
    ComponentInstance led = registry.create("io.led", 2);

    const NetId shared = circuit.addNet();
    zero.bindPin(0, shared);
    one.bindPin(0, shared);
    led.bindPin(0, shared);

    zero.buildSimulation(circuit);
    one.buildSimulation(circuit);
    led.buildSimulation(circuit);

    Simulator sim(circuit); // las constantes se inicializan durante la construccion/reset
    CHECK(sim.getNetValue(shared) == LogicValue::Error);
    CHECK_FALSE(ledIsLit(led, sim.getNetValue(shared)));
}

TEST_CASE("parseLogicValueEnum accepts 0/1/Z/X and rejects anything else", "[components][parsing]") {
    CHECK(parseLogicValueEnum("0") == LogicValue::Zero);
    CHECK(parseLogicValueEnum("1") == LogicValue::One);
    CHECK(parseLogicValueEnum("Z") == LogicValue::HighImpedance);
    CHECK(parseLogicValueEnum("X") == LogicValue::Unknown);
    CHECK_THROWS_AS(parseLogicValueEnum("bogus"), std::invalid_argument);
}

TEST_CASE("LED honors activeHigh when deciding whether it is lit", "[components][led]") {
    ComponentRegistry registry = makeRegistry();
    ComponentInstance activeHighLed = registry.create("io.led", 0);
    ComponentInstance activeLowLed = registry.create("io.led", 1, {{"activeHigh", PropertyValue{false}}});

    CHECK(ledIsLit(activeHighLed, LogicValue::One));
    CHECK_FALSE(ledIsLit(activeHighLed, LogicValue::Zero));
    CHECK_FALSE(ledIsLit(activeHighLed, LogicValue::Unknown));
    CHECK_FALSE(ledIsLit(activeHighLed, LogicValue::HighImpedance));
    CHECK_FALSE(ledIsLit(activeHighLed, LogicValue::Error));

    CHECK(ledIsLit(activeLowLed, LogicValue::Zero));
    CHECK_FALSE(ledIsLit(activeLowLed, LogicValue::One));
}

TEST_CASE("Serializing and deserializing a component instance round-trips its properties", "[components][serialization]") {
    ComponentRegistry registry = makeRegistry();
    ComponentInstance original =
        registry.create("gates.and", 42, {{"inputCount", PropertyValue{uint64_t{4}}}, {"label", PropertyValue{std::string("U1")}}});

    const nlohmann::json json = original.toJson();
    CHECK(json.at("typeId").get<std::string>() == "gates.and");
    CHECK(json.at("instanceId").get<uint32_t>() == 42);
    CHECK(json.at("properties").at("inputCount").get<uint64_t>() == 4);

    const ComponentInstance restored = ComponentInstance::fromJson(registry.definition("gates.and"), json);
    CHECK(restored.instanceId() == 42);
    CHECK(std::get<uint64_t>(restored.property("inputCount")) == 4);
    CHECK(std::get<std::string>(restored.property("label")) == "U1");
    // 4 entradas + 1 salida, re-derivadas exclusivamente a partir de las propiedades deserializadas.
    CHECK(restored.pins().size() == 5);
}

TEST_CASE("Deserializing with a mismatched typeId is rejected", "[components][serialization][validation]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance andGate = registry.create("gates.and", 0);
    const nlohmann::json json = andGate.toJson();

    CHECK_THROWS_AS(ComponentInstance::fromJson(registry.definition("gates.or"), json), std::invalid_argument);
}

TEST_CASE("Invalid properties are rejected at creation and at setProperty", "[components][validation]") {
    ComponentRegistry registry = makeRegistry();

    // Por debajo del inputCount minimo de la puerta AND, que es 2.
    CHECK_THROWS_AS(registry.create("gates.and", 0, {{"inputCount", PropertyValue{uint64_t{1}}}}), std::invalid_argument);

    // Tipo alternativo incorrecto para una propiedad UnsignedInteger.
    CHECK_THROWS_AS(registry.create("gates.and", 0, {{"inputCount", PropertyValue{true}}}), std::invalid_argument);

    // Id de propiedad desconocido.
    CHECK_THROWS_AS(registry.create("gates.and", 0, {{"bogus", PropertyValue{uint64_t{2}}}}), std::invalid_argument);

    ComponentInstance andGate = registry.create("gates.and", 0);
    CHECK_THROWS_AS(andGate.setProperty("inputCount", PropertyValue{uint64_t{1}}), std::invalid_argument);
    CHECK_THROWS_AS(andGate.setProperty("nonexistent", PropertyValue{uint64_t{2}}), std::invalid_argument);

    // Un cambio valido tiene exito y los pines se recalculan.
    andGate.setProperty("inputCount", PropertyValue{uint64_t{5}});
    CHECK(andGate.pins().size() == 6);
}

TEST_CASE("Changing a property drops any existing wiring and simulation binding", "[components][properties][wiring]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;
    ComponentInstance andGate = registry.create("gates.and", 0);

    andGate.bindPin(0, circuit.addNet());
    andGate.bindPin(1, circuit.addNet());
    andGate.bindPin(2, circuit.addNet());
    CHECK(andGate.allPinsBound());

    andGate.setProperty("inputCount", PropertyValue{uint64_t{3}});
    CHECK_FALSE(andGate.allPinsBound()); // ahora hay 4 pines, todos sin conectar de nuevo
    CHECK(andGate.pins().size() == 4);
}

TEST_CASE("Changing a cosmetic property preserves existing wiring and simulation binding",
          "[components][properties][wiring]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;
    ComponentInstance andGate = registry.create("gates.and", 0);

    andGate.bindPin(0, circuit.addNet());
    andGate.bindPin(1, circuit.addNet());
    andGate.bindPin(2, circuit.addNet());
    andGate.buildSimulation(circuit);
    const auto gateIndexBefore = andGate.simBinding().gateIndex;

    andGate.setProperty("label", PropertyValue{std::string("U7")});

    CHECK(andGate.allPinsBound());
    CHECK(andGate.simBinding().kind == ComponentSimBinding::Kind::Driver);
    CHECK(andGate.simBinding().gateIndex == gateIndexBefore);
    CHECK(std::get<std::string>(andGate.property("label")) == "U7");
}

TEST_CASE("buildSimulation refuses to run while a pin is still unconnected", "[components][wiring][validation]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;
    ComponentInstance notGate = registry.create("gates.not", 0);

    CHECK_FALSE(notGate.allPinsBound());
    CHECK_THROWS_AS(notGate.buildSimulation(circuit), std::logic_error);

    notGate.bindPin(0, circuit.addNet());
    notGate.bindPin(1, circuit.addNet());
    CHECK(notGate.allPinsBound());
    CHECK_NOTHROW(notGate.buildSimulation(circuit));
    CHECK(notGate.simBinding().kind == ComponentSimBinding::Kind::Driver);
}

TEST_CASE("Unbinding a pin makes the component unready to build simulation again", "[components][wiring]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;
    ComponentInstance notGate = registry.create("gates.not", 0);
    notGate.bindPin(0, circuit.addNet());
    notGate.bindPin(1, circuit.addNet());
    CHECK(notGate.allPinsBound());

    notGate.unbindPin(0);
    CHECK_FALSE(notGate.allPinsBound());
    CHECK_THROWS_AS(notGate.buildSimulation(circuit), std::logic_error);
}

TEST_CASE("A small Entrada -> AND -> LED circuit stabilizes correctly end to end", "[components][simulator][stability]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance in0 = registry.create("wiring.input", 0, {{"initialValue", PropertyValue{std::string("1")}}});
    ComponentInstance in1 = registry.create("wiring.input", 1, {{"initialValue", PropertyValue{std::string("1")}}});
    ComponentInstance andGate = registry.create("gates.and", 2);
    ComponentInstance led = registry.create("io.led", 3);

    const NetId netA = circuit.addNet();
    const NetId netB = circuit.addNet();
    const NetId netY = circuit.addNet();

    in0.bindPin(0, netA);
    in1.bindPin(0, netB);
    andGate.bindPin(0, netA);
    andGate.bindPin(1, netB);
    andGate.bindPin(2, netY);
    led.bindPin(0, netY);

    in0.buildSimulation(circuit);
    in1.buildSimulation(circuit);
    andGate.buildSimulation(circuit);
    led.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(in0.simBinding().gateIndex, inputInitialValue(in0));
    sim.setInput(in1.simBinding().gateIndex, inputInitialValue(in1));

    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(ledIsLit(led, sim.getNetValue(led.simBinding().observedNets[0])));
}

TEST_CASE("Seven-segment display exposes 7 or 8 pins depending on hasDecimalPoint", "[components][seven_segment]") {
    ComponentRegistry registry = makeRegistry();

    const ComponentInstance withDot = registry.create("io.seven_segment", 0);
    CHECK(withDot.pins().size() == 8);
    CHECK(withDot.pins().back().name == "dot");

    const ComponentInstance withoutDot =
        registry.create("io.seven_segment", 1, {{"hasDecimalPoint", PropertyValue{false}}});
    CHECK(withoutDot.pins().size() == 7);
}

TEST_CASE("Seven-segment display honors commonAnode/commonCathode when lighting a segment",
          "[components][seven_segment]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance commonCathode = registry.create("io.seven_segment", 0); // por defecto: activo en alto
    const ComponentInstance commonAnode =
        registry.create("io.seven_segment", 1, {{"commonAnode", PropertyValue{true}}});

    CHECK(segmentIsLit(commonCathode, LogicValue::One));
    CHECK_FALSE(segmentIsLit(commonCathode, LogicValue::Zero));

    CHECK(segmentIsLit(commonAnode, LogicValue::Zero));
    CHECK_FALSE(segmentIsLit(commonAnode, LogicValue::One));
}

TEST_CASE("Seven-segment display never lights a segment on X, Z or Error", "[components][seven_segment][unknown][highz]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance display = registry.create("io.seven_segment", 0);

    CHECK_FALSE(segmentIsLit(display, LogicValue::Unknown));
    CHECK_FALSE(segmentIsLit(display, LogicValue::HighImpedance));
    CHECK_FALSE(segmentIsLit(display, LogicValue::Error));
}

TEST_CASE("A floating segment reads back as HighImpedance through the simulator", "[components][seven_segment][highz]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance display = registry.create("io.seven_segment", 0);
    std::vector<NetId> segmentNets;
    for (std::size_t i = 0; i < display.pins().size(); ++i) {
        const NetId net = circuit.addNet();
        display.bindPin(i, net);
        segmentNets.push_back(net);
    }
    display.buildSimulation(circuit);

    Simulator sim(circuit);
    REQUIRE(sim.runUntilStable());
    for (const NetId net : segmentNets) {
        CHECK(sim.getNetValue(net) == LogicValue::HighImpedance);
        CHECK_FALSE(segmentIsLit(display, sim.getNetValue(net)));
    }
}

TEST_CASE("Two constants disagreeing on a segment's net produce Error", "[components][seven_segment][conflict]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance display = registry.create("io.seven_segment", 0, {{"hasDecimalPoint", PropertyValue{false}}});
    ComponentInstance zero = registry.create("wiring.constant", 1, {{"value", PropertyValue{std::string("0")}}});
    ComponentInstance one = registry.create("wiring.constant", 2, {{"value", PropertyValue{std::string("1")}}});

    const NetId segmentA = circuit.addNet();
    display.bindPin(0, segmentA); // "a"  (segmento a del display)
    for (std::size_t i = 1; i < display.pins().size(); ++i) {
        display.bindPin(i, circuit.addNet());
    }
    zero.bindPin(0, segmentA);
    one.bindPin(0, segmentA);

    display.buildSimulation(circuit);
    zero.buildSimulation(circuit);
    one.buildSimulation(circuit);

    Simulator sim(circuit);
    CHECK(sim.getNetValue(segmentA) == LogicValue::Error);
    CHECK_FALSE(segmentIsLit(display, sim.getNetValue(segmentA)));
}

TEST_CASE("Seven-segment display serializes and deserializes its properties", "[components][seven_segment][serialization]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance original = registry.create(
        "io.seven_segment", 7,
        {{"commonAnode", PropertyValue{true}}, {"hasDecimalPoint", PropertyValue{false}},
         {"label", PropertyValue{std::string("DISP1")}}});

    const nlohmann::json json = original.toJson();
    const ComponentInstance restored = ComponentInstance::fromJson(registry.definition("io.seven_segment"), json);

    CHECK(restored.instanceId() == 7);
    CHECK(std::get<bool>(restored.property("commonAnode")) == true);
    CHECK(std::get<bool>(restored.property("hasDecimalPoint")) == false);
    CHECK(std::get<std::string>(restored.property("label")) == "DISP1");
    CHECK(restored.pins().size() == 7); // sin pin de punto decimal
}

TEST_CASE("Seven-segment display rejects invalid properties", "[components][seven_segment][validation]") {
    ComponentRegistry registry = makeRegistry();

    CHECK_THROWS_AS(registry.create("io.seven_segment", 0, {{"commonAnode", PropertyValue{uint64_t{1}}}}),
                     std::invalid_argument);
    CHECK_THROWS_AS(registry.create("io.seven_segment", 0, {{"bogus", PropertyValue{true}}}), std::invalid_argument);

    ComponentInstance display = registry.create("io.seven_segment", 0);
    CHECK_THROWS_AS(display.setProperty("hasDecimalPoint", PropertyValue{std::string("no")}), std::invalid_argument);
}

TEST_CASE("Seven-segment display connection/disconnection gates buildSimulation", "[components][seven_segment][wiring]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;
    ComponentInstance display = registry.create("io.seven_segment", 0, {{"hasDecimalPoint", PropertyValue{false}}});

    CHECK_FALSE(display.allPinsBound());
    CHECK_THROWS_AS(display.buildSimulation(circuit), std::logic_error);

    for (std::size_t i = 0; i < display.pins().size(); ++i) {
        display.bindPin(i, circuit.addNet());
    }
    CHECK(display.allPinsBound());
    CHECK_NOTHROW(display.buildSimulation(circuit));
    CHECK(display.simBinding().kind == ComponentSimBinding::Kind::Sink);
    CHECK(display.simBinding().observedNets.size() == 7);

    display.unbindPin(3);
    CHECK_FALSE(display.allPinsBound());
    CHECK_THROWS_AS(display.buildSimulation(circuit), std::logic_error);
}

TEST_CASE("A digit driven through inputs into the seven-segment display is stable end to end",
          "[components][seven_segment][simulator][stability]") {
    // Representa el '0': a,b,c,d,e,f encendidos, g apagado (catodo comun: activo en alto).
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance display = registry.create("io.seven_segment", 0, {{"hasDecimalPoint", PropertyValue{false}}});
    const std::array<bool, 7> pattern{true, true, true, true, true, true, false};

    std::vector<ComponentInstance> sources;
    std::vector<NetId> nets;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const std::string value = pattern[i] ? "1" : "0";
        sources.push_back(
            registry.create("wiring.input", static_cast<uint32_t>(i + 1), {{"initialValue", PropertyValue{value}}}));
        const NetId net = circuit.addNet();
        nets.push_back(net);
        sources.back().bindPin(0, net);
        display.bindPin(i, net);
    }

    for (ComponentInstance& source : sources) {
        source.buildSimulation(circuit);
    }
    display.buildSimulation(circuit);

    Simulator sim(circuit);
    for (ComponentInstance& source : sources) {
        sim.setInput(source.simBinding().gateIndex, inputInitialValue(source));
    }
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);

    for (std::size_t i = 0; i < pattern.size(); ++i) {
        CHECK(segmentIsLit(display, sim.getNetValue(nets[i])) == pattern[i]);
    }
}

TEST_CASE("bodyColor accepts '#RRGGBB' hex strings and rejects anything else", "[components][properties][color]") {
    ComponentRegistry registry = makeRegistry();

    // gates.and tiene bodyColor (no es io.led/io.seven_segment/wiring.input,
    // que quedan afuera - ver makeBodyColorProperty).
    ComponentInstance andGate =
        registry.create("gates.and", 0, {{"bodyColor", PropertyValue{std::string("#3366CC")}}});
    CHECK(std::get<std::string>(andGate.property("bodyColor")) == "#3366CC");

    CHECK_THROWS_AS(registry.create("gates.and", 1, {{"bodyColor", PropertyValue{std::string("3366CC")}}}),
                    std::invalid_argument); // falta el '#'
    CHECK_THROWS_AS(registry.create("gates.and", 2, {{"bodyColor", PropertyValue{std::string("#ZZZZZZ")}}}),
                    std::invalid_argument); // no son digitos hex
    CHECK_THROWS_AS(registry.create("gates.and", 3, {{"bodyColor", PropertyValue{std::string("#FFF")}}}),
                    std::invalid_argument); // formato corto, no soportado
}

TEST_CASE("customWidth/customHeight default to 0 (automatic) and are settable", "[components][properties][size]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance defaultSized = registry.create("gates.or", 0);
    CHECK(std::get<uint64_t>(defaultSized.property("customWidth")) == 0);
    CHECK(std::get<uint64_t>(defaultSized.property("customHeight")) == 0);

    const ComponentInstance customSized = registry.create(
        "gates.or", 1, {{"customWidth", PropertyValue{uint64_t{80}}}, {"customHeight", PropertyValue{uint64_t{40}}}});
    CHECK(std::get<uint64_t>(customSized.property("customWidth")) == 80);
    CHECK(std::get<uint64_t>(customSized.property("customHeight")) == 40);
}

TEST_CASE("notes accepts arbitrary free text and never affects pins", "[components][properties][notes]") {
    ComponentRegistry registry = makeRegistry();
    ComponentInstance andGate = registry.create("gates.and", 0);
    const std::size_t pinsBefore = andGate.pins().size();

    andGate.setProperty("notes", PropertyValue{std::string("nota de prueba\nsegunda linea")});
    CHECK(std::get<std::string>(andGate.property("notes")) == "nota de prueba\nsegunda linea");
    CHECK(andGate.pins().size() == pinsBefore);
}

namespace {
// Crea `count` wiring.input, los ata en orden a `pinNets[0..count-1]` del
// componente ya creado (`target`, con id `nextId`), y devuelve las
// instancias creadas (el llamador debe mantenerlas vivas mientras use el
// Simulator). No liga el resto de los pines de `target` (los de salida) -
// eso lo hace el propio llamador segun lo que necesite observar.
std::vector<ComponentInstance> driveInputs(ComponentRegistry& registry, ComponentInstance& target,
                                            const std::vector<NetId>& pinNets, std::size_t count,
                                            uint32_t nextId) {
    std::vector<ComponentInstance> sources;
    for (std::size_t i = 0; i < count; ++i) {
        sources.push_back(registry.create("wiring.input", nextId + static_cast<uint32_t>(i)));
        sources.back().bindPin(0, pinNets[i]);
        target.bindPin(i, pinNets[i]);
    }
    return sources;
}
} // namespace

TEST_CASE("Decoder activates exactly the output matching the select combination", "[components][plexers][decoder]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance decoder = registry.create("plexers.decoder", 0, {{"selectBits", PropertyValue{uint64_t{2}}}});
    REQUIRE(decoder.pins().size() == 6); // S0,S1 + Y0..Y3

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 6; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, decoder, nets, 2, 1);
    for (std::size_t i = 2; i < 6; ++i) decoder.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    decoder.buildSimulation(circuit);

    Simulator sim(circuit);
    // S0=1 (bit0), S1=0 (bit1) -> combinacion 1 -> Y1 activo, el resto en 0.
    sim.setInput(sources[0].simBinding().gateIndex, LogicValue::One);
    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[2]) == LogicValue::Zero);  // Y0
    CHECK(sim.getNetValue(nets[3]) == LogicValue::One);   // Y1
    CHECK(sim.getNetValue(nets[4]) == LogicValue::Zero);  // Y2
    CHECK(sim.getNetValue(nets[5]) == LogicValue::Zero);  // Y3
}

TEST_CASE("Decoder with selectBits=1 exercises the single-term (Buffer) synthesis path",
          "[components][plexers][decoder]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance decoder = registry.create("plexers.decoder", 0, {{"selectBits", PropertyValue{uint64_t{1}}}});
    REQUIRE(decoder.pins().size() == 3); // S0 + Y0,Y1

    std::vector<NetId> nets{circuit.addNet(), circuit.addNet(), circuit.addNet()};
    ComponentInstance select = registry.create("wiring.input", 1);
    select.bindPin(0, nets[0]);
    decoder.bindPin(0, nets[0]);
    decoder.bindPin(1, nets[1]);
    decoder.bindPin(2, nets[2]);

    select.buildSimulation(circuit);
    decoder.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(select.simBinding().gateIndex, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[1]) == LogicValue::Zero); // Y0
    CHECK(sim.getNetValue(nets[2]) == LogicValue::One);  // Y1
}

TEST_CASE("Multiplexer routes the selected data line to the output", "[components][plexers][multiplexer]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance mux =
        registry.create("plexers.multiplexer", 0, {{"selectBits", PropertyValue{uint64_t{2}}}});
    REQUIRE(mux.pins().size() == 7); // D0..D3, S0, S1, Y

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 7; ++i) nets.push_back(circuit.addNet());
    // D0..D3 en indices 0..3, S0/S1 en 4/5, Y en 6.
    std::vector<ComponentInstance> dataSources = driveInputs(registry, mux, nets, 4, 1);
    ComponentInstance s0 = registry.create("wiring.input", 10);
    ComponentInstance s1 = registry.create("wiring.input", 11);
    s0.bindPin(0, nets[4]);
    mux.bindPin(4, nets[4]);
    s1.bindPin(0, nets[5]);
    mux.bindPin(5, nets[5]);
    mux.bindPin(6, nets[6]);

    for (ComponentInstance& s : dataSources) s.buildSimulation(circuit);
    s0.buildSimulation(circuit);
    s1.buildSimulation(circuit);
    mux.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(dataSources[0].simBinding().gateIndex, LogicValue::Zero); // D0
    sim.setInput(dataSources[1].simBinding().gateIndex, LogicValue::One);  // D1
    sim.setInput(dataSources[2].simBinding().gateIndex, LogicValue::Zero); // D2
    sim.setInput(dataSources[3].simBinding().gateIndex, LogicValue::Zero); // D3
    sim.setInput(s0.simBinding().gateIndex, LogicValue::One);  // selecciona indice 1 (bit0=1,bit1=0)
    sim.setInput(s1.simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[6]) == LogicValue::One); // Y == D1

    sim.setInput(dataSources[1].simBinding().gateIndex, LogicValue::Zero); // D1 -> 0, ninguna otra linea activa
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[6]) == LogicValue::Zero);
}

TEST_CASE("Demultiplexer copies the data input onto only the selected output",
          "[components][plexers][demultiplexer]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance demux =
        registry.create("plexers.demultiplexer", 0, {{"selectBits", PropertyValue{uint64_t{2}}}});
    REQUIRE(demux.pins().size() == 7); // D, S0, S1, Y0..Y3

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 7; ++i) nets.push_back(circuit.addNet());
    ComponentInstance data = registry.create("wiring.input", 1);
    ComponentInstance s0 = registry.create("wiring.input", 2);
    ComponentInstance s1 = registry.create("wiring.input", 3);
    data.bindPin(0, nets[0]);
    demux.bindPin(0, nets[0]);
    s0.bindPin(0, nets[1]);
    demux.bindPin(1, nets[1]);
    s1.bindPin(0, nets[2]);
    demux.bindPin(2, nets[2]);
    for (std::size_t i = 3; i < 7; ++i) demux.bindPin(i, nets[i]);

    data.buildSimulation(circuit);
    s0.buildSimulation(circuit);
    s1.buildSimulation(circuit);
    demux.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(data.simBinding().gateIndex, LogicValue::One);
    sim.setInput(s0.simBinding().gateIndex, LogicValue::One); // selecciona indice 1
    sim.setInput(s1.simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[3]) == LogicValue::Zero); // Y0
    CHECK(sim.getNetValue(nets[4]) == LogicValue::One);  // Y1
    CHECK(sim.getNetValue(nets[5]) == LogicValue::Zero); // Y2
    CHECK(sim.getNetValue(nets[6]) == LogicValue::Zero); // Y3
}

TEST_CASE("Priority encoder picks the highest-index active input and raises valid",
          "[components][plexers][priorityEncoder]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance encoder =
        registry.create("plexers.priorityEncoder", 0, {{"selectBits", PropertyValue{uint64_t{2}}}});
    REQUIRE(encoder.pins().size() == 7); // I0..I3, Y0, Y1, valid

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 7; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, encoder, nets, 4, 1);
    for (std::size_t i = 4; i < 7; ++i) encoder.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    encoder.buildSimulation(circuit);

    Simulator sim(circuit);
    // I1 e I3 activas -> gana la de mayor indice, I3 -> Y=binario(3)=11, valid=1.
    sim.setInput(sources[0].simBinding().gateIndex, LogicValue::Zero);
    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::One);
    sim.setInput(sources[2].simBinding().gateIndex, LogicValue::Zero);
    sim.setInput(sources[3].simBinding().gateIndex, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[4]) == LogicValue::One); // Y0
    CHECK(sim.getNetValue(nets[5]) == LogicValue::One); // Y1
    CHECK(sim.getNetValue(nets[6]) == LogicValue::One); // valid

    // Ninguna entrada activa -> valid en 0.
    for (ComponentInstance& s : sources) {
        sim.setInput(s.simBinding().gateIndex, LogicValue::Zero);
    }
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[6]) == LogicValue::Zero);
}

TEST_CASE("Adder computes Sum/Cout for a basic case and propagates carry across all bits",
          "[components][arithmetic][adder]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance adder = registry.create("arithmetic.adder", 0, {{"bits", PropertyValue{uint64_t{3}}}});
    REQUIRE(adder.pins().size() == 11); // A0..A2, B0..B2, Cin, Sum0..Sum2, Cout

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 11; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, adder, nets, 7, 1);
    for (std::size_t i = 7; i < 11; ++i) adder.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    adder.buildSimulation(circuit);

    Simulator sim(circuit);
    auto setBits = [&](std::size_t start, uint64_t value, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            sim.setInput(sources[start + i].simBinding().gateIndex,
                         ((value >> i) & 1) != 0 ? LogicValue::One : LogicValue::Zero);
        }
    };

    // 3 + 2, sin acarreo de entrada -> Sum=5, Cout=0.
    setBits(0, 3, 3);  // A
    setBits(3, 2, 3);  // B
    sim.setInput(sources[6].simBinding().gateIndex, LogicValue::Zero); // Cin
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[7]) == LogicValue::One);   // Sum0
    CHECK(sim.getNetValue(nets[8]) == LogicValue::Zero);  // Sum1
    CHECK(sim.getNetValue(nets[9]) == LogicValue::One);   // Sum2
    CHECK(sim.getNetValue(nets[10]) == LogicValue::Zero); // Cout

    // 7 + 1 desborda 3 bits -> Sum=0, Cout=1 (el acarreo se propaga por los 3 bits).
    setBits(0, 7, 3);
    setBits(3, 1, 3);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[7]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[8]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[9]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[10]) == LogicValue::One); // Cout
}

TEST_CASE("Subtractor computes Diff/Bout for a basic case and propagates borrow across all bits",
          "[components][arithmetic][subtractor]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance subtractor =
        registry.create("arithmetic.subtractor", 0, {{"bits", PropertyValue{uint64_t{3}}}});
    REQUIRE(subtractor.pins().size() == 11); // A0..A2, B0..B2, Bin, Diff0..Diff2, Bout

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 11; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, subtractor, nets, 7, 1);
    for (std::size_t i = 7; i < 11; ++i) subtractor.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    subtractor.buildSimulation(circuit);

    Simulator sim(circuit);
    auto setBits = [&](std::size_t start, uint64_t value, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            sim.setInput(sources[start + i].simBinding().gateIndex,
                         ((value >> i) & 1) != 0 ? LogicValue::One : LogicValue::Zero);
        }
    };

    // 5 - 2, sin prestamo de entrada -> Diff=3, Bout=0.
    setBits(0, 5, 3); // A
    setBits(3, 2, 3); // B
    sim.setInput(sources[6].simBinding().gateIndex, LogicValue::Zero); // Bin
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[7]) == LogicValue::One);   // Diff0
    CHECK(sim.getNetValue(nets[8]) == LogicValue::One);   // Diff1
    CHECK(sim.getNetValue(nets[9]) == LogicValue::Zero);  // Diff2
    CHECK(sim.getNetValue(nets[10]) == LogicValue::Zero); // Bout

    // 2 - 5 pide prestado -> Diff=5 (complemento a 2 en 3 bits), Bout=1.
    setBits(0, 2, 3);
    setBits(3, 5, 3);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[7]) == LogicValue::One);  // Diff0
    CHECK(sim.getNetValue(nets[8]) == LogicValue::Zero); // Diff1
    CHECK(sim.getNetValue(nets[9]) == LogicValue::One);  // Diff2
    CHECK(sim.getNetValue(nets[10]) == LogicValue::One); // Bout
}

TEST_CASE("Comparator raises exactly one of GT/EQ/LT for A>B, A==B and A<B",
          "[components][arithmetic][comparator]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance comparator =
        registry.create("arithmetic.comparator", 0, {{"bits", PropertyValue{uint64_t{3}}}});
    REQUIRE(comparator.pins().size() == 9); // A0..A2, B0..B2, GT, EQ, LT

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 9; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, comparator, nets, 6, 1);
    for (std::size_t i = 6; i < 9; ++i) comparator.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    comparator.buildSimulation(circuit);

    Simulator sim(circuit);
    auto setBits = [&](std::size_t start, uint64_t value, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            sim.setInput(sources[start + i].simBinding().gateIndex,
                         ((value >> i) & 1) != 0 ? LogicValue::One : LogicValue::Zero);
        }
    };

    // A=5, B=3 -> GT.
    setBits(0, 5, 3);
    setBits(3, 3, 3);
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[6]) == LogicValue::One);  // GT
    CHECK(sim.getNetValue(nets[7]) == LogicValue::Zero); // EQ
    CHECK(sim.getNetValue(nets[8]) == LogicValue::Zero); // LT

    // A=3, B=5 -> LT.
    setBits(0, 3, 3);
    setBits(3, 5, 3);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[6]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[7]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[8]) == LogicValue::One);

    // A=4, B=4 -> EQ.
    setBits(0, 4, 3);
    setBits(3, 4, 3);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[6]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[7]) == LogicValue::One);
    CHECK(sim.getNetValue(nets[8]) == LogicValue::Zero);
}

TEST_CASE("Comparator with bits=1 exercises the single-term (Buffer) synthesis path for GT/EQ/LT",
          "[components][arithmetic][comparator]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance comparator =
        registry.create("arithmetic.comparator", 0, {{"bits", PropertyValue{uint64_t{1}}}});
    REQUIRE(comparator.pins().size() == 5); // A0, B0, GT, EQ, LT

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 5; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, comparator, nets, 2, 1);
    for (std::size_t i = 2; i < 5; ++i) comparator.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    comparator.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(sources[0].simBinding().gateIndex, LogicValue::One);  // A0
    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::Zero); // B0
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[2]) == LogicValue::One);  // GT
    CHECK(sim.getNetValue(nets[3]) == LogicValue::Zero); // EQ
    CHECK(sim.getNetValue(nets[4]) == LogicValue::Zero); // LT

    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::One); // B0 -> A0==B0==1
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[2]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[3]) == LogicValue::One);
    CHECK(sim.getNetValue(nets[4]) == LogicValue::Zero);
}

TEST_CASE("SR latch sets, resets and holds across S/R combinations", "[components][memory][srLatch]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance latch = registry.create("memory.srLatch", 0);
    REQUIRE(latch.pins().size() == 4); // S, R, Q, Qn

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 4; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, latch, nets, 2, 1);
    for (std::size_t i = 2; i < 4; ++i) latch.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    latch.buildSimulation(circuit);

    Simulator sim(circuit);
    // Set: S=1, R=0 -> Q=1, Qn=0.
    sim.setInput(sources[0].simBinding().gateIndex, LogicValue::One);
    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[2]) == LogicValue::One);
    CHECK(sim.getNetValue(nets[3]) == LogicValue::Zero);

    // Reset: S=0, R=1 -> Q=0, Qn=1.
    sim.setInput(sources[0].simBinding().gateIndex, LogicValue::Zero);
    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::One);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[2]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[3]) == LogicValue::One);

    // Hold: S=0, R=0 -> mantiene el ultimo estado (Reset).
    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[2]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[3]) == LogicValue::One);
}

TEST_CASE("D flip-flop captures D only on rising CLK edges", "[components][memory][dFlipFlop]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance dff = registry.create("memory.dFlipFlop", 0);
    REQUIRE(dff.pins().size() == 4); // D, CLK, Q, Qn

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 4; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, dff, nets, 2, 1);
    for (std::size_t i = 2; i < 4; ++i) dff.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    dff.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(sources[0].simBinding().gateIndex, LogicValue::One); // D=1
    REQUIRE(sim.runUntilStable());
    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::Zero); // CLK=0 confirmado
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[2]) == LogicValue::HighImpedance); // sin flanco todavia

    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::One); // flanco ascendente
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[2]) == LogicValue::One);  // Q
    CHECK(sim.getNetValue(nets[3]) == LogicValue::Zero); // Qn

    sim.setInput(sources[0].simBinding().gateIndex, LogicValue::Zero); // D cambia solo, CLK sigue en 1
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[2]) == LogicValue::One); // Q no se mueve

    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::Zero); // flanco descendente
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[2]) == LogicValue::One); // tampoco dispara

    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::One); // segundo flanco ascendente, D=0
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[2]) == LogicValue::Zero);
    CHECK(sim.getNetValue(nets[3]) == LogicValue::One);
}

TEST_CASE("JK flip-flop sets, resets, holds and toggles across successive CLK edges",
          "[components][memory][jkFlipFlop]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance jkff = registry.create("memory.jkFlipFlop", 0);
    REQUIRE(jkff.pins().size() == 5); // J, K, CLK, Q, Qn

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 5; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, jkff, nets, 3, 1);
    for (std::size_t i = 3; i < 5; ++i) jkff.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    jkff.buildSimulation(circuit);

    Simulator sim(circuit);
    auto setJK = [&](LogicValue j, LogicValue k) {
        sim.setInput(sources[0].simBinding().gateIndex, j);
        sim.setInput(sources[1].simBinding().gateIndex, k);
    };
    auto pulseClk = [&]() {
        sim.setInput(sources[2].simBinding().gateIndex, LogicValue::Zero);
        REQUIRE(sim.runUntilStable());
        sim.setInput(sources[2].simBinding().gateIndex, LogicValue::One);
        REQUIRE(sim.runUntilStable());
    };

    // Semilla inicial obligatoria: Q realimenta la conversion JK->D (ver
    // makeJkFlipFlopDefinition), y ese net arranca sin manejar (HighImpedance)
    // hasta el primer flanco. Con J=0,K=1 ambos terminos de la conversion
    // colapsan por la regla del cero dominante de AND sin importar el Q
    // todavia indefinido, asi que Reset es la unica combinacion con
    // resultado garantizado en el arranque - Set/Hold/Toggle si dependen del
    // valor previo de Q y por eso no se prueban hasta tener un Q conocido.
    setJK(LogicValue::Zero, LogicValue::One);
    pulseClk();
    CHECK(sim.getNetValue(nets[3]) == LogicValue::Zero);

    setJK(LogicValue::One, LogicValue::Zero); // Set
    pulseClk();
    CHECK(sim.getNetValue(nets[3]) == LogicValue::One);
    CHECK(sim.getNetValue(nets[4]) == LogicValue::Zero);

    setJK(LogicValue::Zero, LogicValue::One); // Reset
    pulseClk();
    CHECK(sim.getNetValue(nets[3]) == LogicValue::Zero);

    setJK(LogicValue::Zero, LogicValue::Zero); // Hold
    pulseClk();
    CHECK(sim.getNetValue(nets[3]) == LogicValue::Zero);

    setJK(LogicValue::One, LogicValue::One); // Toggle: 0 -> 1
    pulseClk();
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[3]) == LogicValue::One);

    pulseClk(); // Toggle otra vez (J=K=1 sigue activo): 1 -> 0
    CHECK(sim.getNetValue(nets[3]) == LogicValue::Zero);
}

TEST_CASE("Register loads all bits simultaneously on a single CLK edge", "[components][memory][register]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance reg = registry.create("memory.register", 0, {{"bits", PropertyValue{uint64_t{3}}}});
    REQUIRE(reg.pins().size() == 7); // D0..D2, CLK, Q0..Q2

    std::vector<NetId> nets;
    for (std::size_t i = 0; i < 7; ++i) nets.push_back(circuit.addNet());
    std::vector<ComponentInstance> sources = driveInputs(registry, reg, nets, 4, 1);
    for (std::size_t i = 4; i < 7; ++i) reg.bindPin(i, nets[i]);

    for (ComponentInstance& s : sources) s.buildSimulation(circuit);
    reg.buildSimulation(circuit);

    Simulator sim(circuit);
    // D = 5 (101): D0=1, D1=0, D2=1.
    sim.setInput(sources[0].simBinding().gateIndex, LogicValue::One);
    sim.setInput(sources[1].simBinding().gateIndex, LogicValue::Zero);
    sim.setInput(sources[2].simBinding().gateIndex, LogicValue::One);
    sim.setInput(sources[3].simBinding().gateIndex, LogicValue::Zero); // CLK=0 confirmado
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[4]) == LogicValue::HighImpedance); // sin flanco todavia

    sim.setInput(sources[3].simBinding().gateIndex, LogicValue::One); // flanco ascendente
    REQUIRE(sim.runUntilStable());
    CHECK_FALSE(sim.stats().oscillationDetected);
    CHECK(sim.getNetValue(nets[4]) == LogicValue::One);  // Q0
    CHECK(sim.getNetValue(nets[5]) == LogicValue::Zero); // Q1
    CHECK(sim.getNetValue(nets[6]) == LogicValue::One);  // Q2

    // D cambia sin nuevo flanco de CLK: Q no se mueve.
    sim.setInput(sources[0].simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(sim.getNetValue(nets[4]) == LogicValue::One);
}

TEST_CASE("Subcircuit has no pins and builds a no-op simulation until its target document resolves",
          "[components][structural][subcircuit]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    // Sin ExternalContext (el default de ComponentInstance): targetPath
    // vacio de entrada, y aunque no lo estuviera, no hay resolver.
    ComponentInstance subcircuit = registry.create("structural.subcircuit", 0);
    CHECK(subcircuit.pins().empty());
    REQUIRE(subcircuit.allPinsBound()); // vacuamente cierto con 0 pines
    subcircuit.buildSimulation(circuit); // no debe tirar aunque no haya documento resuelto
    CHECK(subcircuit.simBinding().kind == ComponentSimBinding::Kind::Driver);
}

TEST_CASE("propagationDelay defaults to 1 and enforces its minimum", "[components][properties][delay]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance andGate = registry.create("gates.and", 0);
    CHECK(std::get<uint64_t>(andGate.property("propagationDelay")) == 1);

    CHECK_THROWS_AS(registry.create("gates.and", 1, {{"propagationDelay", PropertyValue{uint64_t{0}}}}),
                    std::invalid_argument);

    // Los sinks/wiring no tienen esta propiedad (no hay logica combinacional que retardar).
    CHECK_THROWS_AS(
        registry.create("wiring.output", 2, {{"propagationDelay", PropertyValue{uint64_t{2}}}}),
        std::invalid_argument);
}

namespace {
// Cablea un ic74ls.bcdDriver de punta a punta (4 entradas bit0..bit3, 7
// salidas a..g) y devuelve los 7 valores de salida observados tras fijar
// `bits` (bit0..bit3, en ese orden) y estabilizar. `variant` por defecto
// "7448" (activo en alto, One = encendido) para que los casos existentes
// sigan verificando el patron de segmentos sin invertir - "7447" (activo en
// bajo, el default real de la definicion) tiene su propio test dedicado
// mas abajo.
std::array<LogicValue, 7> runBcdDriver(ComponentRegistry& registry, std::array<LogicValue, 4> bits,
                                        const std::string& variant = "7448") {
    Circuit circuit;
    ComponentInstance driver =
        registry.create("ic74ls.bcdDriver", 0, {{"variant", PropertyValue{variant}}});
    std::vector<ComponentInstance> inputs;
    std::vector<ComponentInstance> outputs;
    std::vector<NetId> inputNets;
    std::vector<NetId> outputNets;

    for (int i = 0; i < 4; ++i) {
        inputs.push_back(registry.create("wiring.input", static_cast<uint32_t>(i + 1)));
        const NetId net = circuit.addNet();
        driver.bindPin(static_cast<std::size_t>(i), net);
        inputs.back().bindPin(0, net);
        inputNets.push_back(net);
    }
    for (int i = 0; i < 7; ++i) {
        outputs.push_back(registry.create("wiring.output", static_cast<uint32_t>(i + 10)));
        const NetId net = circuit.addNet();
        driver.bindPin(static_cast<std::size_t>(4 + i), net);
        outputs.back().bindPin(0, net);
        outputNets.push_back(net);
    }

    driver.buildSimulation(circuit);
    for (ComponentInstance& input : inputs) {
        input.buildSimulation(circuit);
    }
    for (ComponentInstance& output : outputs) {
        output.buildSimulation(circuit);
    }

    Simulator sim(circuit);
    for (int i = 0; i < 4; ++i) {
        sim.setInput(inputs[static_cast<std::size_t>(i)].simBinding().gateIndex, bits[static_cast<std::size_t>(i)]);
    }
    REQUIRE(sim.runUntilStable());

    std::array<LogicValue, 7> result{};
    for (int i = 0; i < 7; ++i) {
        result[static_cast<std::size_t>(i)] =
            sim.getNetValue(outputs[static_cast<std::size_t>(i)].simBinding().observedNets[0]);
    }
    return result;
}
} // namespace

TEST_CASE("ic74ls.bcdDriver decodes BCD digits 0/1/9 to the standard 7-segment pattern",
          "[components][ic74ls][bcdDriver]") {
    ComponentRegistry registry = makeRegistry();

    // Digito 0 (0000): a..f encendidos, g apagado.
    const auto zero = runBcdDriver(registry, {LogicValue::Zero, LogicValue::Zero, LogicValue::Zero, LogicValue::Zero});
    CHECK(zero == std::array<LogicValue, 7>{LogicValue::One, LogicValue::One, LogicValue::One, LogicValue::One,
                                             LogicValue::One, LogicValue::One, LogicValue::Zero});

    // Digito 1 (0001, bit0=1): solo b y c encendidos.
    const auto one = runBcdDriver(registry, {LogicValue::One, LogicValue::Zero, LogicValue::Zero, LogicValue::Zero});
    CHECK(one == std::array<LogicValue, 7>{LogicValue::Zero, LogicValue::One, LogicValue::One, LogicValue::Zero,
                                            LogicValue::Zero, LogicValue::Zero, LogicValue::Zero});

    // Digito 9 (1001, bit0=1 y bit3=1): a,b,c,d,f,g encendidos, e apagado.
    const auto nine = runBcdDriver(registry, {LogicValue::One, LogicValue::Zero, LogicValue::Zero, LogicValue::One});
    CHECK(nine == std::array<LogicValue, 7>{LogicValue::One, LogicValue::One, LogicValue::One, LogicValue::One,
                                             LogicValue::Zero, LogicValue::One, LogicValue::One});
}

TEST_CASE("ic74ls.bcdDriver defaults to the 7447 variant (active-low outputs)",
          "[components][ic74ls][bcdDriver]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance driver = registry.create("ic74ls.bcdDriver", 0);
    CHECK(std::get<std::string>(driver.property("variant")) == "7447");

    // Digito 0 (0000) en 7447: mismo patron que 7448 pero invertido -
    // a..f en Zero (encendido, activo en bajo), g en One (apagado).
    const auto zero =
        runBcdDriver(registry, {LogicValue::Zero, LogicValue::Zero, LogicValue::Zero, LogicValue::Zero}, "7447");
    CHECK(zero == std::array<LogicValue, 7>{LogicValue::Zero, LogicValue::Zero, LogicValue::Zero, LogicValue::Zero,
                                             LogicValue::Zero, LogicValue::Zero, LogicValue::One});
}

TEST_CASE("ic74ls.bcdDriver leaves every segment off for an invalid BCD combination (10-15)",
          "[components][ic74ls][bcdDriver]") {
    ComponentRegistry registry = makeRegistry();

    // 10 = 1010 (bit1=1, bit3=1): ningun digito 0-9 lo cubre.
    const auto ten = runBcdDriver(registry, {LogicValue::Zero, LogicValue::One, LogicValue::Zero, LogicValue::One});
    CHECK(ten == std::array<LogicValue, 7>{LogicValue::Zero, LogicValue::Zero, LogicValue::Zero, LogicValue::Zero,
                                            LogicValue::Zero, LogicValue::Zero, LogicValue::Zero});
}

TEST_CASE("hexDigitSegments matches the standard 7-segment encoding for representative digits",
          "[components][hexDisplay]") {
    // Orden a,b,c,d,e,f,g.
    CHECK(hexDigitSegments(0) == std::array<bool, 7>{true, true, true, true, true, true, false});
    CHECK(hexDigitSegments(1) == std::array<bool, 7>{false, true, true, false, false, false, false});
    CHECK(hexDigitSegments(8) == std::array<bool, 7>{true, true, true, true, true, true, true});
    CHECK(hexDigitSegments(15) == std::array<bool, 7>{true, false, false, false, true, true, true}); // F
    CHECK_THROWS_AS(hexDigitSegments(16), std::invalid_argument);
}

TEST_CASE("Hex display exposes 4 or 5 pins depending on hasDecimalPoint and decodes a clean nibble",
          "[components][hexDisplay]") {
    ComponentRegistry registry = makeRegistry();

    const ComponentInstance withDot = registry.create("io.hexDisplay", 0);
    CHECK(withDot.pins().size() == 5);
    CHECK(withDot.pins().back().name == "dot");

    const ComponentInstance withoutDot =
        registry.create("io.hexDisplay", 1, {{"hasDecimalPoint", PropertyValue{false}}});
    CHECK(withoutDot.pins().size() == 4);

    // 1010 (bit0=0,bit1=1,bit2=0,bit3=1) = 0xA
    const auto state = hexDisplayState(withoutDot, LogicValue::Zero, LogicValue::One, LogicValue::Zero, LogicValue::One);
    REQUIRE(state.valid);
    CHECK(state.segments == hexDigitSegments(0xA));
}

TEST_CASE("Hex display refuses to decode an ambiguous bit", "[components][hexDisplay][unknown][highz]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance display = registry.create("io.hexDisplay", 0);

    const auto stateX = hexDisplayState(display, LogicValue::Unknown, LogicValue::Zero, LogicValue::Zero, LogicValue::Zero);
    CHECK_FALSE(stateX.valid);

    const auto stateZ = hexDisplayState(display, LogicValue::Zero, LogicValue::HighImpedance, LogicValue::Zero,
                                         LogicValue::Zero);
    CHECK_FALSE(stateZ.valid);
}

TEST_CASE("LED matrix exposes rows*cols pins named R{r}C{c} and honors activeHigh", "[components][ledMatrix]") {
    ComponentRegistry registry = makeRegistry();

    const ComponentInstance matrix = registry.create(
        "io.ledMatrix", 0, {{"rows", PropertyValue{uint64_t{2}}}, {"cols", PropertyValue{uint64_t{3}}}});
    REQUIRE(matrix.pins().size() == 6);
    CHECK(matrix.pins()[0].name == "R0C0");
    CHECK(matrix.pins()[1].name == "R0C1");
    CHECK(matrix.pins()[3].name == "R1C0");
    CHECK(matrix.pins()[5].name == "R1C2");

    CHECK(ledMatrixCellIsLit(matrix, LogicValue::One));
    CHECK_FALSE(ledMatrixCellIsLit(matrix, LogicValue::Zero));

    const ComponentInstance activeLow =
        registry.create("io.ledMatrix", 1, {{"activeHigh", PropertyValue{false}}});
    CHECK(ledMatrixCellIsLit(activeLow, LogicValue::Zero));
    CHECK_FALSE(ledMatrixCellIsLit(activeLow, LogicValue::One));
}

TEST_CASE("Multiplexed LED matrix exposes rows+cols pins instead of one per cell",
          "[components][ledMatrix]") {
    ComponentRegistry registry = makeRegistry();

    const ComponentInstance matrix =
        registry.create("io.ledMatrix", 0,
                        {{"rows", PropertyValue{uint64_t{2}}},
                          {"cols", PropertyValue{uint64_t{3}}},
                          {"wiring", PropertyValue{std::string("multiplexed")}}});
    // 2x3 celdas con solo 2+3 pines, contra los 6 de la conexion directa.
    REQUIRE(matrix.pins().size() == 5);
    CHECK(matrix.pins()[0].name == "F0");
    CHECK(matrix.pins()[1].name == "F1");
    CHECK(matrix.pins()[2].name == "C0");
    CHECK(matrix.pins()[4].name == "C2");

    // Un panel de 16x16 es viable multiplexado (32 pines) y se recorta a 8x8
    // en conexion directa, donde serian 256.
    const ComponentInstance big =
        registry.create("io.ledMatrix", 1,
                        {{"rows", PropertyValue{uint64_t{16}}},
                          {"cols", PropertyValue{uint64_t{16}}},
                          {"wiring", PropertyValue{std::string("multiplexed")}}});
    CHECK(big.pins().size() == 32);
    const ComponentInstance bigDirect = registry.create(
        "io.ledMatrix", 2, {{"rows", PropertyValue{uint64_t{16}}}, {"cols", PropertyValue{uint64_t{16}}}});
    CHECK(bigDirect.pins().size() == 64);
}

TEST_CASE("Multiplexed LED matrix cell lights only when its row drives and its column sinks",
          "[components][ledMatrix]") {
    ComponentRegistry registry = makeRegistry();

    const ComponentInstance matrix =
        registry.create("io.ledMatrix", 0, {{"wiring", PropertyValue{std::string("multiplexed")}}});
    // Activo en alto por defecto: fila en One, columna en Zero.
    CHECK(ledMatrixMultiplexedCellIsLit(matrix, LogicValue::One, LogicValue::Zero));
    CHECK_FALSE(ledMatrixMultiplexedCellIsLit(matrix, LogicValue::One, LogicValue::One));
    CHECK_FALSE(ledMatrixMultiplexedCellIsLit(matrix, LogicValue::Zero, LogicValue::Zero));
    // Una fila sin atacar (flotante) no enciende nada, aunque la columna drene.
    CHECK_FALSE(ledMatrixMultiplexedCellIsLit(matrix, LogicValue::HighImpedance, LogicValue::Zero));
    CHECK_FALSE(ledMatrixMultiplexedCellIsLit(matrix, LogicValue::One, LogicValue::Error));

    // Polaridad invertida: la fila alimenta en bajo y la columna drena en alto.
    const ComponentInstance activeLow =
        registry.create("io.ledMatrix", 1,
                        {{"wiring", PropertyValue{std::string("multiplexed")}},
                          {"activeHigh", PropertyValue{false}}});
    CHECK(ledMatrixMultiplexedCellIsLit(activeLow, LogicValue::Zero, LogicValue::One));
    CHECK_FALSE(ledMatrixMultiplexedCellIsLit(activeLow, LogicValue::One, LogicValue::Zero));
}

TEST_CASE("LED matrix cells read back through the simulator like independent LEDs", "[components][ledMatrix]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance matrix = registry.create(
        "io.ledMatrix", 0, {{"rows", PropertyValue{uint64_t{1}}}, {"cols", PropertyValue{uint64_t{2}}}});
    ComponentInstance in0 = registry.create("wiring.input", 1);
    ComponentInstance in1 = registry.create("wiring.input", 2);

    const NetId net0 = circuit.addNet();
    const NetId net1 = circuit.addNet();
    matrix.bindPin(0, net0);
    matrix.bindPin(1, net1);
    in0.bindPin(0, net0);
    in1.bindPin(0, net1);

    matrix.buildSimulation(circuit);
    in0.buildSimulation(circuit);
    in1.buildSimulation(circuit);

    Simulator sim(circuit);
    sim.setInput(in0.simBinding().gateIndex, LogicValue::One);
    sim.setInput(in1.simBinding().gateIndex, LogicValue::Zero);
    REQUIRE(sim.runUntilStable());
    CHECK(ledMatrixCellIsLit(matrix, sim.getNetValue(matrix.simBinding().observedNets[0])));
    CHECK_FALSE(ledMatrixCellIsLit(matrix, sim.getNetValue(matrix.simBinding().observedNets[1])));
}

TEST_CASE("Terminal decodes its 8-bit bus (bit0 = LSB) as an ASCII character", "[components][terminal]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance terminal = registry.create("io.terminal", 0);
    REQUIRE(terminal.pins().size() == 8);

    // 'A' = 0x41 = 0100 0001 -> bit0=1, bit6=1, resto 0.
    std::vector<LogicValue> bitsA(8, LogicValue::Zero);
    bitsA[0] = LogicValue::One;
    bitsA[6] = LogicValue::One;
    const std::optional<char> decoded = terminalCharacter(terminal, bitsA);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == 'A');
}

TEST_CASE("Terminal refuses to decode a byte with an ambiguous bit", "[components][terminal][unknown][highz]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentInstance terminal = registry.create("io.terminal", 0);

    std::vector<LogicValue> bits(8, LogicValue::Zero);
    bits[3] = LogicValue::Unknown;
    CHECK_FALSE(terminalCharacter(terminal, bits).has_value());
}

TEST_CASE("Terminal bits read back through the simulator like an independent 8-bit bus", "[components][terminal]") {
    ComponentRegistry registry = makeRegistry();
    Circuit circuit;

    ComponentInstance terminal = registry.create("io.terminal", 0);
    std::vector<ComponentInstance> inputs;
    std::vector<NetId> nets;
    for (int i = 0; i < 8; ++i) {
        inputs.push_back(registry.create("wiring.input", static_cast<uint32_t>(i + 1)));
        nets.push_back(circuit.addNet());
        terminal.bindPin(static_cast<std::size_t>(i), nets.back());
        inputs.back().bindPin(0, nets.back());
    }
    terminal.buildSimulation(circuit);
    for (ComponentInstance& input : inputs) {
        input.buildSimulation(circuit);
    }

    Simulator sim(circuit);
    for (ComponentInstance& input : inputs) {
        sim.setInput(input.simBinding().gateIndex, LogicValue::Zero); // linea de base limpia para los 8 bits
    }
    sim.setInput(inputs[0].simBinding().gateIndex, LogicValue::One); // bit0 = 1 -> 'A' = 0x41
    sim.setInput(inputs[6].simBinding().gateIndex, LogicValue::One); // bit6 = 1
    REQUIRE(sim.runUntilStable());

    std::vector<LogicValue> observed;
    for (const NetId net : terminal.simBinding().observedNets) {
        observed.push_back(sim.getNetValue(net));
    }
    const std::optional<char> decoded = terminalCharacter(terminal, observed);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == 'A');
}
