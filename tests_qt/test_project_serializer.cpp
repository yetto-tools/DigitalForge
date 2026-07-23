// Ejercita CircuitDocument + formats::ProjectSerializer de extremo a
// extremo. Estos dos solo dependen de QtCore (sin QWidget/QGraphicsItem),
// asi que el ciclo completo de guardado/carga se verifica de forma
// independiente de la aplicacion de escritorio QtWidgets.

#define CATCH_CONFIG_RUNNER
#include <QCoreApplication>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <vector>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "components/BasicComponentLibrary.hpp"
#include "editor/CircuitDocument.hpp"
#include "formats/ProjectSerializer.hpp"

using digitalforge::core::LogicValue;
using digitalforge::components::PropertyValue;
using digitalforge::editor::CircuitDocument;
using digitalforge::editor::ComponentPlacement;
using digitalforge::editor::PinRef;
namespace formats = digitalforge::formats;

namespace {
constexpr const char* kRoundTripPath = "test_project_roundtrip.dfproj";
} // namespace

TEST_CASE("A wired circuit round-trips through save and load", "[formats][roundtrip]") {
    CircuitDocument original;

    const uint32_t in0 = original.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}},
                                                ComponentPlacement{QPointF(0, 0), 0});
    const uint32_t in1 = original.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}},
                                                ComponentPlacement{QPointF(0, 100), 0});
    const uint32_t andGate = original.addComponent("gates.and", {}, ComponentPlacement{QPointF(150, 50), 90, 3});
    const uint32_t led = original.addComponent("io.led", {}, ComponentPlacement{QPointF(300, 50), 0});

    original.addWire(PinRef{in0, 0}, PinRef{andGate, 0});
    original.addWire(PinRef{in1, 0}, PinRef{andGate, 1});
    original.addWire(PinRef{andGate, 2}, PinRef{led, 0});

    REQUIRE(original.pinValue(led, 0) == LogicValue::One); // 1 AND 1, encendido antes de guardar

    formats::saveProjectToFile(original, kRoundTripPath);

    CircuitDocument loaded;
    formats::loadProjectFromFile(loaded, kRoundTripPath);
    std::remove(kRoundTripPath);

    const auto ids = loaded.componentIds();
    CHECK(ids.size() == 4);
    CHECK(loaded.wireIds().size() == 3);

    REQUIRE(loaded.component(in0) != nullptr);
    CHECK(loaded.component(in0)->typeId() == "wiring.input");
    CHECK(std::get<std::string>(loaded.component(in0)->property("initialValue")) == "1");

    const ComponentPlacement andPlacement = loaded.componentPlacement(andGate);
    CHECK(andPlacement.position == QPointF(150, 50));
    CHECK(andPlacement.rotationDegrees == 90);
    CHECK(andPlacement.zOrder == 3);

    // La simulacion se reconstruyo a partir de la topologia cargada y se
    // reinicializo con el initialValue de cada wiring.input - el LED ya
    // deberia estar encendido sin necesidad de ninguna interaccion adicional.
    CHECK(loaded.pinValue(led, 0) == LogicValue::One);
}

TEST_CASE("loadProject rejects an unsupported schema version", "[formats][validation]") {
    CircuitDocument document;
    nlohmann::json json;
    json["schemaVersion"] = 999;
    json["components"] = nlohmann::json::array();
    json["wires"] = nlohmann::json::array();

    CHECK_THROWS_AS(formats::loadProject(document, json), std::invalid_argument);
}

TEST_CASE("loadProject rejects an unknown component typeId", "[formats][validation]") {
    CircuitDocument document;
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["components"] = nlohmann::json::array(
        {{{"typeId", "bogus.thing"}, {"instanceId", 0}, {"properties", nlohmann::json::object()},
          {"position", {{"x", 0.0}, {"y", 0.0}}},
          {"rotation", 0}}});
    json["wires"] = nlohmann::json::array();

    CHECK_THROWS_AS(formats::loadProject(document, json), std::invalid_argument);
}

TEST_CASE("loadProjectFromFile throws for a missing file", "[formats][validation]") {
    CircuitDocument document;
    CHECK_THROWS_AS(formats::loadProjectFromFile(document, "this_file_does_not_exist.dfproj"), std::runtime_error);
}

TEST_CASE("Saving an empty project and reloading it yields an empty document", "[formats][roundtrip]") {
    CircuitDocument original;
    formats::saveProjectToFile(original, kRoundTripPath);

    CircuitDocument loaded;
    formats::loadProjectFromFile(loaded, kRoundTripPath);
    std::remove(kRoundTripPath);

    CHECK(loaded.componentIds().empty());
    CHECK(loaded.wireIds().empty());
}

TEST_CASE("A multi-pin chip keeps every wire connected across save and load", "[formats][roundtrip]") {
    // Reproduce la forma de un proyecto real: tres entradas alimentando un
    // decodificador BCD de 11 pines, cuyas siete salidas van a un display.
    // Un componente con muchos pines es donde antes se notaria cualquier
    // desfase de indices al reconectar los cables.
    CircuitDocument original;
    const uint32_t driver = original.addComponent("ic74ls.bcdDriver",
                                                   {{"variant", PropertyValue{std::string("7448")}}},
                                                   ComponentPlacement{QPointF(0, 0), 0});
    const uint32_t display = original.addComponent("io.seven_segment", {}, ComponentPlacement{QPointF(200, 0), 0});

    std::vector<uint32_t> inputs;
    for (int bit = 0; bit < 4; ++bit) {
        const uint32_t in = original.addComponent(
            "wiring.input", {{"initialValue", PropertyValue{std::string(bit == 0 ? "1" : "0")}}},
            ComponentPlacement{QPointF(-200, bit * 32), 0});
        inputs.push_back(in);
        original.addWire(PinRef{in, 0}, PinRef{driver, static_cast<uint16_t>(bit)});
    }
    // Salidas a..g del driver (pines 4..10) hacia los segmentos del display.
    for (uint16_t segment = 0; segment < 7; ++segment) {
        original.addWire(PinRef{driver, static_cast<uint16_t>(4 + segment)}, PinRef{display, segment});
    }
    REQUIRE(original.wireIds().size() == 11);

    // Con BCD = 1 (bit0 en 1) el 7448 enciende solo los segmentos b y c.
    const LogicValue segmentB = original.pinValue(display, 1);
    const LogicValue segmentC = original.pinValue(display, 2);
    const LogicValue segmentA = original.pinValue(display, 0);
    REQUIRE(segmentB == LogicValue::One);
    REQUIRE(segmentC == LogicValue::One);
    REQUIRE(segmentA == LogicValue::Zero);

    formats::saveProjectToFile(original, kRoundTripPath);
    CircuitDocument loaded;
    formats::loadProjectFromFile(loaded, kRoundTripPath);
    std::remove(kRoundTripPath);

    CHECK(loaded.componentIds().size() == 6);
    CHECK(loaded.wireIds().size() == 11);

    // Lo que importa no es que los cables existan, sino que sigan uniendo los
    // mismos pines: el display tiene que mostrar exactamente lo mismo.
    CHECK(loaded.pinValue(display, 0) == segmentA);
    CHECK(loaded.pinValue(display, 1) == segmentB);
    CHECK(loaded.pinValue(display, 2) == segmentC);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
