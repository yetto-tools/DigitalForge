// Ejercita editor::KarnaughDocument: reset()/setVariableCount() con varias
// funciones, alta/baja/renombre de funciones, y los limites de validacion
// de indice/valor. No necesita su propio main(): comparte el
// CATCH_CONFIG_RUNNER definido en test_project_serializer.cpp (ver
// tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <utility>
#include <vector>

#include "editor/KarnaughDocument.hpp"

using digitalforge::editor::KarnaughCellValue;
using digitalforge::editor::KarnaughDocument;

TEST_CASE("A fresh KarnaughDocument starts with 4 variables and a single function F1, all cells Zero",
          "[karnaugh][document]") {
    KarnaughDocument doc;
    CHECK(doc.variableCount() == 4);
    CHECK(doc.variableName(0) == "A");
    CHECK(doc.variableName(3) == "D");
    CHECK(doc.outputCount() == 1);
    CHECK(doc.outputName(0) == "F1");
    CHECK(doc.outputCells(0).size() == 16);
    for (KarnaughCellValue value : doc.outputCells(0)) {
        CHECK(value == KarnaughCellValue::Zero);
    }
    CHECK_FALSE(doc.dirty()); // el estado inicial no cuenta como "sin guardar"
}

TEST_CASE("reset() validates variableCount and resets to a single function", "[karnaugh][document]") {
    KarnaughDocument doc;
    doc.addOutput("G");
    doc.setCellValue(0, 0, KarnaughCellValue::One);
    doc.reset(2);
    CHECK(doc.variableCount() == 2);
    CHECK(doc.outputCount() == 1);
    CHECK(doc.outputCells(0).size() == 4);
    CHECK(doc.cellValue(0, 0) == KarnaughCellValue::Zero);
    CHECK_THROWS_AS(doc.reset(1), std::invalid_argument);
    CHECK_THROWS_AS(doc.reset(5), std::invalid_argument);
}

TEST_CASE("addOutput appends a Zero-filled function and suggests F2, F3...", "[karnaugh][document]") {
    KarnaughDocument doc; // 4 vars, F1
    int changedCount = 0;
    QObject::connect(&doc, &KarnaughDocument::outputsChanged, [&] { ++changedCount; });

    const int index = doc.addOutput();
    CHECK(index == 1);
    CHECK(doc.outputCount() == 2);
    CHECK(doc.outputName(1) == "F2");
    CHECK(doc.outputCells(1).size() == 16);
    CHECK(changedCount == 1);

    doc.addOutput("Suma");
    CHECK(doc.outputName(2) == "Suma");
}

TEST_CASE("removeOutput refuses to drop the last remaining function", "[karnaugh][document]") {
    KarnaughDocument doc; // solo F1
    CHECK_THROWS_AS(doc.removeOutput(0), std::invalid_argument);

    doc.addOutput();
    doc.removeOutput(0);
    CHECK(doc.outputCount() == 1);
    CHECK(doc.outputName(0) == "F2"); // la que sobrevive es la segunda agregada

    CHECK_THROWS_AS(doc.removeOutput(5), std::invalid_argument);
    CHECK_THROWS_AS(doc.removeOutput(-1), std::invalid_argument);
}

TEST_CASE("setCellValue is independent per function and emits cellChanged with the right output index",
          "[karnaugh][document]") {
    KarnaughDocument doc;
    doc.addOutput(); // F1, F2
    std::vector<std::pair<int, int>> changed;
    QObject::connect(&doc, &KarnaughDocument::cellChanged,
                      [&](int outputIndex, int minterm) { changed.emplace_back(outputIndex, minterm); });

    doc.setCellValue(1, 5, KarnaughCellValue::DontCare);
    CHECK(doc.cellValue(1, 5) == KarnaughCellValue::DontCare);
    CHECK(doc.cellValue(0, 5) == KarnaughCellValue::Zero); // F1 no se toco
    CHECK(doc.dirty());
    REQUIRE(changed.size() == 1);
    CHECK(changed.front() == std::make_pair(1, 5));
}

TEST_CASE("setCellValue/cellValue validate output and minterm ranges", "[karnaugh][document]") {
    KarnaughDocument doc; // 4 variables, 16 celdas, 1 funcion
    CHECK_THROWS_AS(doc.cellValue(0, 16), std::invalid_argument);
    CHECK_THROWS_AS(doc.cellValue(0, -1), std::invalid_argument);
    CHECK_THROWS_AS(doc.cellValue(1, 0), std::invalid_argument);
    CHECK_THROWS_AS(doc.setCellValue(0, 16, KarnaughCellValue::One), std::invalid_argument);
    CHECK_THROWS_AS(doc.setCellValue(-1, 0, KarnaughCellValue::One), std::invalid_argument);
}

TEST_CASE("setVariableName validates the index and emits variablesChanged", "[karnaugh][document]") {
    KarnaughDocument doc;
    int changedCount = 0;
    QObject::connect(&doc, &KarnaughDocument::variablesChanged, [&] { ++changedCount; });
    doc.setVariableName(1, "SEL");
    CHECK(doc.variableName(1) == "SEL");
    CHECK(changedCount == 1);
    CHECK_THROWS_AS(doc.setVariableName(4, "X"), std::invalid_argument);
    CHECK_THROWS_AS(doc.variableName(-1), std::invalid_argument);
}

TEST_CASE("setVariableCount resizes every function, preserving surviving cells", "[karnaugh][document]") {
    KarnaughDocument doc; // 4 vars
    doc.addOutput();      // F1, F2
    doc.setCellValue(0, 3, KarnaughCellValue::One);
    doc.setCellValue(1, 3, KarnaughCellValue::DontCare);
    doc.setVariableName(0, "X0");

    doc.setVariableCount(2); // shrink: solo sobreviven los minterms 0..3
    CHECK(doc.variableCount() == 2);
    CHECK(doc.outputCells(0).size() == 4);
    CHECK(doc.outputCells(1).size() == 4);
    CHECK(doc.cellValue(0, 3) == KarnaughCellValue::One);
    CHECK(doc.cellValue(1, 3) == KarnaughCellValue::DontCare);
    CHECK(doc.variableName(0) == "X0"); // el nombre sobreviviente no se pierde

    doc.setVariableCount(4); // grow: celdas nuevas arrancan en Zero para TODAS las funciones
    CHECK(doc.outputCells(0).size() == 16);
    CHECK(doc.outputCells(1).size() == 16);
    CHECK(doc.cellValue(0, 3) == KarnaughCellValue::One);
    CHECK(doc.cellValue(1, 3) == KarnaughCellValue::DontCare);
    CHECK(doc.cellValue(0, 8) == KarnaughCellValue::Zero);
    CHECK(doc.cellValue(1, 8) == KarnaughCellValue::Zero);
    CHECK(doc.variableName(0) == "X0");
    CHECK(doc.variableName(2) == "C");

    CHECK_THROWS_AS(doc.setVariableCount(1), std::invalid_argument);
    CHECK_THROWS_AS(doc.setVariableCount(5), std::invalid_argument);
}

TEST_CASE("setOutputName validates the index and emits outputsChanged", "[karnaugh][document]") {
    KarnaughDocument doc;
    int changedCount = 0;
    QObject::connect(&doc, &KarnaughDocument::outputsChanged, [&] { ++changedCount; });
    doc.setOutputName(0, "Acarreo");
    CHECK(doc.outputName(0) == "Acarreo");
    CHECK(changedCount == 1);
    CHECK_THROWS_AS(doc.setOutputName(1, "X"), std::invalid_argument);
    CHECK_THROWS_AS(doc.outputName(-1), std::invalid_argument);
}
