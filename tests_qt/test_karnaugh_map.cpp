// Ejercita editor::minimize() (Quine-McCluskey + metodo de Petrick) y las
// funciones de grilla Gray-code (gridDimensions/mintermAt/gridPositionOf).
// No necesita su propio main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <vector>

#include "editor/KarnaughMap.hpp"

using digitalforge::editor::gridDimensions;
using digitalforge::editor::gridPositionOf;
using digitalforge::editor::Implicant;
using digitalforge::editor::KarnaughCellValue;
using digitalforge::editor::KarnaughResult;
using digitalforge::editor::KarnaughStepKind;
using digitalforge::editor::minimize;
using digitalforge::editor::mintermAt;

namespace {

using CV = KarnaughCellValue;

std::vector<QString> namesFor(int variableCount) {
    static const std::vector<QString> all = {"A", "B", "C", "D"};
    return std::vector<QString>(all.begin(), all.begin() + variableCount);
}

bool coversMinterm(const KarnaughResult& result, int minterm) {
    return std::any_of(result.selectedGroups.begin(), result.selectedGroups.end(), [&](const Implicant& implicant) {
        return std::find(implicant.minterms.begin(), implicant.minterms.end(), minterm) != implicant.minterms.end();
    });
}

bool hasStepKind(const KarnaughResult& result, KarnaughStepKind kind) {
    return std::any_of(result.steps.begin(), result.steps.end(), [&](const auto& step) { return step.kind == kind; });
}

} // namespace

TEST_CASE("gridDimensions matches the conventional 2D Karnaugh layout for 2-4 variables", "[karnaugh][grid]") {
    CHECK(gridDimensions(2) == std::pair<int, int>{2, 2});
    CHECK(gridDimensions(3) == std::pair<int, int>{2, 4});
    CHECK(gridDimensions(4) == std::pair<int, int>{4, 4});
    CHECK_THROWS_AS(gridDimensions(1), std::invalid_argument);
    CHECK_THROWS_AS(gridDimensions(5), std::invalid_argument);
}

TEST_CASE("mintermAt/gridPositionOf round-trip for every cell, 2 to 4 variables", "[karnaugh][grid]") {
    for (int variableCount = 2; variableCount <= 4; ++variableCount) {
        const auto [rows, cols] = gridDimensions(variableCount);
        std::vector<bool> seen(std::size_t{1} << variableCount, false);
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const int minterm = mintermAt(row, col, variableCount);
                REQUIRE(minterm >= 0);
                REQUIRE(minterm < (1 << variableCount));
                CHECK_FALSE(seen[static_cast<std::size_t>(minterm)]); // cada minterm aparece en exactamente una celda
                seen[static_cast<std::size_t>(minterm)] = true;
                CHECK(gridPositionOf(minterm, variableCount) == std::pair<int, int>{row, col});
            }
        }
        CHECK(std::all_of(seen.begin(), seen.end(), [](bool v) { return v; }));
    }
}

TEST_CASE("Adjacent grid cells (including wraparound) differ by exactly one bit", "[karnaugh][grid]") {
    // La propiedad que hace util a un mapa de Karnaugh: dos celdas
    // fisicamente contiguas (incluida la voltereta del borde derecho/
    // inferior al opuesto) representan minterms que difieren en un solo
    // bit -- si esto no se cumpliera, agrupar celdas adyacentes visualmente
    // no correspondería a combinar implicantes validos de Quine-McCluskey.
    for (int variableCount = 2; variableCount <= 4; ++variableCount) {
        const auto [rows, cols] = gridDimensions(variableCount);
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const int here = mintermAt(row, col, variableCount);
                const int right = mintermAt(row, (col + 1) % cols, variableCount);
                const int down = mintermAt((row + 1) % rows, col, variableCount);
                CHECK(std::popcount(static_cast<unsigned>(here ^ right)) == 1);
                CHECK(std::popcount(static_cast<unsigned>(here ^ down)) == 1);
            }
        }
    }
}

TEST_CASE("minimize() on an all-zero 2-variable map yields the constant-0 expression", "[karnaugh]") {
    const KarnaughResult result = minimize(2, namesFor(2), {CV::Zero, CV::Zero, CV::Zero, CV::Zero});
    CHECK(result.sopExpression == "0");
    CHECK(result.selectedGroups.empty());
}

TEST_CASE("minimize() on an all-one 2-variable map yields the constant-1 expression", "[karnaugh]") {
    const KarnaughResult result = minimize(2, namesFor(2), {CV::One, CV::One, CV::One, CV::One});
    CHECK(result.sopExpression == "1");
    REQUIRE(result.selectedGroups.size() == 1);
    CHECK(result.selectedGroups.front().literals(2).empty());
    CHECK(coversMinterm(result, 0));
    CHECK(coversMinterm(result, 3));
}

TEST_CASE("minimize() on a single true minterm yields the full-literal minterm expression", "[karnaugh]") {
    // minterm 3 = bit0(A)=1, bit1(B)=1 -> "A·B"
    const KarnaughResult result = minimize(2, namesFor(2), {CV::Zero, CV::Zero, CV::Zero, CV::One});
    CHECK(result.sopExpression == "A·B");
    REQUIRE(result.selectedGroups.size() == 1);
    CHECK(result.selectedGroups.front().minterms == std::vector<int>{3});
}

TEST_CASE("minimize() combines an adjacent pair into a single-literal expression", "[karnaugh]") {
    // minterms 0 (A=0,B=0) y 1 (A=1,B=0) comparten B=0 -> "B'"
    const KarnaughResult result = minimize(2, namesFor(2), {CV::One, CV::One, CV::Zero, CV::Zero});
    CHECK(result.sopExpression == "B'");
    REQUIRE(result.selectedGroups.size() == 1);
    CHECK(result.selectedGroups.front().literals(2).size() == 1);
    CHECK(hasStepKind(result, KarnaughStepKind::CombinePair));
}

TEST_CASE("minimize() uses a don't-care to fold four minterms into one literal", "[karnaugh][dontcare]") {
    // 3 variables A,B,C (bit0=A,bit1=B,bit2=C). m0,m1,m2 = 1, m3 = don't
    // care, m4..m7 = 0. Sin el don't-care en m3, la cobertura minima de
    // {0,1,2} necesitaria 2 terminos (p.ej. "B'C'" + "A'C'"); al poder usar
    // m3 para agrupar, {0,1,2,3} forma un unico grupo de 4 -> "C'".
    std::vector<CV> cells(8, CV::Zero);
    cells[0] = CV::One;
    cells[1] = CV::One;
    cells[2] = CV::One;
    cells[3] = CV::DontCare;
    const KarnaughResult result = minimize(3, namesFor(3), cells);
    CHECK(result.sopExpression == "C'");
    REQUIRE(result.selectedGroups.size() == 1);
    CHECK(result.selectedGroups.front().literals(3).size() == 1);
    CHECK(coversMinterm(result, 0));
    CHECK(coversMinterm(result, 1));
    CHECK(coversMinterm(result, 2));
}

TEST_CASE("minimize() falls back to Petrick's method when no prime implicant is essential",
          "[karnaugh][petrick]") {
    // 4 variables A,B,C,D. Unico minterm requerido: m0. m1 y m2 son
    // don't-care, cada uno forma un par distinto con m0 ({0,1} via A, {0,2}
    // via B) -- ninguno de los dos primos es esencial (m0 esta cubierto por
    // ambos, y ni m1 ni m2 son ellos mismos requeridos), asi que la
    // seleccion por esenciales no alcanza y el metodo de Petrick decide
    // entre los dos (empatados en 3 literales cada uno).
    std::vector<CV> cells(16, CV::Zero);
    cells[0] = CV::One;
    cells[1] = CV::DontCare;
    cells[2] = CV::DontCare;
    const KarnaughResult result = minimize(4, namesFor(4), cells);

    CHECK(result.essentialPrimeImplicants.empty());
    REQUIRE(result.selectedGroups.size() == 1);
    CHECK(result.selectedGroups.front().minterms.size() == 2);
    CHECK(coversMinterm(result, 0));
    CHECK(hasStepKind(result, KarnaughStepKind::SelectViaPetrick));
    CHECK(result.selectedGroups.front().literals(4).size() == 3);
}

TEST_CASE("minimize() steps cover every required minterm via some FinalTerm step", "[karnaugh]") {
    std::vector<CV> cells(16, CV::Zero);
    for (int m : {1, 3, 4, 6, 9, 12}) {
        cells[static_cast<std::size_t>(m)] = CV::One;
    }
    const KarnaughResult result = minimize(4, namesFor(4), cells);
    REQUIRE_FALSE(result.steps.empty());
    for (int m : {1, 3, 4, 6, 9, 12}) {
        CHECK(coversMinterm(result, m));
    }
    // Cada termino final corresponde a un grupo realmente en
    // selectedGroups, no a un paso huerfano.
    for (const auto& step : result.steps) {
        if (step.kind == KarnaughStepKind::FinalTerm) {
            REQUIRE(step.groupIndex >= 0);
            REQUIRE(step.groupIndex < static_cast<int>(result.selectedGroups.size()));
        }
    }
}

TEST_CASE("minimize() validates variableCount and array sizes", "[karnaugh][validation]") {
    CHECK_THROWS_AS(minimize(1, {"A"}, {CV::Zero, CV::One}), std::invalid_argument);
    CHECK_THROWS_AS(minimize(5, namesFor(4), std::vector<CV>(32, CV::Zero)), std::invalid_argument);
    CHECK_THROWS_AS(minimize(2, {"A"}, {CV::Zero, CV::One, CV::Zero, CV::One}), std::invalid_argument);
    CHECK_THROWS_AS(minimize(2, namesFor(2), {CV::Zero, CV::One}), std::invalid_argument);
}
