// Ejercita el modelo de puntos de union (Junction/WireEndpoint) de
// CircuitDocument y los comandos de undo que lo manipulan (SplitWireCommand
// en particular). No necesita su propio main(): comparte el
// CATCH_CONFIG_RUNNER definido en test_project_serializer.cpp (ver
// tests_qt/CMakeLists.txt).

#include <QPointF>
#include <QUndoStack>
#include <catch2/catch_test_macros.hpp>

#include "editor/CircuitDocument.hpp"
#include "editor/UndoCommands.hpp"

using digitalforge::core::LogicValue;
using digitalforge::components::PropertyValue;
using digitalforge::editor::AddWireCommand;
using digitalforge::editor::CircuitDocument;
using digitalforge::editor::MoveJunctionCommand;
using digitalforge::editor::PinRef;
using digitalforge::editor::SetZOrderCommand;
using digitalforge::editor::SplitWireCommand;
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
