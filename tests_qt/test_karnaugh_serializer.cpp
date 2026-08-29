// Ejercita formats::serializeKarnaughDocument()/loadKarnaughDocument() (ida
// y vuelta JSON, con varias funciones) y
// saveKarnaughDocumentToFile()/loadKarnaughDocumentFromFile() (ida y vuelta
// a disco), incluidos los casos de entrada malformada y la compatibilidad
// con el formato viejo de una sola funcion (schemaVersion 1). No necesita su
// propio main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "editor/KarnaughDocument.hpp"
#include "formats/KarnaughSerializer.hpp"

using digitalforge::editor::KarnaughCellValue;
using digitalforge::editor::KarnaughDocument;
using digitalforge::formats::loadKarnaughDocument;
using digitalforge::formats::loadKarnaughDocumentFromFile;
using digitalforge::formats::saveKarnaughDocumentToFile;
using digitalforge::formats::serializeKarnaughDocument;

TEST_CASE("serializeKarnaughDocument round-trips variable names and every function", "[karnaugh][serializer]") {
    KarnaughDocument doc;
    doc.reset(3);
    doc.setVariableName(0, "X");
    doc.setVariableName(1, "Y");
    doc.setVariableName(2, "Z");
    doc.setOutputName(0, "F1");
    doc.setCellValue(0, 0, KarnaughCellValue::One);
    doc.setCellValue(0, 1, KarnaughCellValue::DontCare);
    doc.addOutput("F2");
    doc.setCellValue(1, 7, KarnaughCellValue::One);

    const nlohmann::json json = serializeKarnaughDocument(doc);
    CHECK(json.at("schemaVersion").get<int>() == 2);
    CHECK(json.at("kind").get<std::string>() == "karnaugh");
    CHECK(json.at("variableCount").get<int>() == 3);
    REQUIRE(json.at("outputs").size() == 2);
    CHECK(json.at("outputs").at(0).at("name").get<std::string>() == "F1");
    CHECK(json.at("outputs").at(0).at("cells").size() == 8);
    CHECK(json.at("outputs").at(1).at("name").get<std::string>() == "F2");

    KarnaughDocument reloaded;
    loadKarnaughDocument(reloaded, json);
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
    CHECK_FALSE(reloaded.dirty()); // loadKarnaughDocument() lo deja "limpio"
}

TEST_CASE("loadKarnaughDocument reads a legacy schemaVersion 1 file (flat cells) as a single function F1",
          "[karnaugh][serializer]") {
    KarnaughDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["variableCount"] = 2;
    json["variableNames"] = {"A", "B"};
    json["cells"] = {"0", "1", "X", "0"};

    loadKarnaughDocument(doc, json);
    CHECK(doc.variableCount() == 2);
    REQUIRE(doc.outputCount() == 1);
    CHECK(doc.outputName(0) == "F1");
    CHECK(doc.cellValue(0, 0) == KarnaughCellValue::Zero);
    CHECK(doc.cellValue(0, 1) == KarnaughCellValue::One);
    CHECK(doc.cellValue(0, 2) == KarnaughCellValue::DontCare);
    CHECK_FALSE(doc.dirty());
}

TEST_CASE("loadKarnaughDocument rejects an unsupported schemaVersion", "[karnaugh][serializer]") {
    KarnaughDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 3;
    json["variableCount"] = 2;
    json["outputs"] = nlohmann::json::array({{{"name", "F1"}, {"cells", {"0", "0", "0", "0"}}}});
    CHECK_THROWS_AS(loadKarnaughDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadKarnaughDocument rejects a missing schemaVersion", "[karnaugh][serializer]") {
    KarnaughDocument doc;
    nlohmann::json json;
    json["variableCount"] = 2;
    json["cells"] = {"0", "0", "0", "0"};
    CHECK_THROWS_AS(loadKarnaughDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadKarnaughDocument rejects variableCount outside [2,4]", "[karnaugh][serializer]") {
    KarnaughDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 2;
    json["variableCount"] = 1;
    json["outputs"] = nlohmann::json::array({{{"name", "F1"}, {"cells", {"0", "0"}}}});
    CHECK_THROWS_AS(loadKarnaughDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadKarnaughDocument rejects an empty outputs array", "[karnaugh][serializer]") {
    KarnaughDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 2;
    json["variableCount"] = 2;
    json["outputs"] = nlohmann::json::array();
    CHECK_THROWS_AS(loadKarnaughDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadKarnaughDocument rejects a cells array of the wrong size in any function", "[karnaugh][serializer]") {
    KarnaughDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 2;
    json["variableCount"] = 2;
    json["outputs"] = nlohmann::json::array(
        {{{"name", "F1"}, {"cells", {"0", "0", "0", "0"}}}, {{"name", "F2"}, {"cells", {"0", "0", "0"}}}});
    CHECK_THROWS_AS(loadKarnaughDocument(doc, json), std::invalid_argument);
}

TEST_CASE("saveKarnaughDocumentToFile/loadKarnaughDocumentFromFile round-trip through disk",
          "[karnaugh][serializer]") {
    constexpr const char* kPath = "test_karnaugh_serializer_roundtrip.dfk";
    KarnaughDocument doc;
    doc.reset(2);
    doc.addOutput("F2");
    doc.setCellValue(0, 3, KarnaughCellValue::One);
    doc.setCellValue(1, 2, KarnaughCellValue::DontCare);
    saveKarnaughDocumentToFile(doc, kPath);

    KarnaughDocument reloaded;
    loadKarnaughDocumentFromFile(reloaded, kPath);
    CHECK(reloaded.variableCount() == 2);
    REQUIRE(reloaded.outputCount() == 2);
    CHECK(reloaded.cellValue(0, 3) == KarnaughCellValue::One);
    CHECK(reloaded.cellValue(1, 2) == KarnaughCellValue::DontCare);
    CHECK(reloaded.cellValue(0, 0) == KarnaughCellValue::Zero);

    std::remove(kPath);
}

TEST_CASE("loadKarnaughDocumentFromFile throws for a missing file", "[karnaugh][serializer]") {
    KarnaughDocument doc;
    CHECK_THROWS_AS(loadKarnaughDocumentFromFile(doc, "does_not_exist.dfk"), std::runtime_error);
}
