// Ejercita el modelo de puntos de union (Junction/WireEndpoint) de
// CircuitDocument y los comandos de undo que lo manipulan (SplitWireCommand
// en particular). No necesita su propio main(): comparte el
// CATCH_CONFIG_RUNNER definido en test_project_serializer.cpp (ver
// tests_qt/CMakeLists.txt).

#include <QDir>
#include <QPointF>
#include <QTemporaryDir>
#include <QUndoStack>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <utility>
#include <vector>

#include "editor/CircuitDocument.hpp"
#include "editor/UndoCommands.hpp"

using digitalforge::core::LogicValue;
using digitalforge::components::PropertyValue;
using digitalforge::editor::AddJunctionCommand;
using digitalforge::editor::AddWireCommand;
using digitalforge::editor::CircuitDocument;
using digitalforge::editor::DeleteWireCommand;
using digitalforge::editor::MergeJunctionCommand;
using digitalforge::editor::MoveJunctionCommand;
using digitalforge::editor::PinRef;
using digitalforge::editor::RetargetWireEndpointCommand;
using digitalforge::editor::SetZOrderCommand;
using digitalforge::editor::SplitWireCommand;
using digitalforge::editor::WireConnection;
using digitalforge::editor::WireEndpoint;

TEST_CASE("A pin-junction-pin wire chain merges both pins into one net", "[circuitdocument][junction]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t led = doc.addComponent("io.led");

    const uint32_t junctionId = doc.reserveJunctionId();
    doc.addJunctionWithId(junctionId, QPointF(50, 0));
    doc.addWire(PinRef{in0, 0}, WireEndpoint::junction(junctionId));
    doc.addWire(WireEndpoint::junction(junctionId), PinRef{led, 0});

    CHECK(doc.pinValue(led, 0) == LogicValue::One);
    CHECK(doc.endpointValue(WireEndpoint::junction(junctionId)) == LogicValue::One);
}

TEST_CASE("A junction survives at degree 1 and is removed at degree 0", "[circuitdocument][junction]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input");
    const uint32_t in1 = doc.addComponent("wiring.input");

    const uint32_t junctionId = doc.reserveJunctionId();
    doc.addJunctionWithId(junctionId, QPointF(0, 0));
    const uint32_t wire1 = doc.addWire(PinRef{in0, 0}, WireEndpoint::junction(junctionId));
    const uint32_t wire2 = doc.addWire(PinRef{in1, 0}, WireEndpoint::junction(junctionId));
    REQUIRE(doc.junctionIds().size() == 1);

    doc.removeWire(wire2);
    CHECK(doc.junctionIds().size() == 1); // grado 1: una punta al aire, pero sigue vivo

    doc.removeWire(wire1);
    CHECK(doc.junctionIds().empty()); // grado 0: se elimino en cascada
}

TEST_CASE("retargetWire moves one endpoint to another pin, and is undoable", "[circuitdocument][retarget]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t ledA = doc.addComponent("io.led");
    const uint32_t ledB = doc.addComponent("io.led");

    const uint32_t wireId = doc.addWire(PinRef{in0, 0}, PinRef{ledA, 0});
    REQUIRE(doc.pinValue(ledA, 0) == LogicValue::One);

    QUndoStack undoStack;
    undoStack.push(new RetargetWireEndpointCommand(&doc, wireId, /*endIsA=*/false, WireEndpoint(PinRef{ledB, 0})));

    // La energia se movio de ledA a ledB; ledA queda sin manejar (ya no One).
    CHECK(doc.pinValue(ledB, 0) == LogicValue::One);
    CHECK(doc.pinValue(ledA, 0) != LogicValue::One);

    undoStack.undo();
    CHECK(doc.pinValue(ledA, 0) == LogicValue::One);
    CHECK(doc.pinValue(ledB, 0) != LogicValue::One);
}

TEST_CASE("retargetWire refuses a destination that would connect the wire to itself",
          "[circuitdocument][retarget]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input");
    const uint32_t led = doc.addComponent("io.led");
    const uint32_t wireId = doc.addWire(PinRef{in0, 0}, PinRef{led, 0});

    // Reconectar el extremo B al mismo pin que el extremo A dejaria a==b.
    CHECK_FALSE(doc.retargetWire(wireId, /*endIsA=*/false, WireEndpoint(PinRef{in0, 0})));
    // El cable no cambio.
    const auto* w = doc.wire(wireId);
    REQUIRE(w != nullptr);
    CHECK(w->b == WireEndpoint(PinRef{led, 0}));
}

TEST_CASE("A pin accepts multiple direct wires, all merging into the same net (estilo Proteus)",
          "[circuitdocument][wire]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t ledA = doc.addComponent("io.led");
    const uint32_t ledB = doc.addComponent("io.led");

    doc.addWire(PinRef{in0, 0}, PinRef{ledA, 0});
    // Un segundo cable directo al mismo pin ya no se rechaza -- un pin
    // acepta cuantos cables hagan falta, sin pasar por un punto de union
    // intermedio (ver CircuitDocument::wiresAttachedToPin()).
    CHECK_NOTHROW(doc.addWire(PinRef{in0, 0}, PinRef{ledB, 0}));
    CHECK(doc.wiresAttachedToPin(PinRef{in0, 0}).size() == 2);

    CHECK(doc.pinValue(ledA, 0) == LogicValue::One);
    CHECK(doc.pinValue(ledB, 0) == LogicValue::One);
}

TEST_CASE("retargetWire accepts a pin that already has another wire, merging both into one net",
          "[circuitdocument][retarget]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t ledA = doc.addComponent("io.led");
    const uint32_t ledB = doc.addComponent("io.led");
    const uint32_t ledC = doc.addComponent("io.led"); // sumidero, sin driver propio -- no compite por la net

    doc.addWire(PinRef{in0, 0}, PinRef{ledA, 0});
    const uint32_t wireToLedB = doc.addWire(PinRef{ledC, 0}, PinRef{ledB, 0});

    // ledA ya tiene un cable (el de in0) -- reapuntar el otro cable ahi ya
    // no se rechaza: el pin termina con dos cables, los dos en la misma net.
    CHECK(doc.retargetWire(wireToLedB, /*endIsA=*/false, WireEndpoint(PinRef{ledA, 0})));
    const auto* w = doc.wire(wireToLedB);
    REQUIRE(w != nullptr);
    CHECK(w->b == WireEndpoint(PinRef{ledA, 0}));
    CHECK(doc.wiresAttachedToPin(PinRef{ledA, 0}).size() == 2);
    CHECK(doc.pinValue(ledA, 0) == LogicValue::One);
    CHECK(doc.pinValue(ledC, 0) == LogicValue::One); // ledC ahora comparte la net de in0 via ledA

    // Reapuntarlo a su MISMA posicion actual (el pin que ya tenia) sigue
    // siendo un no-op valido -- ya no hace falta ningun caso especial para
    // esto, un pin ocupado (aunque sea por si mismo) siempre es un destino
    // valido.
    CHECK(doc.retargetWire(wireToLedB, /*endIsA=*/false, WireEndpoint(PinRef{ledA, 0})));
}

TEST_CASE("A geometric-connection hint merges two pins into one net without a drawn wire",
          "[circuitdocument][geometric]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t led = doc.addComponent("io.led");

    // Sin cable ni hint: el led no recibe nada del input.
    REQUIRE(doc.pinValue(led, 0) != LogicValue::One);

    // Un hint geometrico (como el que produce la vista cuando dos pines caen
    // en la misma celda) los une electricamente.
    doc.setGeometricConnectionProvider([in0, led] {
        return std::vector<std::pair<WireEndpoint, WireEndpoint>>{
            {WireEndpoint(PinRef{in0, 0}), WireEndpoint(PinRef{led, 0})}};
    });
    CHECK(doc.pinValue(led, 0) == LogicValue::One);

    // Quitar el provider los vuelve a separar.
    doc.setGeometricConnectionProvider(nullptr);
    CHECK(doc.pinValue(led, 0) != LogicValue::One);
}

TEST_CASE("AddJunctionCommand creates a free junction and undo removes it", "[circuitdocument][geometric]") {
    CircuitDocument doc;
    QUndoStack undoStack;
    auto* command = new AddJunctionCommand(&doc, QPointF(16, 16));
    undoStack.push(command);
    REQUIRE(doc.junctionIds().size() == 1);

    undoStack.undo();
    CHECK(doc.junctionIds().empty()); // sin cables, se elimina al deshacer

    undoStack.redo();
    CHECK(doc.junctionIds().size() == 1);
}

TEST_CASE("setJunctionPosition relocates a junction and is undoable via MoveJunctionCommand",
          "[circuitdocument][junction]") {
    CircuitDocument doc;
    const uint32_t junctionId = doc.reserveJunctionId();
    doc.addJunctionWithId(junctionId, QPointF(0, 0));

    doc.setJunctionPosition(junctionId, QPointF(40, 24));
    CHECK(doc.junctionPosition(junctionId) == QPointF(40, 24));

    QUndoStack undoStack;
    undoStack.push(new MoveJunctionCommand(&doc, junctionId, QPointF(40, 24), QPointF(80, 24)));
    CHECK(doc.junctionPosition(junctionId) == QPointF(80, 24));

    undoStack.undo();
    CHECK(doc.junctionPosition(junctionId) == QPointF(40, 24));

    undoStack.redo();
    CHECK(doc.junctionPosition(junctionId) == QPointF(80, 24));
}

TEST_CASE("SetZOrderCommand changes ComponentPlacement::zOrder and is undoable", "[circuitdocument][undocommands]") {
    CircuitDocument doc;
    const uint32_t componentId = doc.addComponent("gates.and");
    REQUIRE(doc.componentPlacement(componentId).zOrder == 0);

    QUndoStack undoStack;
    undoStack.push(new SetZOrderCommand(&doc, componentId, 0, 5));
    CHECK(doc.componentPlacement(componentId).zOrder == 5);

    undoStack.undo();
    CHECK(doc.componentPlacement(componentId).zOrder == 0);

    undoStack.redo();
    CHECK(doc.componentPlacement(componentId).zOrder == 5);
}

TEST_CASE("SplitWireCommand preserves connectivity through the two new segments plus the branch",
          "[undocommands][junction]") {
    CircuitDocument doc;
    QUndoStack undoStack;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t led = doc.addComponent("io.led");
    const uint32_t led2 = doc.addComponent("io.led");

    const uint32_t originalWireId = doc.addWire(PinRef{in0, 0}, PinRef{led, 0});
    REQUIRE(doc.pinValue(led, 0) == LogicValue::One);

    auto* splitCommand = new SplitWireCommand(&doc, originalWireId, QPointF(50, 0));
    undoStack.push(splitCommand);
    const uint32_t junctionId = splitCommand->junctionId();

    CHECK(doc.pinValue(led, 0) == LogicValue::One); // sigue llegando a traves de los dos segmentos nuevos

    undoStack.push(new AddWireCommand(&doc, WireEndpoint::junction(junctionId), PinRef{led2, 0}));
    CHECK(doc.pinValue(led2, 0) == LogicValue::One); // la derivacion nueva tambien recibe la senal
}

TEST_CASE("SplitWireCommand preserves the waypoints handed to it for each new segment",
          "[undocommands][junction]") {
    // Regresion del bug reportado: la mitad de un cable que se estaba
    // derivando perdia su trazado ya acomodado en cuanto se conectaba otro
    // cable sobre su cuerpo, porque SplitWireCommand::redo() los reemplazaba
    // por listas vacias sin importar lo que tuviera el cable original.
    CircuitDocument doc;
    QUndoStack undoStack;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t led = doc.addComponent("io.led");
    const uint32_t originalWireId = doc.addWire(PinRef{in0, 0}, PinRef{led, 0});

    const std::vector<QPointF> before{QPointF(10, 0), QPointF(10, 20)};
    const std::vector<QPointF> after{QPointF(80, 20)};
    auto* splitCommand = new SplitWireCommand(&doc, originalWireId, QPointF(50, 0), before, after);
    undoStack.push(splitCommand);

    bool foundBefore = false;
    bool foundAfter = false;
    for (uint32_t id : doc.wireIds()) {
        const WireConnection* w = doc.wire(id);
        REQUIRE(w != nullptr);
        foundBefore = foundBefore || w->waypoints == before;
        foundAfter = foundAfter || w->waypoints == after;
    }
    CHECK(foundBefore);
    CHECK(foundAfter);
    CHECK(doc.pinValue(led, 0) == LogicValue::One);
}

TEST_CASE("MergeJunctionCommand replaces two wires at a degree-2 junction with one",
          "[undocommands][junction]") {
    // El inverso de un split: si la derivacion que motivo el T se borra
    // despues, el punto de union sobrevivia para siempre en grado 2 con su
    // punto visible (el bug de "queda una bola" al borrar un cable conectado
    // a otro). MergeJunctionCommand lo reemplaza por un unico cable.
    CircuitDocument doc;
    QUndoStack undoStack;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t led = doc.addComponent("io.led");

    const uint32_t junctionId = doc.reserveJunctionId();
    doc.addJunctionWithId(junctionId, QPointF(50, 0));
    const uint32_t wire1Id = doc.addWire(PinRef{in0, 0}, WireEndpoint::junction(junctionId));
    const uint32_t wire2Id = doc.addWire(WireEndpoint::junction(junctionId), PinRef{led, 0});
    doc.setWireWaypoints(wire1Id, {QPointF(20, 0)});
    doc.setWireWaypoints(wire2Id, {QPointF(80, 0)});
    REQUIRE(doc.pinValue(led, 0) == LogicValue::One);

    const WireConnection wire1 = *doc.wire(wire1Id);
    const WireConnection wire2 = *doc.wire(wire2Id);
    const std::vector<QPointF> merged{QPointF(20, 0), QPointF(80, 0)};
    undoStack.push(new MergeJunctionCommand(&doc, junctionId, wire1, wire2, merged));

    CHECK(doc.wireIds().size() == 1);
    CHECK(doc.junctionIds().empty());
    CHECK(doc.pinValue(led, 0) == LogicValue::One);

    undoStack.undo();
    CHECK(doc.wireIds().size() == 2);
    CHECK(doc.junctionIds().size() == 1);
    CHECK(doc.pinValue(led, 0) == LogicValue::One);
}

TEST_CASE("MergeJunctionCommand does not orphan a dead-end junction on the surviving side",
          "[undocommands][junction]") {
    // deadEndJunction--wireToMerge--junctionId--wireBranch--branch
    //                                    |
    //                                 wireOther--led
    // deadEndJunction no tiene NINGUNA otra conexion salvo wireToMerge
    // (grado 1): al fusionar junctionId (que pierde wireBranch y le quedan
    // wireToMerge/wireOther como sobrevivientes), el cable nuevo debe
    // conectar deadEndJunction con led directamente. El bug real:
    // MergeJunctionCommand::redo() borraba wireToMerge/wireOther ANTES de
    // restaurar el cable nuevo -- borrar wireToMerge dejaba a
    // deadEndJunction en grado 0 un instante, y eraseWireCascading() la
    // eliminaba de junctions_ antes de que el cable nuevo llegara a
    // referenciarla, dejando una referencia colgante que
    // rebuildSimulation() no podia resolver (map::at). Reproduce el crash
    // real reportado al borrar una derivacion cuyo otro extremo
    // sobreviviente terminaba en una union asi (ver
    // AppData/DigitalForge/autosave.dfproj del usuario, wire real "37").
    CircuitDocument doc;
    QUndoStack undoStack;
    const uint32_t led = doc.addComponent("io.led");
    const uint32_t branch = doc.addComponent("io.led");

    const uint32_t junctionId = doc.reserveJunctionId();
    const uint32_t deadEndJunction = doc.reserveJunctionId();
    doc.addJunctionWithId(junctionId, QPointF(50, 0));
    doc.addJunctionWithId(deadEndJunction, QPointF(0, 0));

    const uint32_t wireToMerge = doc.addWire(WireEndpoint::junction(deadEndJunction), WireEndpoint::junction(junctionId));
    const uint32_t wireOther = doc.addWire(WireEndpoint::junction(junctionId), PinRef{led, 0});
    const uint32_t wireBranch = doc.addWire(WireEndpoint::junction(junctionId), PinRef{branch, 0});
    REQUIRE(doc.junctionIds().size() == 2);
    REQUIRE(doc.wiresAttachedToJunction(deadEndJunction).size() == 1); // solo wireToMerge

    undoStack.beginMacro("Eliminar");
    undoStack.push(new DeleteWireCommand(&doc, wireBranch));
    const WireConnection survivor1 = *doc.wire(wireToMerge);
    const WireConnection survivor2 = *doc.wire(wireOther);
    undoStack.push(new MergeJunctionCommand(&doc, junctionId, survivor1, survivor2, {}));
    undoStack.endMacro();

    // No crashea (si MergeJunctionCommand::redo() dejara a deadEndJunction
    // colgando, esto ya habria lanzado map::at antes de llegar aca). La
    // union que se fusiona es junctionId; deadEndJunction sigue vivo, ahora
    // conectado directo a led por el cable nuevo.
    CHECK(doc.junctionIds().size() == 1);
    REQUIRE(doc.junctionIds().front() == deadEndJunction);
    REQUIRE(doc.wireIds().size() == 1);
    const WireConnection& merged = *doc.wire(doc.wireIds().front());
    const WireEndpoint deadEnd = WireEndpoint::junction(deadEndJunction);
    CHECK((merged.a == deadEnd || merged.b == deadEnd));
    CHECK((merged.a == WireEndpoint(PinRef{led, 0}) || merged.b == WireEndpoint(PinRef{led, 0})));

    undoStack.undo();
    CHECK(doc.junctionIds().size() == 2);
    CHECK(doc.wireIds().size() == 3);
    CHECK(doc.wiresAttachedToJunction(deadEndJunction).size() == 1);
    CHECK(doc.wiresAttachedToJunction(junctionId).size() == 3); // wireToMerge + wireOther + wireBranch, todo de vuelta
}

TEST_CASE("Deleting the branch wire at two adjacent junctions in the same gesture merges both without "
          "corrupting the document",
          "[undocommands][junction]") {
    // Reproduce CircuitScene::deleteSelected(): compA--w1--J1--w2--J2--w3--compB,
    // con una tercera derivacion en cada union (w4 en J1, w5 en J2). Al
    // seleccionar y borrar w4+w5 a la vez, las DOS uniones quedan en grado 2
    // en el mismo gesto y comparten w2 como sobreviviente. Un primer intento
    // de arreglo pasaba a cada MergeJunctionCommand un snapshot de
    // sobrevivientes tomado ANTES de borrar nada -- crasheaba (map::at) porque
    // el primer merge consume w2 (y en cascada borra J1), dejando al segundo
    // con una referencia a w2/J1 ya inexistentes. El arreglo real (ver
    // CircuitScene::deleteSelected()) vuelve a leer los sobrevivientes del
    // documento, en vivo, justo antes de cada push -- lo que este test
    // reproduce para verificar el algoritmo sin necesitar un QGraphicsScene
    // completo.
    CircuitDocument doc;
    QUndoStack undoStack;
    const uint32_t compA = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t compB = doc.addComponent("io.led");
    const uint32_t branchA = doc.addComponent("io.led");
    const uint32_t branchB = doc.addComponent("io.led");

    const uint32_t j1 = doc.reserveJunctionId();
    const uint32_t j2 = doc.reserveJunctionId();
    doc.addJunctionWithId(j1, QPointF(50, 0));
    doc.addJunctionWithId(j2, QPointF(100, 0));

    doc.addWire(PinRef{compA, 0}, WireEndpoint::junction(j1));
    doc.addWire(WireEndpoint::junction(j1), WireEndpoint::junction(j2));
    doc.addWire(WireEndpoint::junction(j2), PinRef{compB, 0});
    const uint32_t w4 = doc.addWire(WireEndpoint::junction(j1), PinRef{branchA, 0});
    const uint32_t w5 = doc.addWire(WireEndpoint::junction(j2), PinRef{branchB, 0});
    REQUIRE(doc.junctionIds().size() == 2);
    REQUIRE(doc.pinValue(compB, 0) == LogicValue::One);

    undoStack.beginMacro("Eliminar");
    undoStack.push(new DeleteWireCommand(&doc, w4));
    undoStack.push(new DeleteWireCommand(&doc, w5));
    for (uint32_t junctionId : {j1, j2}) {
        // Releido en vivo, no de un snapshot tomado antes del bucle: para
        // cuando le toca el turno a j2, el merge de j1 ya pudo haber
        // reemplazado el cable que compartian.
        const std::vector<WireConnection> survivors = doc.wiresAttachedToJunction(junctionId);
        if (survivors.size() != 2) {
            continue;
        }
        undoStack.push(new MergeJunctionCommand(&doc, junctionId, survivors[0], survivors[1], {}));
    }
    undoStack.endMacro();

    CHECK(doc.junctionIds().empty());
    CHECK(doc.wireIds().size() == 1);
    CHECK(doc.pinValue(compB, 0) == LogicValue::One);

    undoStack.undo();
    CHECK(doc.junctionIds().size() == 2);
    CHECK(doc.wireIds().size() == 5); // w1, w2, w3, w4, w5 -- el macro completo
    CHECK(doc.pinValue(compB, 0) == LogicValue::One);
}

TEST_CASE("Undo/redo of a split+branch macro is a single step and fully restores connectivity",
          "[undocommands][junction]") {
    CircuitDocument doc;
    QUndoStack undoStack;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t led = doc.addComponent("io.led");
    const uint32_t led2 = doc.addComponent("io.led");
    const uint32_t originalWireId = doc.addWire(PinRef{in0, 0}, PinRef{led, 0});

    undoStack.beginMacro("Derivar cable");
    auto* splitCommand = new SplitWireCommand(&doc, originalWireId, QPointF(50, 0));
    undoStack.push(splitCommand);
    const uint32_t junctionId = splitCommand->junctionId();
    undoStack.push(new AddWireCommand(&doc, WireEndpoint::junction(junctionId), PinRef{led2, 0}));
    undoStack.endMacro();

    REQUIRE(doc.wireIds().size() == 3);
    REQUIRE(doc.junctionIds().size() == 1);
    CHECK(doc.pinValue(led2, 0) == LogicValue::One);

    undoStack.undo(); // un solo paso deshace todo el macro
    CHECK(doc.wireIds().size() == 1);
    CHECK(doc.junctionIds().empty());
    CHECK(doc.pinValue(led, 0) == LogicValue::One); // el cable original quedo intacto

    undoStack.redo(); // un solo paso rehace todo
    CHECK(doc.wireIds().size() == 3);
    CHECK(doc.junctionIds().size() == 1);
    CHECK(doc.pinValue(led2, 0) == LogicValue::One);
}

TEST_CASE("setProperty() rebuilds the simulation for properties marked affectsSimulation even without a pin-count "
          "change",
          "[circuitdocument][property]") {
    // Regresion: "initialValue" de wiring.input no cambia su cantidad de
    // pines, asi que CircuitDocument::setProperty() no disparaba ningun
    // rebuild al cambiarlo - el valor quedaba guardado en la propiedad pero
    // el core::Simulator en ejecucion seguia con el valor viejo hasta el
    // proximo rebuild "de casualidad" o hasta Reiniciar/recargar el
    // proyecto (el bug reportado: "los valores default no se estan
    // aplicando").
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input"); // "Z" por defecto -> resuelve a Zero
    REQUIRE(doc.pinValue(in0, 0) == LogicValue::Zero);

    doc.setProperty(in0, "initialValue", PropertyValue{std::string("1")});
    CHECK(doc.pinValue(in0, 0) == LogicValue::One);
}

TEST_CASE("Turning on asyncPresetClear appends PRE/CLR at the end without disturbing existing wire indices",
          "[circuitdocument][property][memory]") {
    // Regresion dirigida: CircuitDocument::setProperty() solo invalida un
    // cable si su pinIndex >= la nueva cantidad de pines. Si PRE/CLR se
    // insertaran en el MEDIO de la lista de pines (en vez de al final, ver
    // makeDFlipFlopDefinition), la cantidad total crece pero ningun cable se
    // marca invalido, y los que apuntaban a Q/Q' quedarian recableados al
    // pin equivocado en caliente, sin aviso y sin registro de undo.
    CircuitDocument doc;
    const uint32_t dff = doc.addComponent("memory.dFlipFlop");
    const uint32_t clockSource = doc.addComponent("wiring.input");
    const uint32_t ledSink = doc.addComponent("io.led");
    REQUIRE(doc.component(dff)->pins().size() == 4); // D, CLK, Q, Q'

    doc.addWire(PinRef{clockSource, 0}, PinRef{dff, 1}); // -> CLK
    doc.addWire(PinRef{dff, 2}, PinRef{ledSink, 0});      // Q ->
    REQUIRE(doc.wireIds().size() == 2);

    const std::vector<WireConnection> removed =
        doc.setProperty(dff, "asyncPresetClear", PropertyValue{true});

    CHECK(removed.empty());
    CHECK(doc.wireIds().size() == 2);
    REQUIRE(doc.component(dff)->pins().size() == 6); // + PRE, CLR al final
    CHECK(doc.component(dff)->pins()[1].name == "CLK");
    CHECK(doc.component(dff)->pins()[2].name == "Q ");
    CHECK(doc.component(dff)->pins()[4].name == "PRE");
    CHECK(doc.component(dff)->pins()[5].name == "CLR");
}

TEST_CASE("CircuitDocument loads JSON components from DIGITALFORGE_COMPONENTS_DIR",
          "[circuitdocument][json]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    {
        std::ofstream file(QDir(tempDir.path()).filePath(QStringLiteral("nand3.json")).toStdString());
        file << R"({
            "typeId": "custom.nand3",
            "displayName": "NAND-3",
            "category": "Gates",
            "pins": [
                {"name": "A", "dir": "in"}, {"name": "B", "dir": "in"},
                {"name": "C", "dir": "in"}, {"name": "Y", "dir": "out"}
            ],
            "netlist": [{"gate": "Nand", "in": ["A", "B", "C"], "out": "Y"}]
        })";
    }
    qputenv("DIGITALFORGE_COMPONENTS_DIR", tempDir.path().toUtf8());

    CircuitDocument doc;
    CHECK(doc.registry().contains("custom.nand3"));
    CHECK(doc.componentLibraryReport().errors.empty());
    CHECK(doc.componentLibraryReport().loadedTypeIds.size() == 1);

    // Es colocable y simula: NAND(1,1,1) = 0.
    const uint32_t a = doc.addComponent("wiring.input", {{"initialValue", std::string("1")}});
    const uint32_t b = doc.addComponent("wiring.input", {{"initialValue", std::string("1")}});
    const uint32_t c = doc.addComponent("wiring.input", {{"initialValue", std::string("1")}});
    const uint32_t nand3 = doc.addComponent("custom.nand3");
    doc.addWire(PinRef{a, 0}, PinRef{nand3, 0});
    doc.addWire(PinRef{b, 0}, PinRef{nand3, 1});
    doc.addWire(PinRef{c, 0}, PinRef{nand3, 2});
    doc.runUntilStable();
    CHECK(doc.pinValue(nand3, 3) == LogicValue::Zero);

    qunsetenv("DIGITALFORGE_COMPONENTS_DIR");
}
