// Verifica que un proyecto guarde identidad y huellas por instancia, y que al
// reabrirlo se detecte -en vez de aceptar en silencio- que la definicion
// instalada ya no es la que se uso. Ver components/ComponentFingerprints.hpp.

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>
#include <string>

#include "components/ComponentFingerprints.hpp"
#include "editor/CircuitDocument.hpp"
#include "formats/ProjectSerializer.hpp"

using digitalforge::components::CompatibilityVerdict;
using digitalforge::editor::CircuitDocument;
using digitalforge::editor::ComponentPlacement;
namespace formats = digitalforge::formats;

namespace {

// Proyecto minimo con un unico componente colocado.
nlohmann::json makeProjectWithOneGate() {
    CircuitDocument document;
    document.addComponent("gates.and", {}, ComponentPlacement{QPointF(40.0, 40.0), 0});
    return formats::serializeProject(document);
}

// La entrada de compatibilidad del primer componente del proyecto.
nlohmann::json& compatibilityOf(nlohmann::json& project) {
    return project.at("components").at(0).at("compatibility");
}

} // namespace

TEST_CASE("Saving a project records identity and fingerprints per instance", "[formats][compatibility]") {
    const nlohmann::json project = makeProjectWithOneGate();
    const nlohmann::json& component = project.at("components").at(0);

    REQUIRE(component.contains("definitionVersion"));
    REQUIRE(component.contains("compatibility"));
    const nlohmann::json& compatibility = component.at("compatibility");

    // Los cuatro campos que la especificacion exige, mas los dos que hacen
    // falta para poder clasificar un cambio visual o de encapsulado.
    for (const char* key : {"publicInterfaceHash", "pinInterfaceHash", "propertyInterfaceHash", "simulationHash",
                             "appearanceHash", "packageHash"}) {
        REQUIRE(compatibility.contains(key));
        // Un SHA-256 en hexadecimal: 64 caracteres.
        CHECK(compatibility.at(key).get<std::string>().size() == 64);
    }

    // El proyecto guarda identidad y huellas, NO la definicion compilada.
    CHECK_FALSE(component.contains("pins"));
    CHECK_FALSE(component.contains("netlist"));
}

TEST_CASE("Reopening an unchanged project reports no incompatibilities", "[formats][compatibility]") {
    const nlohmann::json project = makeProjectWithOneGate();

    CircuitDocument loaded;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(loaded, project, &report);

    CHECK(report.hasStoredMetadata);
    CHECK(report.empty());
    CHECK(loaded.componentIds().size() == 1);
}

TEST_CASE("A changed pin contract is reported as requiring migration", "[formats][compatibility]") {
    nlohmann::json project = makeProjectWithOneGate();
    // Simula que la definicion instalada cambio sus pines desde que se guardo
    // el proyecto: la huella almacenada deja de coincidir con la actual.
    compatibilityOf(project)["pinInterfaceHash"] = std::string(64, 'a');

    CircuitDocument loaded;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(loaded, project, &report);

    REQUIRE(report.issues.size() == 1);
    CHECK(report.issues[0].typeId == "gates.and");
    CHECK(report.issues[0].verdict == CompatibilityVerdict::RequiresMigration);
    // La carga no se aborta: el componente sigue estando, para que el usuario
    // pueda ver que cambio en vez de perder el trabajo.
    CHECK(loaded.componentIds().size() == 1);
}

TEST_CASE("A changed behaviour with the same public interface only needs a recompile",
          "[formats][compatibility]") {
    nlohmann::json project = makeProjectWithOneGate();
    compatibilityOf(project)["simulationHash"] = std::string(64, 'b');

    CircuitDocument loaded;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(loaded, project, &report);

    REQUIRE(report.issues.size() == 1);
    CHECK(report.issues[0].verdict == CompatibilityVerdict::RequiresRecompile);
}

TEST_CASE("A redrawn symbol is classified as a visual change only", "[formats][compatibility]") {
    nlohmann::json project = makeProjectWithOneGate();
    compatibilityOf(project)["appearanceHash"] = std::string(64, 'c');

    CircuitDocument loaded;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(loaded, project, &report);

    REQUIRE(report.issues.size() == 1);
    CHECK(report.issues[0].verdict == CompatibilityVerdict::AppearanceOnly);
}

TEST_CASE("Projects saved before fingerprints existed load without false alarms",
          "[formats][compatibility]") {
    nlohmann::json project = makeProjectWithOneGate();
    project.at("components").at(0).erase("compatibility");
    project.at("components").at(0).erase("definitionVersion");

    CircuitDocument loaded;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(loaded, project, &report);

    // Sin metadata guardada no hay nada contra que comparar - y eso no es lo
    // mismo que "todo coincide", por eso el reporte lo distingue.
    CHECK_FALSE(report.hasStoredMetadata);
    CHECK(report.empty());
    CHECK(loaded.componentIds().size() == 1);
}

TEST_CASE("A corrupted hash is treated as absent, not as a difference", "[formats][compatibility]") {
    nlohmann::json project = makeProjectWithOneGate();
    compatibilityOf(project)["pinInterfaceHash"] = "no-es-un-hash-valido";

    CircuitDocument loaded;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(loaded, project, &report);

    // Un hash ilegible no puede compararse; inventar una incompatibilidad a
    // partir de el seria una falsa alarma.
    CHECK(report.empty());
}

TEST_CASE("Wires are saved with the pin key, not only the index", "[formats][compatibility]") {
    CircuitDocument document;
    const uint32_t in = document.addComponent("wiring.input", {}, ComponentPlacement{QPointF(0, 0), 0});
    const uint32_t gate = document.addComponent("gates.and", {}, ComponentPlacement{QPointF(100, 0), 0});
    document.addWire(digitalforge::editor::PinRef{in, 0}, digitalforge::editor::PinRef{gate, 1});

    const nlohmann::json project = formats::serializeProject(document);
    const nlohmann::json& endpointB = project.at("wires").at(0).at("b");
    REQUIRE(endpointB.contains("pinKey"));
    CHECK(endpointB.at("pinKey").get<std::string>() == "In1"); // segunda entrada de la AND
    // El indice se sigue guardando para que el archivo abra en versiones
    // anteriores, pero ya no es lo que manda al reconectar.
    CHECK(endpointB.at("pinIndex").get<unsigned>() == 1);
}

TEST_CASE("A reordered pin list reconnects by key instead of by index", "[formats][compatibility]") {
    // Escenario que la especificacion prohibe resolver por indice: los pines
    // del componente cambiaron de orden entre guardar y abrir.
    CircuitDocument document;
    const uint32_t in = document.addComponent("wiring.input", {}, ComponentPlacement{QPointF(0, 0), 0});
    const uint32_t gate = document.addComponent("gates.and", {}, ComponentPlacement{QPointF(100, 0), 0});
    document.addWire(digitalforge::editor::PinRef{in, 0}, digitalforge::editor::PinRef{gate, 1});

    nlohmann::json project = formats::serializeProject(document);
    // El archivo dice indice 0 pero clave "in1": manda la clave, asi que el
    // cable tiene que terminar en el pin 1.
    project.at("wires").at(0).at("b").at("pinIndex") = 0;

    CircuitDocument loaded;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(loaded, project, &report);

    REQUIRE(loaded.wireIds().size() == 1);
    const auto* wire = loaded.wire(loaded.wireIds().front());
    REQUIRE(wire != nullptr);
    CHECK(wire->b.pinIndex == 1);
    CHECK(report.unresolvedWires.empty());
}

TEST_CASE("A pin key that no longer exists leaves the wire unrestored and reported",
          "[formats][compatibility]") {
    CircuitDocument document;
    const uint32_t in = document.addComponent("wiring.input", {}, ComponentPlacement{QPointF(0, 0), 0});
    const uint32_t gate = document.addComponent("gates.and", {}, ComponentPlacement{QPointF(100, 0), 0});
    document.addWire(digitalforge::editor::PinRef{in, 0}, digitalforge::editor::PinRef{gate, 1});

    nlohmann::json project = formats::serializeProject(document);
    project.at("wires").at(0).at("b").at("pinKey") = "pin_que_ya_no_existe";

    CircuitDocument loaded;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(loaded, project, &report);

    // El cable NO se reconecta por indice: se pierde y se informa.
    CHECK(loaded.wireIds().empty());
    REQUIRE(report.unresolvedWires.size() == 1);
    CHECK(report.unresolvedWires[0].pinKey == "pin_que_ya_no_existe");
    CHECK(report.unresolvedWires[0].componentId == gate);
    CHECK_FALSE(report.empty());
    // Los componentes siguen estando: no se pierde el trabajo del usuario.
    CHECK(loaded.componentIds().size() == 2);
}

TEST_CASE("Wires saved before pin keys existed still load by index", "[formats][compatibility]") {
    CircuitDocument document;
    const uint32_t in = document.addComponent("wiring.input", {}, ComponentPlacement{QPointF(0, 0), 0});
    const uint32_t gate = document.addComponent("gates.and", {}, ComponentPlacement{QPointF(100, 0), 0});
    document.addWire(digitalforge::editor::PinRef{in, 0}, digitalforge::editor::PinRef{gate, 1});

    nlohmann::json project = formats::serializeProject(document);
    project.at("wires").at(0).at("a").erase("pinKey");
    project.at("wires").at(0).at("b").erase("pinKey");

    CircuitDocument loaded;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(loaded, project, &report);

    // Sin clave guardada el indice es lo unico que ese archivo registro.
    REQUIRE(loaded.wireIds().size() == 1);
    CHECK(report.unresolvedWires.empty());
}

TEST_CASE("Compatibility checking is optional and never changes what gets loaded",
          "[formats][compatibility]") {
    nlohmann::json project = makeProjectWithOneGate();
    compatibilityOf(project)["publicInterfaceHash"] = std::string(64, 'd');

    CircuitDocument withReport;
    formats::ProjectCompatibilityReport report;
    formats::loadProject(withReport, project, &report);

    CircuitDocument withoutReport;
    formats::loadProject(withoutReport, project); // sin reporte: mismo resultado

    CHECK(report.issues.size() == 1);
    CHECK(withReport.componentIds().size() == withoutReport.componentIds().size());
}
