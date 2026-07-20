// Ejercita Fase 4 (subcircuitos): un documento del Project usado como
// componente dentro de otro. No necesita su propio main(): comparte el
// CATCH_CONFIG_RUNNER definido en test_project_serializer.cpp (ver
// tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <stdexcept>

#include "components/ComponentInstance.hpp"
#include "core/LogicValue.hpp"
#include "core/Pin.hpp"
#include "editor/CircuitDocument.hpp"
#include "editor/Project.hpp"

using digitalforge::components::ComponentInstance;
using digitalforge::components::PropertyValue;
using digitalforge::core::LogicValue;
using digitalforge::core::PinDirection;
using digitalforge::editor::CircuitDocument;
using digitalforge::editor::ComponentPlacement;
using digitalforge::editor::PinRef;
using digitalforge::editor::Project;

namespace {

// Documento "Puerta": A, B (wiring.input) -> AND -> Y (wiring.output).
// Posiciones elegidas para que boundaryPins() los ordene A, B, Y (Y y luego
// X: A en y=0, B en y=100, Y en y=200 - claramente por debajo de ambas
// entradas).
void buildPuertaDocument(CircuitDocument& puerta) {
    const uint32_t a = puerta.addComponent("wiring.input", {{"label", PropertyValue{std::string("A")}}},
                                            ComponentPlacement{QPointF(0, 0), 0});
    const uint32_t b = puerta.addComponent("wiring.input", {{"label", PropertyValue{std::string("B")}}},
                                            ComponentPlacement{QPointF(0, 100), 0});
    const uint32_t andGate = puerta.addComponent("gates.and", {}, ComponentPlacement{QPointF(150, 50), 0});
    const uint32_t y = puerta.addComponent("wiring.output", {{"label", PropertyValue{std::string("Y")}}},
                                            ComponentPlacement{QPointF(300, 150), 0});
    puerta.addWire(PinRef{a, 0}, PinRef{andGate, 0});
    puerta.addWire(PinRef{b, 0}, PinRef{andGate, 1});
    puerta.addWire(PinRef{andGate, 2}, PinRef{y, 0});
}

} // namespace

TEST_CASE("A subcircuit has no pins until the project is saved and targetPath resolves",
          "[project][subcircuit]") {
    Project project;
    const uint32_t mainId = project.documentIds().front();
    CircuitDocument& main = *project.document(mainId);

    const uint32_t sub = main.addComponent("structural.subcircuit");
    CHECK(main.component(sub)->pins().empty());

    // Todavia sin guardar: ningun documento hermano es resoluble por ruta
    // todavia, aunque el nombre coincida.
    project.addDocument("Puerta");
    CHECK_THROWS_AS(main.setProperty(sub, "targetPath", PropertyValue{std::string("Puerta.dfc")}),
                    std::invalid_argument);
}

TEST_CASE("A subcircuit exposes its target's boundary pins ordered by canvas position and flattens its "
          "simulation end to end",
          "[project][subcircuit]") {
    constexpr const char* kProjectPath = "test_subcircuit_project.dfproj";
    Project project;

    const uint32_t mainId = project.documentIds().front();
    project.renameDocument(mainId, "Main");
    const uint32_t puertaId = project.addDocument("Puerta");
    buildPuertaDocument(*project.document(puertaId));
    project.setActiveDocument(mainId);

    // Necesita guardarse una vez para que "Puerta.dfc" sea una ruta
    // relativa resoluble (ver restriccion de v1 en el plan de Fase 4).
    project.saveToFile(kProjectPath);

    CircuitDocument& main = *project.document(mainId);
    const uint32_t sub = main.addComponent("structural.subcircuit");
    const auto removedByTarget = main.setProperty(sub, "targetPath", PropertyValue{std::string("Puerta.dfc")});
    CHECK(removedByTarget.empty());

    const ComponentInstance* subInstance = main.component(sub);
    REQUIRE(subInstance->pins().size() == 3);
    CHECK(subInstance->pins()[0].name == "A");
    CHECK(subInstance->pins()[0].direction == PinDirection::Input);
    CHECK(subInstance->pins()[1].name == "B");
    CHECK(subInstance->pins()[1].direction == PinDirection::Input);
    CHECK(subInstance->pins()[2].name == "Y");
    CHECK(subInstance->pins()[2].direction == PinDirection::Output);

    const uint32_t inA = main.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t inB = main.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t led = main.addComponent("io.led");
    main.addWire(PinRef{inA, 0}, PinRef{sub, 0});
    main.addWire(PinRef{inB, 0}, PinRef{sub, 1});
    main.addWire(PinRef{sub, 2}, PinRef{led, 0});

    CHECK(main.pinValue(led, 0) == LogicValue::One); // 1 AND 1, aplanado a traves del subcircuito

    main.setLiveSimulation(true);
    main.setInputValue(inA, LogicValue::Zero);
    CHECK(main.pinValue(led, 0) == LogicValue::Zero);
    main.setInputValue(inA, LogicValue::One);
    CHECK(main.pinValue(led, 0) == LogicValue::One);

    std::remove(kProjectPath);
    std::remove("Main.dfc");
    std::remove("Puerta.dfc");
}

TEST_CASE("Setting targetPath to itself or to a document that already contains a subcircuit is rejected",
          "[project][subcircuit][validation]") {
    constexpr const char* kProjectPath = "test_subcircuit_reject_project.dfproj";
    Project project;

    const uint32_t mainId = project.documentIds().front();
    project.renameDocument(mainId, "Main");
    const uint32_t puertaId = project.addDocument("Puerta");
    buildPuertaDocument(*project.document(puertaId));

    const uint32_t anidadoId = project.addDocument("Anidado");
    project.setActiveDocument(mainId);

    project.saveToFile(kProjectPath);

    CircuitDocument& main = *project.document(mainId);
    const uint32_t sub = main.addComponent("structural.subcircuit");

    // Auto-referencia.
    CHECK_THROWS_AS(main.setProperty(sub, "targetPath", PropertyValue{std::string("Main.dfc")}),
                    std::invalid_argument);

    // "Anidado" ya contiene, a su vez, un subcircuito (que apunta a
    // "Puerta") - no se admite mas de un nivel.
    CircuitDocument& anidado = *project.document(anidadoId);
    const uint32_t nestedSub = anidado.addComponent("structural.subcircuit");
    REQUIRE_NOTHROW(anidado.setProperty(nestedSub, "targetPath", PropertyValue{std::string("Puerta.dfc")}));

    CHECK_THROWS_AS(main.setProperty(sub, "targetPath", PropertyValue{std::string("Anidado.dfc")}),
                    std::invalid_argument);

    // Pero apuntar directamente a "Puerta" (una hoja, sin subcircuitos)
    // sigue siendo valido.
    REQUIRE_NOTHROW(main.setProperty(sub, "targetPath", PropertyValue{std::string("Puerta.dfc")}));

    std::remove(kProjectPath);
    std::remove("Main.dfc");
    std::remove("Puerta.dfc");
    std::remove("Anidado.dfc");
}

TEST_CASE("A project with a wired subcircuit round-trips through saveToFile/loadFromFile", "[project][subcircuit][roundtrip]") {
    constexpr const char* kProjectPath = "test_subcircuit_roundtrip.dfproj";
    {
        Project project;
        const uint32_t mainId = project.documentIds().front();
        project.renameDocument(mainId, "Main");
        const uint32_t puertaId = project.addDocument("Puerta");
        buildPuertaDocument(*project.document(puertaId));
        project.setActiveDocument(mainId);
        project.saveToFile(kProjectPath); // primer guardado: fija la ruta relativa de "Puerta"

        CircuitDocument& main = *project.document(mainId);
        const uint32_t sub = main.addComponent("structural.subcircuit");
        main.setProperty(sub, "targetPath", PropertyValue{std::string("Puerta.dfc")});
        const uint32_t inA = main.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
        const uint32_t inB = main.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
        const uint32_t led = main.addComponent("io.led");
        main.addWire(PinRef{inA, 0}, PinRef{sub, 0});
        main.addWire(PinRef{inB, 0}, PinRef{sub, 1});
        main.addWire(PinRef{sub, 2}, PinRef{led, 0});
        REQUIRE(main.pinValue(led, 0) == LogicValue::One);

        project.saveToFile(kProjectPath); // segundo guardado: ya con el subcircuito cableado
    }

    Project loaded;
    loaded.loadFromFile(kProjectPath);

    uint32_t mainId = 0;
    bool foundMain = false;
    for (const uint32_t id : loaded.documentIds()) {
        if (loaded.documentName(id) == QString("Main")) {
            mainId = id;
            foundMain = true;
        }
    }
    REQUIRE(foundMain);

    const CircuitDocument* main = loaded.document(mainId);
    REQUIRE(main->componentIds().size() == 4); // subcircuito + 2 entradas + LED

    // El subcircuito debe haber recuperado sus 3 pines (A, B, Y) al cargar,
    // sin importar el orden del manifiesto (ver Project::loadFromFile: dos
    // pasadas, todos los documentos existen antes de cargar el contenido de
    // cualquiera).
    bool foundLedOn = false;
    for (const uint32_t id : main->componentIds()) {
        if (main->component(id)->typeId() == "io.led") {
            foundLedOn = (main->pinValue(id, 0) == LogicValue::One);
        }
    }
    CHECK(foundLedOn);

    std::remove(kProjectPath);
    std::remove("Main.dfc");
    std::remove("Puerta.dfc");
}
