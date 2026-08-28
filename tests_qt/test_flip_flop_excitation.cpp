// Ejercita editor::computeExcitation() para los 4 tipos de flip-flop contra
// las tablas de excitacion estandar (D, T, JK, SR), incluidos los casos
// Q == DontCare (fila inalcanzable) y Q+ == DontCare. No necesita su propio
// main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "editor/FlipFlopExcitation.hpp"

using digitalforge::editor::computeExcitation;
using digitalforge::editor::ExcitationColumn;
using digitalforge::editor::FlipFlopType;
using digitalforge::editor::KarnaughCellValue;

namespace {
// Los 4 minterms de abajo cubren las 4 combinaciones (Q,Q+) de la tabla de
// excitacion clasica:
//   m0: Q=0,Q+=0   m1: Q=1,Q+=0   m2: Q=0,Q+=1   m3: Q=1,Q+=1
const std::vector<KarnaughCellValue> kPresentState = {KarnaughCellValue::Zero, KarnaughCellValue::One,
                                                       KarnaughCellValue::Zero, KarnaughCellValue::One};
const std::vector<KarnaughCellValue> kNextState = {KarnaughCellValue::Zero, KarnaughCellValue::Zero,
                                                    KarnaughCellValue::One, KarnaughCellValue::One};
} // namespace

TEST_CASE("computeExcitation(D) copies nextState directly and names the column D+bitName",
          "[flipFlop][excitation]") {
    const std::vector<ExcitationColumn> columns = computeExcitation(FlipFlopType::D, "Q0", kPresentState, kNextState);
    REQUIRE(columns.size() == 1);
    CHECK(columns[0].name == "DQ0");
    CHECK(columns[0].values == kNextState);
}

TEST_CASE("computeExcitation(T) is 0 when Q+ == Q and 1 when Q+ != Q", "[flipFlop][excitation]") {
    const std::vector<ExcitationColumn> columns = computeExcitation(FlipFlopType::T, "Q0", kPresentState, kNextState);
    REQUIRE(columns.size() == 1);
    CHECK(columns[0].name == "TQ0");
    // m0: Q=0,Q+=0 -> 0 | m1: Q=1,Q+=0 -> 1 | m2: Q=0,Q+=1 -> 1 | m3: Q=1,Q+=1 -> 0
    const std::vector<KarnaughCellValue> expected = {KarnaughCellValue::Zero, KarnaughCellValue::One,
                                                      KarnaughCellValue::One, KarnaughCellValue::Zero};
    CHECK(columns[0].values == expected);
}

TEST_CASE("computeExcitation(JK) matches the standard J/K excitation table", "[flipFlop][excitation]") {
    const std::vector<ExcitationColumn> columns = computeExcitation(FlipFlopType::JK, "Q0", kPresentState, kNextState);
    REQUIRE(columns.size() == 2);
    CHECK(columns[0].name == "JQ0");
    CHECK(columns[1].name == "KQ0");
    // m0: Q=0,Q+=0 -> J=0,K=X | m1: Q=1,Q+=0 -> J=X,K=1
    // m2: Q=0,Q+=1 -> J=1,K=X | m3: Q=1,Q+=1 -> J=X,K=0
    const std::vector<KarnaughCellValue> expectedJ = {KarnaughCellValue::Zero, KarnaughCellValue::DontCare,
                                                       KarnaughCellValue::One, KarnaughCellValue::DontCare};
    const std::vector<KarnaughCellValue> expectedK = {KarnaughCellValue::DontCare, KarnaughCellValue::One,
                                                       KarnaughCellValue::DontCare, KarnaughCellValue::Zero};
    CHECK(columns[0].values == expectedJ);
    CHECK(columns[1].values == expectedK);
}

TEST_CASE("computeExcitation(SR) matches the standard S/R excitation table", "[flipFlop][excitation]") {
    const std::vector<ExcitationColumn> columns = computeExcitation(FlipFlopType::SR, "Q0", kPresentState, kNextState);
    REQUIRE(columns.size() == 2);
    CHECK(columns[0].name == "SQ0");
    CHECK(columns[1].name == "RQ0");
    // m0: Q=0,Q+=0 -> S=0,R=X | m1: Q=1,Q+=0 -> S=0,R=1
    // m2: Q=0,Q+=1 -> S=1,R=0 | m3: Q=1,Q+=1 -> S=X,R=0
    const std::vector<KarnaughCellValue> expectedS = {KarnaughCellValue::Zero, KarnaughCellValue::Zero,
                                                       KarnaughCellValue::One, KarnaughCellValue::DontCare};
    const std::vector<KarnaughCellValue> expectedR = {KarnaughCellValue::DontCare, KarnaughCellValue::One,
                                                       KarnaughCellValue::Zero, KarnaughCellValue::Zero};
    CHECK(columns[0].values == expectedS);
    CHECK(columns[1].values == expectedR);
}

TEST_CASE("computeExcitation propagates a DontCare next-state to every column, for every flip-flop type",
          "[flipFlop][excitation]") {
    const std::vector<KarnaughCellValue> presentState = {KarnaughCellValue::Zero, KarnaughCellValue::One,
                                                          KarnaughCellValue::Zero, KarnaughCellValue::One};
    const std::vector<KarnaughCellValue> nextStateWithDontCare = {KarnaughCellValue::DontCare, KarnaughCellValue::Zero,
                                                                   KarnaughCellValue::One, KarnaughCellValue::Zero};
    for (const FlipFlopType type : {FlipFlopType::D, FlipFlopType::T, FlipFlopType::JK, FlipFlopType::SR}) {
        const std::vector<ExcitationColumn> columns =
            computeExcitation(type, "Q0", presentState, nextStateWithDontCare);
        for (const ExcitationColumn& column : columns) {
            CHECK(column.values[0] == KarnaughCellValue::DontCare);
        }
    }
}

TEST_CASE("computeExcitation propagates a DontCare present-state to every column, for every flip-flop type "
          "(including D, whose formula does not otherwise depend on Q)",
          "[flipFlop][excitation]") {
    // Fila inalcanzable (estado actual marcado X): la excitacion completa
    // tiene que salir DontCare aunque el estado siguiente este definido --
    // no tiene sentido pedir una entrada concreta para llegar a un estado
    // siguiente partiendo de un estado que nunca ocurre.
    const std::vector<KarnaughCellValue> presentStateWithDontCare = {KarnaughCellValue::DontCare,
                                                                      KarnaughCellValue::Zero, KarnaughCellValue::One,
                                                                      KarnaughCellValue::One};
    const std::vector<KarnaughCellValue> nextState = {KarnaughCellValue::One, KarnaughCellValue::Zero,
                                                       KarnaughCellValue::One, KarnaughCellValue::Zero};
    for (const FlipFlopType type : {FlipFlopType::D, FlipFlopType::T, FlipFlopType::JK, FlipFlopType::SR}) {
        const std::vector<ExcitationColumn> columns =
            computeExcitation(type, "Q0", presentStateWithDontCare, nextState);
        for (const ExcitationColumn& column : columns) {
            CHECK(column.values[0] == KarnaughCellValue::DontCare);
        }
    }
}
