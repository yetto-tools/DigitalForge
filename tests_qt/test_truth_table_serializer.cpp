// Ejercita formats::serializeTruthTableDocument()/loadTruthTableDocument()
// (ida y vuelta JSON, con varias columnas de salida) y
// saveTruthTableDocumentToFile()/loadTruthTableDocumentFromFile() (ida y
// vuelta a disco), incluidos los casos de entrada malformada. No necesita su
// propio main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "editor/TruthTableDocument.hpp"
#include "formats/TruthTableSerializer.hpp"

using digitalforge::editor::KarnaughCellValue;
using digitalforge::editor::TruthTableDocument;
using digitalforge::formats::loadTruthTableDocument;
using digitalforge::formats::loadTruthTableDocumentFromFile;
using digitalforge::formats::saveTruthTableDocumentToFile;
using digitalforge::formats::serializeTruthTableDocument;

TEST_CASE("serializeTruthTableDocument round-trips variable names and every output column",
          "[truthTable][serializer]") {
    TruthTableDocument doc;
    doc.reset(3);
    doc.setVariableName(0, "X");
    doc.setVariableName(1, "Y");
    doc.setVariableName(2, "Z");
    doc.setOutputName(0, "F1");
    doc.setCellValue(0, 0, KarnaughCellValue::One);
    doc.setCellValue(0, 1, KarnaughCellValue::DontCare);
    doc.addOutput("F2");
    doc.setCellValue(1, 7, KarnaughCellValue::One);

    const nlohmann::json json = serializeTruthTableDocument(doc);
    CHECK(json.at("schemaVersion").get<int>() == 1);
    CHECK(json.at("kind").get<std::string>() == "truthtable");
    CHECK(json.at("variableCount").get<int>() == 3);
    REQUIRE(json.at("outputs").size() == 2);
    CHECK(json.at("outputs").at(0).at("name").get<std::string>() == "F1");
    CHECK(json.at("outputs").at(0).at("cells").size() == 8);
    CHECK(json.at("outputs").at(1).at("name").get<std::string>() == "F2");

    TruthTableDocument reloaded;
    loadTruthTableDocument(reloaded, json);
    CHECK(reloaded.variableCount() == 3);
    CHECK(reloaded.variableName(0) == "X");
    CHECK(reloaded.variableName(1) == "Y");
    CHECK(reloaded.variableName(2) == "Z");
    REQUIRE(reloaded.outputCount() == 2);
    CHECK(reloaded.outputName(0) == "F1");
    CHECK(reloaded.outputName(1) == "F2");
    CHECK(reloaded.cellValue(0, 0) == KarnaughCellValue::One);
    CHECK(reloaded.cellValue(0, 1) == KarnaughCellValue::DontCare);
    CHECK(reloaded.cellValue(1, 7) == KarnaughCellValue::One);
    CHECK(reloaded.cellValue(1, 0) == KarnaughCellValue::Zero);
    CHECK_FALSE(reloaded.dirty()); // loadTruthTableDocument() lo deja "limpio"
}

TEST_CASE("loadTruthTableDocument rejects an unsupported schemaVersion", "[truthTable][serializer]") {
    TruthTableDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 2;
    json["variableCount"] = 2;
    json["outputs"] = nlohmann::json::array({{{"name", "F1"}, {"cells", {"0", "0", "0", "0"}}}});
    CHECK_THROWS_AS(loadTruthTableDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadTruthTableDocument rejects variableCount outside [2,4]", "[truthTable][serializer]") {
    TruthTableDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["variableCount"] = 1;
    json["outputs"] = nlohmann::json::array({{{"name", "F1"}, {"cells", {"0", "0"}}}});
    CHECK_THROWS_AS(loadTruthTableDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadTruthTableDocument rejects an empty outputs array", "[truthTable][serializer]") {
    TruthTableDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["variableCount"] = 2;
    json["outputs"] = nlohmann::json::array();
    CHECK_THROWS_AS(loadTruthTableDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadTruthTableDocument rejects a cells array of the wrong size in any output",
          "[truthTable][serializer]") {
    TruthTableDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["variableCount"] = 2;
    json["outputs"] = nlohmann::json::array(
        {{{"name", "F1"}, {"cells", {"0", "0", "0", "0"}}}, {{"name", "F2"}, {"cells", {"0", "0", "0"}}}});
    CHECK_THROWS_AS(loadTruthTableDocument(doc, json), std::invalid_argument);
}

TEST_CASE("saveTruthTableDocumentToFile/loadTruthTableDocumentFromFile round-trip through disk",
          "[truthTable][serializer]") {
    constexpr const char* kPath = "test_truth_table_serializer_roundtrip.dft";
    TruthTableDocument doc;
    doc.reset(2);
    doc.addOutput("F2");
    doc.setCellValue(0, 3, KarnaughCellValue::One);
    doc.setCellValue(1, 2, KarnaughCellValue::DontCare);
    saveTruthTableDocumentToFile(doc, kPath);

    TruthTableDocument reloaded;
    loadTruthTableDocumentFromFile(reloaded, kPath);
    CHECK(reloaded.variableCount() == 2);
    REQUIRE(reloaded.outputCount() == 2);
    CHECK(reloaded.cellValue(0, 3) == KarnaughCellValue::One);
    CHECK(reloaded.cellValue(1, 2) == KarnaughCellValue::DontCare);
    CHECK(reloaded.cellValue(0, 0) == KarnaughCellValue::Zero);

    std::remove(kPath);
}

TEST_CASE("loadTruthTableDocumentFromFile throws for a missing file", "[truthTable][serializer]") {
    TruthTableDocument doc;
    CHECK_THROWS_AS(loadTruthTableDocumentFromFile(doc, "does_not_exist.dft"), std::runtime_error);
}
