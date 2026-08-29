// Ejercita formats::serializeExcitationTableDocument()/
// loadExcitationTableDocument() (ida y vuelta JSON, con varios tipos de
// flip-flop y estado actual editado a mano) y
// saveExcitationTableDocumentToFile()/loadExcitationTableDocumentFromFile()
// (ida y vuelta a disco), incluidos los casos de entrada malformada. No
// necesita su propio main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "editor/ExcitationTableDocument.hpp"
#include "formats/ExcitationTableSerializer.hpp"

using digitalforge::editor::ExcitationTableDocument;
using digitalforge::editor::FlipFlopType;
using digitalforge::editor::KarnaughCellValue;
using digitalforge::formats::loadExcitationTableDocument;
using digitalforge::formats::loadExcitationTableDocumentFromFile;
using digitalforge::formats::saveExcitationTableDocumentToFile;
using digitalforge::formats::serializeExcitationTableDocument;

TEST_CASE("serializeExcitationTableDocument round-trips bit names, flip-flop types, presentState and nextState",
          "[excitationTable][serializer]") {
    ExcitationTableDocument doc;
    doc.reset(3);
    doc.setStateBitName(0, "X");
    doc.setStateBitName(1, "Y");
    doc.setStateBitName(2, "Z");
    doc.setFlipFlopType(0, FlipFlopType::JK);
    doc.setFlipFlopType(1, FlipFlopType::T);
    doc.setPresentState(0, 2, KarnaughCellValue::DontCare); // fila marcada inalcanzable
    doc.setNextState(0, 0, KarnaughCellValue::One);
    doc.setNextState(0, 1, KarnaughCellValue::DontCare);
    doc.setNextState(1, 7, KarnaughCellValue::One);

    const nlohmann::json json = serializeExcitationTableDocument(doc);
    CHECK(json.at("schemaVersion").get<int>() == 1);
    CHECK(json.at("kind").get<std::string>() == "excitationtable");
    CHECK(json.at("stateBitCount").get<int>() == 3);
    REQUIRE(json.at("bits").size() == 3);
    CHECK(json.at("bits").at(0).at("name").get<std::string>() == "X");
    CHECK(json.at("bits").at(0).at("flipFlopType").get<std::string>() == "JK");
    CHECK(json.at("bits").at(0).at("presentState").size() == 8);
    CHECK(json.at("bits").at(0).at("presentState").at(2).get<std::string>() == "X");
    CHECK(json.at("bits").at(0).at("nextState").size() == 8);
    CHECK(json.at("bits").at(1).at("flipFlopType").get<std::string>() == "T");
    CHECK(json.at("bits").at(2).at("flipFlopType").get<std::string>() == "D");

    ExcitationTableDocument reloaded;
    loadExcitationTableDocument(reloaded, json);
    CHECK(reloaded.stateBitCount() == 3);
    CHECK(reloaded.stateBitName(0) == "X");
    CHECK(reloaded.stateBitName(1) == "Y");
    CHECK(reloaded.stateBitName(2) == "Z");
    CHECK(reloaded.flipFlopType(0) == FlipFlopType::JK);
    CHECK(reloaded.flipFlopType(1) == FlipFlopType::T);
    CHECK(reloaded.flipFlopType(2) == FlipFlopType::D);
    CHECK(reloaded.presentState(0, 2) == KarnaughCellValue::DontCare);
    CHECK(reloaded.presentState(0, 0) == KarnaughCellValue::Zero); // sin editar: valor natural
    CHECK(reloaded.nextState(0, 0) == KarnaughCellValue::One);
    CHECK(reloaded.nextState(0, 1) == KarnaughCellValue::DontCare);
    CHECK(reloaded.nextState(1, 7) == KarnaughCellValue::One);
    CHECK(reloaded.nextState(1, 0) == KarnaughCellValue::Zero);
    CHECK_FALSE(reloaded.dirty()); // loadExcitationTableDocument() lo deja "limpio"
}

namespace {
// bits[] valido de 2 bits/4 celdas, listo para mutar en cada test de rechazo.
nlohmann::json validTwoBitBits() {
    return nlohmann::json::array(
        {{{"name", "Q0"}, {"flipFlopType", "D"}, {"presentState", {"0", "1", "0", "1"}}, {"nextState", {"0", "0", "0", "0"}}},
         {{"name", "Q1"}, {"flipFlopType", "D"}, {"presentState", {"0", "0", "1", "1"}}, {"nextState", {"0", "0", "0", "0"}}}});
}
} // namespace

TEST_CASE("loadExcitationTableDocument rejects an unsupported schemaVersion", "[excitationTable][serializer]") {
    ExcitationTableDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 2;
    json["stateBitCount"] = 2;
    json["bits"] = validTwoBitBits();
    CHECK_THROWS_AS(loadExcitationTableDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadExcitationTableDocument rejects stateBitCount outside [2,4]", "[excitationTable][serializer]") {
    ExcitationTableDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["stateBitCount"] = 1;
    json["bits"] = nlohmann::json::array(
        {{{"name", "Q0"}, {"flipFlopType", "D"}, {"presentState", {"0", "1"}}, {"nextState", {"0", "0"}}}});
    CHECK_THROWS_AS(loadExcitationTableDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadExcitationTableDocument rejects a bits array whose size does not match stateBitCount",
          "[excitationTable][serializer]") {
    ExcitationTableDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["stateBitCount"] = 2;
    json["bits"] = nlohmann::json::array(
        {{{"name", "Q0"}, {"flipFlopType", "D"}, {"presentState", {"0", "1", "0", "1"}}, {"nextState", {"0", "0", "0", "0"}}}});
    CHECK_THROWS_AS(loadExcitationTableDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadExcitationTableDocument rejects a nextState array of the wrong size in any bit",
          "[excitationTable][serializer]") {
    ExcitationTableDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["stateBitCount"] = 2;
    nlohmann::json bits = validTwoBitBits();
    bits[1]["nextState"] = {"0", "0", "0"}; // deberia tener 4 celdas
    json["bits"] = std::move(bits);
    CHECK_THROWS_AS(loadExcitationTableDocument(doc, json), std::invalid_argument);
}

TEST_CASE("loadExcitationTableDocument rejects a presentState array of the wrong size in any bit",
          "[excitationTable][serializer]") {
    ExcitationTableDocument doc;
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["stateBitCount"] = 2;
    nlohmann::json bits = validTwoBitBits();
    bits[0]["presentState"] = {"0", "1", "0"}; // deberia tener 4 celdas
    json["bits"] = std::move(bits);
    CHECK_THROWS_AS(loadExcitationTableDocument(doc, json), std::invalid_argument);
}

TEST_CASE("saveExcitationTableDocumentToFile/loadExcitationTableDocumentFromFile round-trip through disk",
          "[excitationTable][serializer]") {
    constexpr const char* kPath = "test_excitation_table_serializer_roundtrip.dfe";
    ExcitationTableDocument doc;
    doc.reset(2);
    doc.setFlipFlopType(1, FlipFlopType::SR);
    doc.setPresentState(1, 1, KarnaughCellValue::DontCare);
    doc.setNextState(0, 3, KarnaughCellValue::One);
    doc.setNextState(1, 2, KarnaughCellValue::DontCare);
    saveExcitationTableDocumentToFile(doc, kPath);

    ExcitationTableDocument reloaded;
    loadExcitationTableDocumentFromFile(reloaded, kPath);
    CHECK(reloaded.stateBitCount() == 2);
    CHECK(reloaded.flipFlopType(1) == FlipFlopType::SR);
    CHECK(reloaded.presentState(1, 1) == KarnaughCellValue::DontCare);
    CHECK(reloaded.nextState(0, 3) == KarnaughCellValue::One);
    CHECK(reloaded.nextState(1, 2) == KarnaughCellValue::DontCare);
    CHECK(reloaded.nextState(0, 0) == KarnaughCellValue::Zero);

    std::remove(kPath);
}

TEST_CASE("loadExcitationTableDocumentFromFile throws for a missing file", "[excitationTable][serializer]") {
    ExcitationTableDocument doc;
    CHECK_THROWS_AS(loadExcitationTableDocumentFromFile(doc, "does_not_exist.dfe"), std::runtime_error);
}
