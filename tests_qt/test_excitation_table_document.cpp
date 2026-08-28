// Ejercita editor::ExcitationTableDocument: reset()/setStateBitCount(), tipo
// de flip-flop por bit (default D), estado actual editable (default natural)
// y estado siguiente por bit, y los limites de validacion de indice/valor.
// No necesita su propio main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "editor/ExcitationTableDocument.hpp"

using digitalforge::editor::ExcitationTableDocument;
using digitalforge::editor::FlipFlopType;
using digitalforge::editor::KarnaughCellValue;

TEST_CASE("A fresh ExcitationTableDocument starts with 2 state bits Q0/Q1, all D, nextState in Zero and "
          "presentState at its natural value",
          "[excitationTable][document]") {
    ExcitationTableDocument doc;
    CHECK(doc.stateBitCount() == 2);
    CHECK(doc.stateBitName(0) == "Q0");
    CHECK(doc.stateBitName(1) == "Q1");
    CHECK(doc.flipFlopType(0) == FlipFlopType::D);
    CHECK(doc.flipFlopType(1) == FlipFlopType::D);
    for (int m = 0; m < 4; ++m) {
        CHECK(doc.nextState(0, m) == KarnaughCellValue::Zero);
        CHECK(doc.nextState(1, m) == KarnaughCellValue::Zero);
        // presentState arranca en el bit natural del minterm (bit 0 = Q0, bit 1 = Q1).
        CHECK(doc.presentState(0, m) == (((m >> 0) & 1) ? KarnaughCellValue::One : KarnaughCellValue::Zero));
        CHECK(doc.presentState(1, m) == (((m >> 1) & 1) ? KarnaughCellValue::One : KarnaughCellValue::Zero));
    }
    CHECK_FALSE(doc.dirty()); // el estado inicial no cuenta como "sin guardar"
}

TEST_CASE("reset() validates stateBitCount and resets every bit to D/natural-presentState/Zero-nextState",
          "[excitationTable][document]") {
    ExcitationTableDocument doc;
    doc.setFlipFlopType(0, FlipFlopType::JK);
    doc.setPresentState(0, 0, KarnaughCellValue::DontCare);
    doc.setNextState(0, 0, KarnaughCellValue::One);
    doc.reset(3);
    CHECK(doc.stateBitCount() == 3);
    CHECK(doc.stateBitName(2) == "Q2");
    CHECK(doc.flipFlopType(0) == FlipFlopType::D);
    CHECK(doc.presentState(0, 0) == KarnaughCellValue::Zero); // vuelve a su valor natural
    CHECK(doc.nextState(0, 0) == KarnaughCellValue::Zero);
    CHECK_THROWS_AS(doc.reset(1), std::invalid_argument);
    CHECK_THROWS_AS(doc.reset(5), std::invalid_argument);
}

TEST_CASE("setFlipFlopType is independent per bit and emits structureChanged", "[excitationTable][document]") {
    ExcitationTableDocument doc;
    int changedCount = 0;
    QObject::connect(&doc, &ExcitationTableDocument::structureChanged, [&] { ++changedCount; });

    doc.setFlipFlopType(1, FlipFlopType::SR);
    CHECK(doc.flipFlopType(1) == FlipFlopType::SR);
    CHECK(doc.flipFlopType(0) == FlipFlopType::D); // Q0 no se toco
    CHECK(doc.dirty());
    CHECK(changedCount == 1);
}

TEST_CASE("setPresentState is independent per bit, editable to DontCare, and emits presentStateChanged",
          "[excitationTable][document]") {
    ExcitationTableDocument doc;
    std::vector<std::pair<int, int>> changed;
    QObject::connect(&doc, &ExcitationTableDocument::presentStateChanged,
                      [&](int bitIndex, int minterm) { changed.emplace_back(bitIndex, minterm); });

    doc.setPresentState(1, 2, KarnaughCellValue::DontCare);
    CHECK(doc.presentState(1, 2) == KarnaughCellValue::DontCare);
    CHECK(doc.presentState(0, 2) == KarnaughCellValue::Zero); // Q0 no se toco (bit 0 de minterm 2 es 0)
    CHECK(doc.dirty());
    REQUIRE(changed.size() == 1);
    CHECK(changed.front() == std::make_pair(1, 2));
}

TEST_CASE("setNextState is independent per bit and emits nextStateChanged with the right bitIndex",
          "[excitationTable][document]") {
    ExcitationTableDocument doc;
    std::vector<std::pair<int, int>> changed;
    QObject::connect(&doc, &ExcitationTableDocument::nextStateChanged,
                      [&](int bitIndex, int minterm) { changed.emplace_back(bitIndex, minterm); });

    doc.setNextState(1, 2, KarnaughCellValue::DontCare);
    CHECK(doc.nextState(1, 2) == KarnaughCellValue::DontCare);
    CHECK(doc.nextState(0, 2) == KarnaughCellValue::Zero); // Q0 no se toco
    CHECK(doc.dirty());
    REQUIRE(changed.size() == 1);
    CHECK(changed.front() == std::make_pair(1, 2));
}

TEST_CASE("setPresentState/presentState and setNextState/nextState validate bitIndex and minterm ranges",
          "[excitationTable][document]") {
    ExcitationTableDocument doc; // 2 bits de estado, 4 celdas cada uno
    CHECK_THROWS_AS(doc.presentState(0, 4), std::invalid_argument);
    CHECK_THROWS_AS(doc.presentState(0, -1), std::invalid_argument);
    CHECK_THROWS_AS(doc.presentState(2, 0), std::invalid_argument);
    CHECK_THROWS_AS(doc.setPresentState(0, 4, KarnaughCellValue::One), std::invalid_argument);
    CHECK_THROWS_AS(doc.setPresentState(-1, 0, KarnaughCellValue::One), std::invalid_argument);

    CHECK_THROWS_AS(doc.nextState(0, 4), std::invalid_argument);
    CHECK_THROWS_AS(doc.nextState(0, -1), std::invalid_argument);
    CHECK_THROWS_AS(doc.nextState(2, 0), std::invalid_argument);
    CHECK_THROWS_AS(doc.setNextState(0, 4, KarnaughCellValue::One), std::invalid_argument);
    CHECK_THROWS_AS(doc.setNextState(-1, 0, KarnaughCellValue::One), std::invalid_argument);
}

TEST_CASE("setStateBitCount resizes every bit's presentState/nextState, preserving surviving cells and types",
          "[excitationTable][document]") {
    ExcitationTableDocument doc; // 2 bits
    doc.setFlipFlopType(0, FlipFlopType::T);
    doc.setPresentState(0, 3, KarnaughCellValue::DontCare);
    doc.setNextState(0, 3, KarnaughCellValue::One);
    doc.setNextState(1, 3, KarnaughCellValue::DontCare);

    doc.setStateBitCount(3); // grow: bit nuevo Q2 arranca en D, presentState natural, nextState en Zero
    CHECK(doc.stateBitCount() == 3);
    CHECK(doc.flipFlopType(0) == FlipFlopType::T); // sobrevive
    CHECK(doc.flipFlopType(2) == FlipFlopType::D);
    CHECK(doc.presentState(0, 3) == KarnaughCellValue::DontCare); // edicion manual sobrevive
    CHECK(doc.nextState(0, 3) == KarnaughCellValue::One);
    CHECK(doc.nextState(1, 3) == KarnaughCellValue::DontCare);
    for (int m = 0; m < 8; ++m) {
        // Q2 es enteramente nuevo: presentState arranca en su valor natural (bit 2 de m), no en Zero.
        CHECK(doc.presentState(2, m) == (((m >> 2) & 1) ? KarnaughCellValue::One : KarnaughCellValue::Zero));
        CHECK(doc.nextState(2, m) == KarnaughCellValue::Zero);
    }
    // Minterms 4..7 son nuevos para Q0/Q1 (crecieron de 4 a 8 celdas):
    // tambien arrancan en su valor natural, no heredan nada de antes.
    CHECK(doc.presentState(0, 5) == KarnaughCellValue::One); // bit 0 de 5 (0b101) es 1
    CHECK(doc.presentState(1, 5) == KarnaughCellValue::Zero); // bit 1 de 5 (0b101) es 0

    CHECK_THROWS_AS(doc.setStateBitCount(1), std::invalid_argument);
    CHECK_THROWS_AS(doc.setStateBitCount(5), std::invalid_argument);
}

TEST_CASE("setStateBitName validates the index and emits structureChanged", "[excitationTable][document]") {
    ExcitationTableDocument doc;
    int changedCount = 0;
    QObject::connect(&doc, &ExcitationTableDocument::structureChanged, [&] { ++changedCount; });
    doc.setStateBitName(0, "Reset");
    CHECK(doc.stateBitName(0) == "Reset");
    CHECK(changedCount == 1);
    CHECK_THROWS_AS(doc.setStateBitName(2, "X"), std::invalid_argument);
    CHECK_THROWS_AS(doc.stateBitName(-1), std::invalid_argument);
}
