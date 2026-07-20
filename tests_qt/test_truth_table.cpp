// Ejercita editor::computeTruthTable() (Fase C) directamente sobre
// CircuitDocument, sin pasar por ui::TruthTablePanel (un QWidget - la suite
// corre sobre QCoreApplication, no QApplication, ver test_project_serializer.cpp).
// No necesita su propio main(): comparte el CATCH_CONFIG_RUNNER definido ahi.

#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "editor/CircuitDocument.hpp"
#include "editor/TruthTable.hpp"

using digitalforge::components::PropertyValue;
using digitalforge::core::LogicValue;
using digitalforge::editor::CircuitDocument;
using digitalforge::editor::computeTruthTable;
using digitalforge::editor::PinRef;
using digitalforge::editor::TruthTable;
using digitalforge::editor::TruthTableFormula;

TEST_CASE("computeTruthTable produces the full truth table of a 2-input AND circuit", "[truthtable]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input");
    const uint32_t in1 = doc.addComponent("wiring.input");
    const uint32_t andGate = doc.addComponent("gates.and");
    const uint32_t out = doc.addComponent("wiring.output");

    doc.addWire(PinRef{in0, 0}, PinRef{andGate, 0});
    doc.addWire(PinRef{in1, 0}, PinRef{andGate, 1});
    doc.addWire(PinRef{andGate, 2}, PinRef{out, 0});

    const TruthTable table = computeTruthTable(doc);
    REQUIRE(table.inputHeaders.size() == 2);
    REQUIRE(table.outputHeaders.size() == 1);
    REQUIRE(table.rows.size() == 4);

    // Combinacion = bit0 (in0) | bit1 (in1) << 1, en ese orden de columnas.
    for (const auto& row : table.rows) {
        REQUIRE(row.values.size() == 3);
        const bool a = row.values[0] == '1';
        const bool b = row.values[1] == '1';
        const char y = row.values[2];
        CHECK(y == ((a && b) ? '1' : '0'));
    }
}

TEST_CASE("computeTruthTable's outputFormula is the sum of products of the minterms where the output is 1",
          "[truthtable][formula]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input");
    const uint32_t in1 = doc.addComponent("wiring.input");
    const uint32_t andGate = doc.addComponent("gates.and");
    const uint32_t out = doc.addComponent("wiring.output");

    doc.addWire(PinRef{in0, 0}, PinRef{andGate, 0});
    doc.addWire(PinRef{in1, 0}, PinRef{andGate, 1});
    doc.addWire(PinRef{andGate, 2}, PinRef{out, 0});

    const TruthTable table = computeTruthTable(doc);
    REQUIRE(table.outputFormulas.size() == 1);
    const TruthTableFormula& formula = table.outputFormulas.front();
    // AND: unico minterm donde ambas entradas valen 1 (ninguna negada).
    CHECK(formula.expression == table.inputHeaders[0] + QStringLiteral("·") + table.inputHeaders[1]);
    CHECK(formula.complete);
}

TEST_CASE("computeTruthTable's outputFormula sums every minterm where a multi-row output is 1",
          "[truthtable][formula]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input");
    const uint32_t in1 = doc.addComponent("wiring.input");
    const uint32_t xorGate = doc.addComponent("gates.xor");
    const uint32_t out = doc.addComponent("wiring.output");

    doc.addWire(PinRef{in0, 0}, PinRef{xorGate, 0});
    doc.addWire(PinRef{in1, 0}, PinRef{xorGate, 1});
    doc.addWire(PinRef{xorGate, 2}, PinRef{out, 0});

    const TruthTable table = computeTruthTable(doc);
    REQUIRE(table.outputFormulas.size() == 1);
    const TruthTableFormula& formula = table.outputFormulas.front();
    const QString& a = table.inputHeaders[0];
    const QString& b = table.inputHeaders[1];
    // XOR: dos minterms, cada uno con exactamente una entrada negada. Orden
    // de filas = combinacion binaria creciente (bit0=in0, bit1=in1), asi que
    // a·b' (combinacion 1: a=1,b=0) sale antes que a'·b (combinacion 2:
    // a=0,b=1).
    CHECK(formula.expression ==
          a + QStringLiteral("·") + b + "'" + QStringLiteral(" + ") + a + "'" + QStringLiteral("·") + b);
    CHECK(formula.complete);
}

TEST_CASE("computeTruthTable restores each input's original value afterward", "[truthtable]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t out = doc.addComponent("wiring.output");
    doc.addWire(PinRef{in0, 0}, PinRef{out, 0});

    REQUIRE(doc.pinValue(in0, 0) == LogicValue::One);
    (void)computeTruthTable(doc);
    CHECK(doc.pinValue(in0, 0) == LogicValue::One); // el barrido no debe dejarlo en otro valor
}

TEST_CASE("computeTruthTable uses each input/output's label when set, falling back to IN{id}/OUT{id}",
          "[truthtable]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"label", PropertyValue{std::string("Clk")}}});
    const uint32_t out = doc.addComponent("wiring.output");
    doc.addWire(PinRef{in0, 0}, PinRef{out, 0});

    const TruthTable table = computeTruthTable(doc);
    REQUIRE(table.inputHeaders.size() == 1);
    CHECK(table.inputHeaders[0] == "Clk");
    REQUIRE(table.outputHeaders.size() == 1);
    CHECK(table.outputHeaders[0] == QString("OUT%1").arg(out));
}

TEST_CASE("computeTruthTable rejects a circuit missing inputs or outputs", "[truthtable][error]") {
    CircuitDocument onlyInput;
    (void)onlyInput.addComponent("wiring.input");
    CHECK_THROWS_AS(computeTruthTable(onlyInput), std::invalid_argument);

    CircuitDocument onlyOutput;
    (void)onlyOutput.addComponent("wiring.output");
    CHECK_THROWS_AS(computeTruthTable(onlyOutput), std::invalid_argument);
}

TEST_CASE("computeTruthTable rejects more inputs than the configured cap", "[truthtable][error]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input");
    const uint32_t in1 = doc.addComponent("wiring.input");
    const uint32_t in2 = doc.addComponent("wiring.input");
    const uint32_t out = doc.addComponent("wiring.output");
    doc.addWire(PinRef{in0, 0}, PinRef{out, 0});
    (void)in1;
    (void)in2;

    CHECK_THROWS_AS(computeTruthTable(doc, /*maxInputs=*/2), std::invalid_argument);
    CHECK_NOTHROW(computeTruthTable(doc, /*maxInputs=*/3));
}
