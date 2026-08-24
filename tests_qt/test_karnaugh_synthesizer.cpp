// Ejercita formats::synthesizeToCircuit(): toma un editor::KarnaughResult ya
// minimizado y arma un CircuitDocument real con compuertas AND/OR/NOT. La
// verificacion de correccion reutiliza editor::computeTruthTable() (ya
// existente, ver TruthTable.hpp) para barrer el circuito sintetizado y
// comprobar que reproduce EXACTAMENTE los valores '0'/'1' del mapa de
// Karnaugh original (las celdas "no importa" se excluyen de la
// comparacion, a proposito: el sintetizador puede resolverlas para
// cualquier lado segun que implicante haya elegido cubrir el area).
//
// No necesita su propio main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#include <QUndoStack>

#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/LogicValue.hpp"
#include "editor/CircuitDocument.hpp"
#include "editor/KarnaughMap.hpp"
#include "editor/TruthTable.hpp"
#include "editor/UndoCommands.hpp"
#include "formats/KarnaughSynthesizer.hpp"

using digitalforge::core::LogicValue;
using digitalforge::editor::CircuitDocument;
using digitalforge::editor::computeTruthTable;
using digitalforge::editor::DeleteComponentCommand;
using digitalforge::editor::DeleteWireCommand;
using digitalforge::editor::KarnaughCellValue;
using digitalforge::editor::MergeJunctionCommand;
using digitalforge::editor::minimize;
using digitalforge::editor::TruthTable;
using digitalforge::editor::WireConnection;
using digitalforge::editor::WireEndpoint;
using digitalforge::formats::OutputSpec;
using digitalforge::formats::SynthesisOptions;
using digitalforge::formats::synthesizeMultiOutputToCircuit;
using digitalforge::formats::synthesizeToCircuit;

namespace {

using CV = KarnaughCellValue;

std::vector<QString> namesFor(int variableCount) {
    static const std::vector<QString> all = {"A", "B", "C", "D"};
    return std::vector<QString>(all.begin(), all.begin() + variableCount);
}

// Reproduce exactamente CircuitScene::deleteSelected() (ver CircuitScene.cpp)
// pero a partir de ids explicitos en vez de QGraphicsItem* seleccionados -
// permite ejercitar la misma logica de borrado sobre un CircuitDocument
// desnudo, sin necesitar una escena/vista real (tests_qt corre sobre
// QCoreApplication, no QApplication).
void deleteSelectedIds(CircuitDocument& document, QUndoStack& undoStack, const std::vector<uint32_t>& selectedComponentIds,
                        const std::vector<uint32_t>& selectedWireIds, const std::vector<uint32_t>& selectedJunctionIds) {
    std::vector<uint32_t> componentIds = selectedComponentIds;
    std::vector<uint32_t> wireIds = selectedWireIds;
    for (uint32_t junctionId : selectedJunctionIds) {
        for (const WireConnection& w : document.wiresAttachedToJunction(junctionId)) {
            wireIds.push_back(w.id);
        }
    }
    std::sort(wireIds.begin(), wireIds.end());
    wireIds.erase(std::unique(wireIds.begin(), wireIds.end()), wireIds.end());

    std::set<uint32_t> removedWireIds(wireIds.begin(), wireIds.end());
    for (uint32_t componentId : componentIds) {
        for (const WireConnection& w : document.wiresAttachedToComponent(componentId)) {
            removedWireIds.insert(w.id);
        }
    }
    std::set<uint32_t> candidateJunctionIds;
    for (uint32_t wireId : removedWireIds) {
        if (const WireConnection* w = document.wire(wireId)) {
            for (const WireEndpoint& e : {w->a, w->b}) {
                if (e.isJunction) {
                    candidateJunctionIds.insert(e.id);
                }
            }
        }
    }
    std::vector<uint32_t> pendingMergeJunctionIds;
    for (uint32_t junctionId : candidateJunctionIds) {
        std::size_t survivorCount = 0;
        for (const WireConnection& w : document.wiresAttachedToJunction(junctionId)) {
            survivorCount += removedWireIds.contains(w.id) ? 0 : 1;
        }
        if (survivorCount == 2) {
            pendingMergeJunctionIds.push_back(junctionId);
        }
    }

    const std::size_t commandCount = componentIds.size() + wireIds.size() + pendingMergeJunctionIds.size();
    if (commandCount == 0) {
        return;
    }
    if (commandCount > 1) {
        undoStack.beginMacro("Delete selection");
    }
    for (uint32_t componentId : componentIds) {
        undoStack.push(new DeleteComponentCommand(&document, componentId));
    }
    for (uint32_t wireId : wireIds) {
        if (document.wire(wireId) != nullptr) {
            undoStack.push(new DeleteWireCommand(&document, wireId));
        }
    }
    for (uint32_t junctionId : pendingMergeJunctionIds) {
        const std::vector<WireConnection> survivors = document.wiresAttachedToJunction(junctionId);
        if (survivors.size() != 2) {
            continue;
        }
        undoStack.push(new MergeJunctionCommand(&document, junctionId, survivors[0], survivors[1], {}));
    }
    if (commandCount > 1) {
        undoStack.endMacro();
    }
}

// Sintetiza `cells` y comprueba que el circuito resultante, barrido con
// computeTruthTable(), coincide con `cells` en cada minterm que NO sea
// DontCare. Devuelve la tabla resultante por si el test quiere revisar
// algo mas (p.ej. cuantos componentes tiene el circuito).
TruthTable synthesizeAndVerify(int variableCount, const std::vector<CV>& cells, const SynthesisOptions& options = {}) {
    const auto result = minimize(variableCount, namesFor(variableCount), cells);
    CircuitDocument doc;
    synthesizeToCircuit(doc, result, options);

    const TruthTable table = computeTruthTable(doc);
    REQUIRE(table.inputHeaders.size() == static_cast<std::size_t>(variableCount));
    REQUIRE(table.outputHeaders.size() == 1);
    REQUIRE(table.rows.size() == cells.size());

    for (std::size_t m = 0; m < cells.size(); ++m) {
        if (cells[m] == CV::DontCare) {
            continue;
        }
        const char expected = cells[m] == CV::One ? '1' : '0';
        const char actual = table.rows[m].values[static_cast<std::size_t>(variableCount)];
        INFO("minterm " << m << ": esperaba '" << expected << "', obtuvo '" << actual << "'");
        CHECK(actual == expected);
    }
    return table;
}

} // namespace

TEST_CASE("synthesizeToCircuit reproduces a simple 2-input AND", "[karnaugh][synthesis]") {
    // F = A·B: solo el minterm 3 (A=1,B=1) es 1.
    synthesizeAndVerify(2, {CV::Zero, CV::Zero, CV::Zero, CV::One});
}

TEST_CASE("synthesizeToCircuit reproduces a simple 2-input OR", "[karnaugh][synthesis]") {
    // F = A+B: todo menos el minterm 0 es 1.
    synthesizeAndVerify(2, {CV::Zero, CV::One, CV::One, CV::One});
}

TEST_CASE("synthesizeToCircuit reproduces a 2-variable XOR (two 2-literal terms, both negated variants)",
          "[karnaugh][synthesis]") {
    // F = A'B + AB' (minterms 1 y 2): ejercita negacion en AMBAS
    // variables, en terminos distintos, sin compuerta compartida trivial.
    const TruthTable table = synthesizeAndVerify(2, {CV::Zero, CV::One, CV::One, CV::Zero});
    CHECK(table.rows[0].values[2] == '0');
    CHECK(table.rows[1].values[2] == '1');
    CHECK(table.rows[2].values[2] == '1');
    CHECK(table.rows[3].values[2] == '0');
}

TEST_CASE("synthesizeToCircuit reproduces a 4-variable map simplified by a don't-care", "[karnaugh][synthesis]") {
    // C' con la ayuda del don't-care en m3, extendido a 4 variables (D
    // libre): m0,m1,m2,m8,m9,m10 = 1 (C=0, D=0, A,B libres); m3,m11 = don't
    // care (D=1 con C=0, A,B en los mismos casos); todo lo demas (C=1) = 0.
    std::vector<CV> cells(16, CV::Zero);
    for (int m : {0, 1, 2, 8, 9, 10}) {
        cells[static_cast<std::size_t>(m)] = CV::One;
    }
    cells[3] = CV::DontCare;
    cells[11] = CV::DontCare;
    synthesizeAndVerify(4, cells);
}

TEST_CASE("synthesizeToCircuit reproduces the constant-1 function", "[karnaugh][synthesis]") {
    synthesizeAndVerify(2, {CV::One, CV::One, CV::One, CV::One});
}

TEST_CASE("synthesizeToCircuit reproduces the constant-0 function", "[karnaugh][synthesis]") {
    synthesizeAndVerify(2, {CV::Zero, CV::Zero, CV::Zero, CV::Zero});
}

TEST_CASE("synthesizeToCircuit reproduces a single-literal result without an AND gate", "[karnaugh][synthesis]") {
    // F = A (minterms 1,3: bit0=A=1 sin importar B) -- un solo literal, no
    // debe haber ninguna gates.and en el circuito resultante.
    const auto result = minimize(2, namesFor(2), {CV::Zero, CV::One, CV::Zero, CV::One});
    CHECK(result.sopExpression == "A");
    CircuitDocument doc;
    synthesizeToCircuit(doc, result);
    for (uint32_t id : doc.componentIds()) {
        REQUIRE(doc.component(id)->typeId() != "gates.and");
    }
    synthesizeAndVerify(2, {CV::Zero, CV::One, CV::Zero, CV::One});
}

TEST_CASE("synthesizeToCircuit with useInvertMask=true still reproduces the same function", "[karnaugh][synthesis]") {
    std::vector<CV> cells = {CV::Zero, CV::One, CV::One, CV::Zero};
    SynthesisOptions options;
    options.useInvertMask = true;
    synthesizeAndVerify(2, cells, options);
}

TEST_CASE("synthesizeToCircuit clears any pre-existing content in the target document",
          "[karnaugh][synthesis]") {
    CircuitDocument doc;
    doc.addComponent("io.led");
    REQUIRE(doc.componentIds().size() == 1);

    const auto result = minimize(2, namesFor(2), {CV::Zero, CV::Zero, CV::Zero, CV::One});
    synthesizeToCircuit(doc, result);

    for (uint32_t id : doc.componentIds()) {
        CHECK(doc.component(id)->typeId() != "io.led");
    }
}

TEST_CASE("synthesizeToCircuit routes a fanned-out net as a shared bus, not N crossing diagonals",
          "[karnaugh][synthesis]") {
    // F = AB + AC + BC (funcion de mayoria de 3 variables): las tres
    // variables aparecen cada una en dos terminos, asi que sus tres redes
    // "verdaderas" tienen 3 consumidores cada una (la entrada propia + los 2
    // gates.and que la usan) -> wireNet() arma un punto de union de grado 3
    // por variable. Reportado por el usuario: el circuito sintetizado
    // quedaba lleno de quiebres y cruces porque cada ramal se cableaba con
    // una linea recta de un solo codo directo a su consumidor, sin importar
    // cuan lejos quedara. Este test verifica el arreglo: los ramales de una
    // misma red comparten el mismo carril vertical (mismo X de waypoint), y
    // redes de variables distintas usan carriles distintos.
    const std::vector<KarnaughCellValue> cells = {CV::Zero, CV::Zero, CV::Zero, CV::One,
                                                   CV::Zero, CV::One,  CV::One,  CV::One};
    const auto result = minimize(3, namesFor(3), cells);
    REQUIRE(result.selectedGroups.size() == 3);

    CircuitDocument doc;
    synthesizeToCircuit(doc, result);
    synthesizeAndVerify(3, cells); // sigue siendo logicamente correcto

    const std::vector<uint32_t> junctionIds = doc.junctionIds();
    REQUIRE(junctionIds.size() == 3); // una estrella de grado 3 por variable

    std::set<qreal> laneXs;
    for (uint32_t junctionId : junctionIds) {
        const std::vector<WireConnection> attached = doc.wiresAttachedToJunction(junctionId);
        REQUIRE(attached.size() == 3);
        const QPointF junctionPos = doc.junctionPosition(junctionId);

        std::optional<qreal> laneX;
        for (const WireConnection& wire : attached) {
            if (wire.waypoints.empty()) {
                // El unico ramal que puede quedar sin waypoint es el que va
                // a un consumidor en la MISMA fila que el punto de union
                // (la propia entrada): ahi un tramo recto ya es correcto.
                continue;
            }
            REQUIRE(wire.waypoints.size() == 1);
            // Todo ramal con waypoint arranca en vertical por el carril del
            // punto de union: su X coincide con el de la union.
            CHECK(wire.waypoints.front().x() == junctionPos.x());
            if (!laneX.has_value()) {
                laneX = wire.waypoints.front().x();
            } else {
                CHECK(*laneX == wire.waypoints.front().x());
            }
        }
        REQUIRE(laneX.has_value());
        laneXs.insert(*laneX);
    }
    // Las tres variables no comparten carril: si lo hicieran, sus buses se
    // superpondrian exactamente igual que las diagonales que reemplazan.
    CHECK(laneXs.size() == 3);
}

TEST_CASE("synthesizeMultiOutputToCircuit shares one set of inputs across every output",
          "[karnaugh][synthesis][multiOutput]") {
    // Dos salidas de 3 variables sobre las MISMAS A,B,C: F1 = mayoria(A,B,C),
    // F2 = A*B (C no le importa). Reproduce a mano lo que el usuario venia
    // armando copiando y pegando circuitos de una sola salida (el
    // decodificador BCD a 7 segmentos): esta funcion debe dar UN circuito con
    // 3 wiring.input compartidas, no 6.
    const std::vector<KarnaughCellValue> majorityCells = {CV::Zero, CV::Zero, CV::Zero, CV::One,
                                                            CV::Zero, CV::One,  CV::One,  CV::One};
    const std::vector<KarnaughCellValue> andCells = {CV::Zero, CV::Zero, CV::Zero, CV::One,
                                                       CV::Zero, CV::Zero, CV::Zero, CV::One};
    const auto names = namesFor(3);
    std::vector<OutputSpec> outputs;
    outputs.push_back(OutputSpec{"F1", minimize(3, names, majorityCells)});
    outputs.push_back(OutputSpec{"F2", minimize(3, names, andCells)});

    CircuitDocument doc;
    synthesizeMultiOutputToCircuit(doc, 3, names, outputs);

    int inputCount = 0;
    int outputCount = 0;
    for (uint32_t id : doc.componentIds()) {
        const std::string& typeId = doc.component(id)->typeId();
        if (typeId == "wiring.input") {
            ++inputCount;
        } else if (typeId == "wiring.output") {
            ++outputCount;
        }
    }
    CHECK(inputCount == 3); // compartidas entre las dos salidas, no 3+3
    CHECK(outputCount == 2);

    const TruthTable table = computeTruthTable(doc);
    REQUIRE(table.inputHeaders.size() == 3);
    REQUIRE(table.outputHeaders.size() == 2);
    REQUIRE(table.rows.size() == 8);
    for (std::size_t m = 0; m < 8; ++m) {
        INFO("minterm " << m);
        CHECK(table.rows[m].values[3] == (majorityCells[m] == CV::One ? '1' : '0'));
        CHECK(table.rows[m].values[4] == (andCells[m] == CV::One ? '1' : '0'));
    }
}

TEST_CASE("synthesizeMultiOutputToCircuit rejects an empty output list", "[karnaugh][synthesis][multiOutput]") {
    CircuitDocument doc;
    CHECK_THROWS_AS(synthesizeMultiOutputToCircuit(doc, 2, namesFor(2), {}), std::invalid_argument);
}

TEST_CASE("deleteSelected() on a synthesized circuit never crashes, for every possible multi-selection",
          "[karnaugh][synthesis][regression]") {
    // F = A*B + A*C (minterms 3,5,7 de 3 variables): ambos terminos
    // comparten la variable A sin negar, asi que su red "verdadera" tiene 3
    // consumidores (la propia entrada A, y el In0 de cada AND) -> wireNet()
    // (ver KarnaughSynthesizer.cpp) arma ahi un punto de union real de
    // grado 3, algo que ningun otro test de este archivo ejercita todavia.
    // Reportado por el usuario: "genero componentes, pero al seleccionar
    // documento karnaugh y eliminar crasheo aplicacion" -- este test cubre
    // exhaustivamente (2^N - 1 subconjuntos no vacios) cada combinacion
    // posible de componentes/cables/uniones seleccionados a la vez sobre
    // exactamente esa topologia, para encontrar cualquier crash de borrado
    // en cascada que los tests dirigidos de test_circuit_document.cpp no
    // hayan cubierto todavia.
    const std::vector<KarnaughCellValue> cells = {CV::Zero, CV::Zero, CV::Zero, CV::One,
                                                   CV::Zero, CV::One,  CV::Zero, CV::One};
    const auto result = minimize(3, namesFor(3), cells);
    REQUIRE(result.selectedGroups.size() == 2);

    CircuitDocument templateDoc;
    synthesizeToCircuit(templateDoc, result);
    const std::vector<uint32_t> allComponentIds = templateDoc.componentIds();
    const std::vector<uint32_t> allWireIds = templateDoc.wireIds();
    const std::vector<uint32_t> allJunctionIds = templateDoc.junctionIds();
    REQUIRE(allJunctionIds.size() == 1); // el fan-out de A predicho arriba

    const std::size_t itemCount = allComponentIds.size() + allWireIds.size() + allJunctionIds.size();
    REQUIRE(itemCount < 24); // exhaustivo solo es viable con pocos items (2^itemCount combinaciones)
    const uint32_t combinationCount = 1U << itemCount;

    for (uint32_t mask = 1; mask < combinationCount; ++mask) {
        CircuitDocument doc;
        synthesizeToCircuit(doc, result);
        QUndoStack undoStack;

        std::vector<uint32_t> selectedComponentIds;
        std::vector<uint32_t> selectedWireIds;
        std::vector<uint32_t> selectedJunctionIds;
        std::size_t bit = 0;
        for (uint32_t id : allComponentIds) {
            if ((mask >> bit) & 1U) {
                selectedComponentIds.push_back(id);
            }
            ++bit;
        }
        for (uint32_t id : allWireIds) {
            if ((mask >> bit) & 1U) {
                selectedWireIds.push_back(id);
            }
            ++bit;
        }
        for (uint32_t id : allJunctionIds) {
            if ((mask >> bit) & 1U) {
                selectedJunctionIds.push_back(id);
            }
            ++bit;
        }

        INFO("mask = " << mask);
        REQUIRE_NOTHROW(
            deleteSelectedIds(doc, undoStack, selectedComponentIds, selectedWireIds, selectedJunctionIds));
    }
}
