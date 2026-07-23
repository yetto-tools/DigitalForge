// Tests del cargador de componentes desde JSON (formato por netlist de
// primitivas, ver components/JsonComponentLoader). Se cubren tres cosas: que
// una definicion valida simule con la tabla de verdad correcta, que la
// validacion en tiempo de carga rechace JSON malformado con un error claro, y
// que el cargador de directorio no aborte ante un archivo roto.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "components/ComponentInstance.hpp"
#include "components/ComponentRegistry.hpp"
#include "components/BasicComponentLibrary.hpp"
#include "components/JsonComponentLoader.hpp"
#include "core/Circuit.hpp"
#include "core/Simulator.hpp"

using digitalforge::components::ComponentInstance;
using digitalforge::components::ComponentLoadReport;
using digitalforge::components::ComponentRegistry;
using digitalforge::components::componentDefinitionFromJson;
using digitalforge::components::loadComponentLibraryFromDirectory;
using digitalforge::components::registerBasicComponentLibrary;
using digitalforge::core::Circuit;
using digitalforge::core::LogicValue;
using digitalforge::core::NetId;
using digitalforge::core::Simulator;

namespace {

int pinIndex(const ComponentInstance& instance, const std::string& name) {
    const std::vector<digitalforge::components::PinTemplate>& pins = instance.pins();
    for (std::size_t i = 0; i < pins.size(); ++i) {
        if (pins[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Evalua un componente puramente combinacional cargado de JSON para una
// combinacion concreta de entradas de 1 bit y devuelve el valor de un pin de
// salida. Arma el mismo cableado que el editor: una wiring.input por cada
// entrada y una wiring.output que observa la salida pedida.
LogicValue evaluate(const ComponentRegistry& registry, const std::string& typeId,
                    const std::vector<std::pair<std::string, bool>>& inputs, const std::string& outputPin) {
    Circuit circuit;
    ComponentInstance component = registry.create(typeId, 1000);

    // Un net por cada pin del componente.
    std::vector<NetId> pinNets(component.pins().size());
    for (std::size_t i = 0; i < pinNets.size(); ++i) {
        pinNets[i] = circuit.addNet();
        component.bindPin(static_cast<uint16_t>(i), pinNets[i]);
    }

    // Un wiring.input por cada entrada, cableado al net del pin homonimo.
    std::vector<ComponentInstance> drivers;
    drivers.reserve(inputs.size());
    uint32_t nextId = 0;
    for (const auto& [name, value] : inputs) {
        ComponentInstance driver = registry.create("wiring.input", nextId++);
        const int idx = pinIndex(component, name);
        REQUIRE(idx >= 0);
        driver.bindPin(0, pinNets[static_cast<std::size_t>(idx)]);
        drivers.push_back(std::move(driver));
    }

    const int outIdx = pinIndex(component, outputPin);
    REQUIRE(outIdx >= 0);
    const NetId outNet = pinNets[static_cast<std::size_t>(outIdx)];

    for (ComponentInstance& driver : drivers) {
        driver.buildSimulation(circuit);
    }
    component.buildSimulation(circuit);

    Simulator sim(circuit);
    for (std::size_t i = 0; i < drivers.size(); ++i) {
        sim.setInput(drivers[i].simBinding().gateIndex,
                     inputs[i].second ? LogicValue::One : LogicValue::Zero);
    }
    REQUIRE(sim.runUntilStable());
    return sim.getNetValue(outNet);
}

ComponentRegistry registryWith(const nlohmann::json& definition) {
    ComponentRegistry registry;
    registerBasicComponentLibrary(registry);
    registry.registerDefinition(componentDefinitionFromJson(definition));
    return registry;
}

} // namespace

TEST_CASE("JSON netlist component: AND-3 truth table", "[components][json]") {
    const nlohmann::json json = nlohmann::json::parse(R"({
        "typeId": "custom.and3",
        "displayName": "AND-3",
        "category": "Gates",
        "pins": [
            {"name": "A", "dir": "in"}, {"name": "B", "dir": "in"},
            {"name": "C", "dir": "in"}, {"name": "Y", "dir": "out"}
        ],
        "netlist": [
            {"gate": "And", "in": ["A", "B"], "out": "t"},
            {"gate": "And", "in": ["t", "C"], "out": "Y"}
        ]
    })");
    const ComponentRegistry registry = registryWith(json);

    for (int combo = 0; combo < 8; ++combo) {
        const bool a = (combo & 1) != 0;
        const bool b = (combo & 2) != 0;
        const bool c = (combo & 4) != 0;
        const LogicValue expected = (a && b && c) ? LogicValue::One : LogicValue::Zero;
        CHECK(evaluate(registry, "custom.and3", {{"A", a}, {"B", b}, {"C", c}}, "Y") == expected);
    }
}

TEST_CASE("JSON netlist component: majority-of-3 voter", "[components][json]") {
    const nlohmann::json json = nlohmann::json::parse(R"({
        "typeId": "custom.majority3",
        "category": "Gates",
        "pins": [
            {"name": "A", "dir": "in"}, {"name": "B", "dir": "in"},
            {"name": "C", "dir": "in"}, {"name": "Y", "dir": "out"}
        ],
        "netlist": [
            {"gate": "And", "in": ["A", "B"], "out": "ab"},
            {"gate": "And", "in": ["B", "C"], "out": "bc"},
            {"gate": "And", "in": ["A", "C"], "out": "ac"},
            {"gate": "Or", "in": ["ab", "bc", "ac"], "out": "Y"}
        ]
    })");
    const ComponentRegistry registry = registryWith(json);

    for (int combo = 0; combo < 8; ++combo) {
        const bool a = (combo & 1) != 0;
        const bool b = (combo & 2) != 0;
        const bool c = (combo & 4) != 0;
        const int ones = (a ? 1 : 0) + (b ? 1 : 0) + (c ? 1 : 0);
        const LogicValue expected = (ones >= 2) ? LogicValue::One : LogicValue::Zero;
        CHECK(evaluate(registry, "custom.majority3", {{"A", a}, {"B", b}, {"C", c}}, "Y") == expected);
    }
}

TEST_CASE("JSON netlist component: half adder drives both outputs", "[components][json]") {
    const nlohmann::json json = nlohmann::json::parse(R"({
        "typeId": "custom.halfAdder",
        "category": "Arithmetic",
        "pins": [
            {"name": "A", "dir": "in"}, {"name": "B", "dir": "in"},
            {"name": "S", "dir": "out"}, {"name": "C", "dir": "out"}
        ],
        "netlist": [
            {"gate": "Xor", "in": ["A", "B"], "out": "S"},
            {"gate": "And", "in": ["A", "B"], "out": "C"}
        ]
    })");
    const ComponentRegistry registry = registryWith(json);

    for (int combo = 0; combo < 4; ++combo) {
        const bool a = (combo & 1) != 0;
        const bool b = (combo & 2) != 0;
        const LogicValue sum = ((a != b)) ? LogicValue::One : LogicValue::Zero;
        const LogicValue carry = (a && b) ? LogicValue::One : LogicValue::Zero;
        CHECK(evaluate(registry, "custom.halfAdder", {{"A", a}, {"B", b}}, "S") == sum);
        CHECK(evaluate(registry, "custom.halfAdder", {{"A", a}, {"B", b}}, "C") == carry);
    }
}

TEST_CASE("JSON netlist component: validation rejects malformed definitions", "[components][json]") {
    const auto rejects = [](const char* text) {
        CHECK_THROWS_AS(componentDefinitionFromJson(nlohmann::json::parse(text)), std::invalid_argument);
    };

    SECTION("unknown gate type") {
        rejects(R"({"typeId":"x","pins":[{"name":"A","dir":"in"},{"name":"Y","dir":"out"}],
                    "netlist":[{"gate":"Frobnicate","in":["A"],"out":"Y"}]})");
    }
    SECTION("wrong arity for the gate type") {
        rejects(R"({"typeId":"x","pins":[{"name":"A","dir":"in"},{"name":"Y","dir":"out"}],
                    "netlist":[{"gate":"And","in":["A"],"out":"Y"}]})");
    }
    SECTION("a gate drives an input pin") {
        rejects(R"({"typeId":"x","pins":[{"name":"A","dir":"in"},{"name":"Y","dir":"out"}],
                    "netlist":[{"gate":"Not","in":["Y"],"out":"A"}]})");
    }
    SECTION("output pin left without a driver") {
        rejects(R"({"typeId":"x","pins":[{"name":"A","dir":"in"},{"name":"Y","dir":"out"}],
                    "netlist":[{"gate":"Buffer","in":["A"],"out":"dead"}]})");
    }
    SECTION("internal net read but never driven") {
        rejects(R"({"typeId":"x","pins":[{"name":"A","dir":"in"},{"name":"Y","dir":"out"}],
                    "netlist":[{"gate":"And","in":["A","ghost"],"out":"Y"}]})");
    }
    SECTION("duplicate pin name") {
        rejects(R"({"typeId":"x","pins":[{"name":"A","dir":"in"},{"name":"A","dir":"out"}],
                    "netlist":[{"gate":"Buffer","in":["A"],"out":"A"}]})");
    }
    SECTION("InputPin is not a valid netlist primitive") {
        rejects(R"({"typeId":"x","pins":[{"name":"Y","dir":"out"}],
                    "netlist":[{"gate":"InputPin","in":[],"out":"Y"}]})");
    }
}

TEST_CASE("JSON directory loader skips bad files and keeps the good ones", "[components][json]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "digitalforge_json_loader_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    {
        std::ofstream good(dir / "good.json");
        good << R"({"typeId":"custom.buf","pins":[{"name":"A","dir":"in"},{"name":"Y","dir":"out"}],
                    "netlist":[{"gate":"Buffer","in":["A"],"out":"Y"}]})";
    }
    {
        std::ofstream bad(dir / "bad.json");
        bad << R"({"typeId":"custom.broken","pins":[{"name":"A","dir":"in"},{"name":"Y","dir":"out"}],
                   "netlist":[{"gate":"Nope","in":["A"],"out":"Y"}]})";
    }
    {
        std::ofstream notJson(dir / "notjson.json");
        notJson << "esto no es json {";
    }

    ComponentRegistry registry;
    registerBasicComponentLibrary(registry);
    const ComponentLoadReport report = loadComponentLibraryFromDirectory(registry, dir);

    CHECK(registry.contains("custom.buf"));
    CHECK(report.loadedTypeIds.size() == 1);
    CHECK(report.errors.size() == 2); // bad.json (compuerta desconocida) + notjson.json (parseo)
    CHECK_FALSE(report.ok());

    fs::remove_all(dir, ec);
}

TEST_CASE("JSON directory loader treats a missing directory as empty", "[components][json]") {
    ComponentRegistry registry;
    registerBasicComponentLibrary(registry);
    const ComponentLoadReport report =
        loadComponentLibraryFromDirectory(registry, std::filesystem::path("no/such/dir/anywhere"));
    CHECK(report.loadedTypeIds.empty());
    CHECK(report.ok());
}
