// Ejercita editor::Project (multiples CircuitDocument bajo un mismo
// proyecto) + formats::ProjectManifestSerializer de punta a punta. No
// necesita su propio main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include <QUndoStack>

#include "editor/CircuitDocument.hpp"
#include "editor/ExcitationTableDocument.hpp"
#include "editor/FlipFlopExcitation.hpp"
#include "editor/KarnaughDocument.hpp"
#include "editor/Project.hpp"
#include "editor/UndoCommands.hpp"
#include "formats/ProjectSerializer.hpp"

using digitalforge::components::PropertyValue;
using digitalforge::editor::CircuitDocument;
using digitalforge::editor::PinRef;
using digitalforge::editor::Project;
namespace formats = digitalforge::formats;

namespace {

bool fileExists(const char* path) {
    return std::ifstream(path).good();
}

void wireAndGateWithLed(CircuitDocument& doc) {
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t in1 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    const uint32_t andGate = doc.addComponent("gates.and");
    const uint32_t led = doc.addComponent("io.led");
    doc.addWire(PinRef{in0, 0}, PinRef{andGate, 0});
    doc.addWire(PinRef{in1, 0}, PinRef{andGate, 1});
    doc.addWire(PinRef{andGate, 2}, PinRef{led, 0});
}

} // namespace

TEST_CASE("A fresh Project starts with exactly one anonymous, clean document", "[project]") {
    Project project;
    const auto ids = project.documentIds();
    REQUIRE(ids.size() == 1);
    CHECK(project.activeDocumentId() == ids.front());
    CHECK(project.filePath().isEmpty());
    CHECK_FALSE(project.hasUnsavedChanges());
}

TEST_CASE("addDocument adds and activates a new empty document", "[project]") {
    Project project;
    const uint32_t firstId = project.documentIds().front();
    const uint32_t secondId = project.addDocument("Segundo");

    CHECK(project.documentIds().size() == 2);
    CHECK(project.activeDocumentId() == secondId);
    CHECK(project.documentName(secondId) == QString("Segundo"));
    CHECK(project.document(secondId)->componentIds().empty());
    CHECK(project.hasUnsavedChanges());

    project.setActiveDocument(firstId);
    CHECK(project.activeDocumentId() == firstId);
}

TEST_CASE("addDocument rejects a name already used by another document, case-insensitively", "[project][uniqueness]") {
    Project project;
    project.renameDocument(project.documentIds().front(), "Principal");
    CHECK_THROWS_AS(project.addDocument("Principal"), std::invalid_argument);
    CHECK_THROWS_AS(project.addDocument("PRINCIPAL"), std::invalid_argument); // sin distinguir mayusculas/minusculas
    CHECK_NOTHROW(project.addDocument("Secundario")); // un nombre distinto sigue funcionando
    CHECK(project.documentIds().size() == 2);
}

TEST_CASE("suggestUniqueDocumentName avoids colliding with the project's default first document",
          "[project][uniqueness]") {
    // Regresion: un nuevo proyecto ya arranca con un documento llamado
    // "Documento" (Project::newProject()) - sugerir ese mismo nombre tal
    // cual para el siguiente documento chocaria de entrada contra
    // addDocument(), dando la falsa impresion de que el proyecto no
    // admitia mas de un documento (el bug reportado).
    Project project;
    const QString suggested = project.suggestUniqueDocumentName("Documento");
    CHECK(suggested != QString("Documento"));
    CHECK_NOTHROW(project.addDocument(suggested));
    CHECK(project.documentIds().size() == 2);

    // Con "Documento"/"Documento2" ya ocupados, sugiere "Documento3".
    const QString nextSuggested = project.suggestUniqueDocumentName("Documento");
    CHECK_NOTHROW(project.addDocument(nextSuggested));
    CHECK(project.documentIds().size() == 3);

    // Una base sin colision se devuelve tal cual.
    CHECK(project.suggestUniqueDocumentName("SinColision") == QString("SinColision"));
}

TEST_CASE("removeDocument refuses to remove the last remaining document", "[project]") {
    Project project;
    const uint32_t onlyId = project.documentIds().front();
    CHECK_THROWS_AS(project.removeDocument(onlyId), std::invalid_argument);
}

TEST_CASE("removeDocument reassigns the active document when the active one is removed", "[project]") {
    Project project;
    const uint32_t firstId = project.documentIds().front();
    const uint32_t secondId = project.addDocument("Segundo");
    REQUIRE(project.activeDocumentId() == secondId);

    project.removeDocument(secondId);
    CHECK(project.documentIds().size() == 1);
    CHECK(project.activeDocumentId() == firstId);
}

TEST_CASE("renameDocument updates the display name", "[project]") {
    Project project;
    const uint32_t id = project.documentIds().front();
    project.renameDocument(id, "Principal");
    CHECK(project.documentName(id) == QString("Principal"));
}

TEST_CASE("renameDocument rejects a name taken by another document, but allows renaming to its own current name",
          "[project][uniqueness]") {
    Project project;
    const uint32_t firstId = project.documentIds().front();
    const uint32_t secondId = project.addDocument("Segundo");

    CHECK_THROWS_AS(project.renameDocument(secondId, "Documento"), std::invalid_argument); // nombre del primero
    CHECK_THROWS_AS(project.renameDocument(secondId, "documento"), std::invalid_argument); // idem, sin distinguir caso
    CHECK_NOTHROW(project.renameDocument(firstId, "Documento")); // renombrar a su propio nombre actual no es colision
    CHECK_NOTHROW(project.renameDocument(secondId, "Otro"));
    CHECK(project.documentName(secondId) == QString("Otro"));
}

TEST_CASE("importDocument rejects a name already used by another document in the project", "[project][uniqueness]") {
    constexpr const char* kExternalPath = "test_project_import_collision.dfc";
    CircuitDocument external;
    formats::saveProjectToFile(external, kExternalPath); // nombre base: "test_project_import_collision"

    Project project;
    project.renameDocument(project.documentIds().front(), "test_project_import_collision");
    CHECK_THROWS_AS(project.importDocument(kExternalPath), std::invalid_argument);
    CHECK(project.documentIds().size() == 1); // no se agrego nada

    std::remove(kExternalPath);
}

TEST_CASE("exportDocument writes a standalone .dfc a fresh CircuitDocument can load", "[project][export]") {
    constexpr const char* kExportPath = "test_project_export.dfc";
    Project project;
    wireAndGateWithLed(*project.activeDocument());

    project.exportDocument(project.activeDocumentId(), kExportPath);

    CircuitDocument reloaded;
    formats::loadProjectFromFile(reloaded, kExportPath);
    std::remove(kExportPath);

    REQUIRE(reloaded.componentIds().size() == 4);
}

TEST_CASE("saveToFile skips rewriting the .dfproj manifest when the document list hasn't changed",
          "[project][save]") {
    constexpr const char* kProjectPath = "test_project_manifest_skip.dfproj";
    Project project;
    project.saveToFile(kProjectPath); // primer guardado: manifiesto nuevo, escribe todo
    REQUIRE(fileExists(kProjectPath));

    // Sin agregar/quitar/renombrar documentos desde el guardado anterior:
    // manifestDirty_ sigue en false, asi que un segundo guardado en la
    // misma ruta no deberia tocar el .dfproj para nada - se lo borra a
    // mano para confirmarlo (si el codigo lo reescribiera, reaparaceria).
    std::remove(kProjectPath);
    REQUIRE_FALSE(fileExists(kProjectPath));

    project.saveToFile(kProjectPath);
    CHECK_FALSE(fileExists(kProjectPath)); // sigue sin reaparecer: no se reescribio

    // Agregar un documento si ensucia el manifiesto - el siguiente guardado
    // si debe volver a escribirlo.
    project.addDocument("Segundo");
    project.saveToFile(kProjectPath);
    CHECK(fileExists(kProjectPath));

    std::remove(kProjectPath);
    std::remove("Documento.dfc");
    std::remove("Segundo.dfc");
}

TEST_CASE("A multi-document project round-trips through saveToFile/loadFromFile", "[project][roundtrip]") {
    constexpr const char* kProjectPath = "test_project_roundtrip.dfproj";
    {
        Project project;
        project.renameDocument(project.documentIds().front(), "Principal");
        wireAndGateWithLed(*project.activeDocument());

        const uint32_t secondId = project.addDocument("Vacio");
        project.setActiveDocument(secondId);

        project.saveToFile(kProjectPath);
        CHECK_FALSE(project.hasUnsavedChanges());
    }

    Project loaded;
    loaded.loadFromFile(kProjectPath);

    const auto ids = loaded.documentIds();
    REQUIRE(ids.size() == 2);
    CHECK(loaded.filePath() == QString(kProjectPath));

    bool foundPrincipal = false;
    bool foundVacio = false;
    for (const uint32_t id : ids) {
        if (loaded.documentName(id) == QString("Principal")) {
            foundPrincipal = true;
            CHECK(loaded.document(id)->componentIds().size() == 4);
        } else if (loaded.documentName(id) == QString("Vacio")) {
            foundVacio = true;
            CHECK(loaded.document(id)->componentIds().empty());
        }
    }
    CHECK(foundPrincipal);
    CHECK(foundVacio);

    std::remove(kProjectPath);
    std::remove("Principal.dfc");
    std::remove("Vacio.dfc");
}

TEST_CASE("Project::loadFromFile treats a legacy single-circuit .dfproj as a one-document project with a "
          "known location",
          "[project][compat]") {
    // No es un manifiesto propio (no hay .dfc separados, el circuito vive
    // directo en el .dfproj, schema viejo), pero SI tiene una ubicacion
    // conocida en disco -- filePath() debe reportarla para que Guardar
    // reescriba ahi mismo en vez de tratar cada guardado como si el
    // proyecto nunca se hubiera abierto (ver MainWindow::onSave(), que cae a
    // "Guardar como" cuando filePath() esta vacio).
    constexpr const char* kLegacyPath = "test_project_legacy.dfproj";
    CircuitDocument legacy;
    wireAndGateWithLed(legacy);
    formats::saveProjectToFile(legacy, kLegacyPath);

    Project project;
    project.loadFromFile(kLegacyPath);

    CHECK(project.documentIds().size() == 1);
    CHECK(project.filePath() == QString(kLegacyPath));
    CHECK(project.document(project.activeDocumentId())->componentIds().size() == 4);

    // Guardar en el mismo path no debe promoverlo a manifiesto (seguiria
    // siendo el circuito solo) ni crear ningun .dfc adicional al lado.
    project.document(project.activeDocumentId())->addComponent("io.led");
    project.saveToFile(project.filePath());
    CHECK_FALSE(fileExists("test_project_legacy.dfc"));

    Project reloaded;
    reloaded.loadFromFile(kLegacyPath);
    CHECK(reloaded.document(reloaded.activeDocumentId())->componentIds().size() == 5);

    std::remove(kLegacyPath);
}

TEST_CASE("A fresh Project's initial document is the undoGroup's active stack from the start", "[project][undo]") {
    // Regresion: addEntry() solo hace QUndoGroup::addStack() (que no activa
    // el stack solo); newProject() seteaba activeId_ a mano sin pasar por
    // setActiveDocument(), asi que el grupo se quedaba sin stack activo y
    // Deshacer/Rehacer quedaban deshabilitados para siempre en el documento
    // anonimo inicial (el unico que existe hasta que se agrega un segundo
    // documento) - ver Project::newProject().
    Project project;
    const uint32_t id = project.activeDocumentId();
    QUndoStack* stack = project.undoStack(id);
    CHECK(project.undoGroup().activeStack() == stack);

    CircuitDocument* doc = project.document(id);
    stack->push(new digitalforge::editor::PlaceComponentCommand(doc, "wiring.input", {},
                                                                  digitalforge::editor::ComponentPlacement{}));

    CHECK(project.undoGroup().activeStack()->canUndo());
}

TEST_CASE("addKarnaughDocument adds a document distinguishable via documentKind()", "[project][karnaugh]") {
    Project project;
    const uint32_t circuitId = project.documentIds().front();
    const uint32_t karnaughId = project.addKarnaughDocument("MiMapa", 3);

    CHECK(karnaughId != circuitId); // mismo espacio de ids, nunca chocan
    CHECK(project.documentKind(circuitId) == Project::DocumentKind::Circuit);
    CHECK(project.documentKind(karnaughId) == Project::DocumentKind::Karnaugh);
    CHECK(project.karnaughDocumentIds() == std::vector<uint32_t>{karnaughId});
    CHECK(project.karnaughDocumentName(karnaughId) == QString("MiMapa"));
    CHECK(project.karnaughDocument(karnaughId)->variableCount() == 3);
    CHECK(project.hasUnsavedChanges());

    // No participa de activeDocumentId()/activeDocument(): agregar un mapa
    // de Karnaugh nunca cambia "cual circuito esta activo" (ver el
    // comentario de Project::addKarnaughDocument()).
    CHECK(project.activeDocumentId() == circuitId);

    CHECK_THROWS_AS(project.documentKind(999), std::invalid_argument);
}

TEST_CASE("addKarnaughDocument shares the same name namespace as circuit documents", "[project][karnaugh][uniqueness]") {
    Project project;
    project.renameDocument(project.documentIds().front(), "Compartido");
    CHECK_THROWS_AS(project.addKarnaughDocument("Compartido"), std::invalid_argument);
    CHECK_THROWS_AS(project.addKarnaughDocument("COMPARTIDO"), std::invalid_argument);

    const uint32_t karnaughId = project.addKarnaughDocument("SoloKarnaugh");
    CHECK_THROWS_AS(project.addDocument("SoloKarnaugh"), std::invalid_argument);
    CHECK_THROWS_AS(project.renameDocument(project.documentIds().front(), "SoloKarnaugh"), std::invalid_argument);
    CHECK_THROWS_AS(project.renameKarnaughDocument(karnaughId, "Compartido"), std::invalid_argument);
}

TEST_CASE("removeKarnaughDocument removes it without requiring at least one to remain", "[project][karnaugh]") {
    Project project;
    const uint32_t id = project.addKarnaughDocument("Temporal");
    REQUIRE(project.karnaughDocumentIds().size() == 1);

    project.removeKarnaughDocument(id);
    CHECK(project.karnaughDocumentIds().empty()); // a diferencia de removeDocument(), cero es valido
    CHECK_THROWS_AS(project.removeKarnaughDocument(id), std::invalid_argument);
}

TEST_CASE("renameKarnaughDocument updates the display name and emits karnaughDocumentRenamed",
          "[project][karnaugh]") {
    Project project;
    const uint32_t id = project.addKarnaughDocument("Original");
    int renamedCount = 0;
    QObject::connect(&project, &Project::karnaughDocumentRenamed, [&](uint32_t renamedId) {
        ++renamedCount;
        CHECK(renamedId == id);
    });
    project.renameKarnaughDocument(id, "Renombrado");
    CHECK(project.karnaughDocumentName(id) == QString("Renombrado"));
    CHECK(renamedCount == 1);
}

TEST_CASE("A project mixing a circuit and a Karnaugh map round-trips through saveToFile/loadFromFile",
          "[project][karnaugh][roundtrip]") {
    constexpr const char* kProjectPath = "test_project_karnaugh_roundtrip.dfproj";
    {
        Project project;
        project.renameDocument(project.documentIds().front(), "Circuito");
        wireAndGateWithLed(*project.activeDocument());

        const uint32_t karnaughId = project.addKarnaughDocument("Mapa", 3);
        project.karnaughDocument(karnaughId)->setVariableName(0, "X");
        project.karnaughDocument(karnaughId)->setCellValue(0, 5, digitalforge::editor::KarnaughCellValue::One);

        project.saveToFile(kProjectPath);
        CHECK_FALSE(project.hasUnsavedChanges());
    }

    Project loaded;
    loaded.loadFromFile(kProjectPath);

    // El circuito sigue siendo el documento activo tras recargar (ver el
    // comentario de Project::saveToFile() sobre por que los mapas de
    // Karnaugh siempre van al final del arreglo del manifiesto).
    CHECK(loaded.documentIds().size() == 1);
    CHECK(loaded.document(loaded.activeDocumentId())->componentIds().size() == 4);

    REQUIRE(loaded.karnaughDocumentIds().size() == 1);
    const uint32_t karnaughId = loaded.karnaughDocumentIds().front();
    CHECK(loaded.karnaughDocumentName(karnaughId) == QString("Mapa"));
    CHECK(loaded.karnaughDocument(karnaughId)->variableCount() == 3);
    CHECK(loaded.karnaughDocument(karnaughId)->variableName(0) == QString("X"));
    CHECK(loaded.karnaughDocument(karnaughId)->cellValue(0, 5) == digitalforge::editor::KarnaughCellValue::One);

    std::remove(kProjectPath);
    std::remove("Circuito.dfc");
    std::remove("Mapa.dfk");
}

TEST_CASE("loadFromFile still loads a manifest with no \"kind\" key exactly as before (backward compatibility)",
          "[project][karnaugh][compat]") {
    // Un .dfproj guardado ANTES de que existiera el campo "kind" (todo
    // proyecto guardado hasta hoy) no lo tiene en absoluto -- debe seguir
    // cargando cada entrada como circuito, exactamente igual que siempre.
    constexpr const char* kProjectPath = "test_project_no_kind_field.dfproj";
    constexpr const char* kDocPath = "test_project_no_kind_field_doc.dfc";
    {
        CircuitDocument doc;
        wireAndGateWithLed(doc);
        formats::saveProjectToFile(doc, kDocPath);
    }
    nlohmann::json manifest;
    manifest["schemaVersion"] = 1;
    manifest["projectName"] = "SinKind";
    manifest["documents"] = nlohmann::json::array();
    manifest["documents"].push_back({{"name", "SoloDoc"}, {"path", kDocPath}}); // sin "kind"
    manifest["activeDocument"] = 0;
    std::ofstream out(kProjectPath, std::ios::trunc);
    out << manifest.dump(2);
    out.close();

    Project project;
    project.loadFromFile(kProjectPath);
    CHECK(project.documentIds().size() == 1);
    CHECK(project.karnaughDocumentIds().empty());
    CHECK(project.document(project.activeDocumentId())->componentIds().size() == 4);

    std::remove(kProjectPath);
    std::remove(kDocPath);
}

TEST_CASE("addExcitationTableDocument adds a document distinguishable via documentKind()",
          "[project][excitationTable]") {
    Project project;
    const uint32_t circuitId = project.documentIds().front();
    const uint32_t excitationId = project.addExcitationTableDocument("MiExcitacion", 3);

    CHECK(excitationId != circuitId); // mismo espacio de ids, nunca chocan
    CHECK(project.documentKind(circuitId) == Project::DocumentKind::Circuit);
    CHECK(project.documentKind(excitationId) == Project::DocumentKind::ExcitationTable);
    CHECK(project.excitationTableDocumentIds() == std::vector<uint32_t>{excitationId});
    CHECK(project.excitationTableDocumentName(excitationId) == QString("MiExcitacion"));
    CHECK(project.excitationTableDocument(excitationId)->stateBitCount() == 3);
    CHECK(project.hasUnsavedChanges());

    // No participa de activeDocumentId()/activeDocument(): mismo criterio
    // que un mapa de Karnaugh (ver el comentario de
    // Project::addKarnaughDocument()).
    CHECK(project.activeDocumentId() == circuitId);

    CHECK_THROWS_AS(project.documentKind(999), std::invalid_argument);
}

TEST_CASE("addExcitationTableDocument shares the same name namespace as circuit/Karnaugh/TruthTable documents",
          "[project][excitationTable][uniqueness]") {
    Project project;
    project.renameDocument(project.documentIds().front(), "Compartido");
    CHECK_THROWS_AS(project.addExcitationTableDocument("Compartido"), std::invalid_argument);
    CHECK_THROWS_AS(project.addExcitationTableDocument("COMPARTIDO"), std::invalid_argument);

    const uint32_t excitationId = project.addExcitationTableDocument("SoloExcitacion");
    CHECK_THROWS_AS(project.addDocument("SoloExcitacion"), std::invalid_argument);
    CHECK_THROWS_AS(project.renameDocument(project.documentIds().front(), "SoloExcitacion"), std::invalid_argument);
    CHECK_THROWS_AS(project.renameExcitationTableDocument(excitationId, "Compartido"), std::invalid_argument);
}

TEST_CASE("removeExcitationTableDocument removes it without requiring at least one to remain",
          "[project][excitationTable]") {
    Project project;
    const uint32_t id = project.addExcitationTableDocument("Temporal");
    REQUIRE(project.excitationTableDocumentIds().size() == 1);

    project.removeExcitationTableDocument(id);
    CHECK(project.excitationTableDocumentIds().empty()); // a diferencia de removeDocument(), cero es valido
    CHECK_THROWS_AS(project.removeExcitationTableDocument(id), std::invalid_argument);
}

TEST_CASE("renameExcitationTableDocument updates the display name and emits excitationTableDocumentRenamed",
          "[project][excitationTable]") {
    Project project;
    const uint32_t id = project.addExcitationTableDocument("Original");
    int renamedCount = 0;
    QObject::connect(&project, &Project::excitationTableDocumentRenamed, [&](uint32_t renamedId) {
        ++renamedCount;
        CHECK(renamedId == id);
    });
    project.renameExcitationTableDocument(id, "Renombrado");
    CHECK(project.excitationTableDocumentName(id) == QString("Renombrado"));
    CHECK(renamedCount == 1);
}

TEST_CASE("A project mixing a circuit and an excitation table round-trips through saveToFile/loadFromFile",
          "[project][excitationTable][roundtrip]") {
    constexpr const char* kProjectPath = "test_project_excitation_roundtrip.dfproj";
    {
        Project project;
        project.renameDocument(project.documentIds().front(), "Circuito");
        wireAndGateWithLed(*project.activeDocument());

        const uint32_t excitationId = project.addExcitationTableDocument("Excitacion", 3);
        auto* excitationDoc = project.excitationTableDocument(excitationId);
        excitationDoc->setStateBitName(0, "X");
        excitationDoc->setFlipFlopType(1, digitalforge::editor::FlipFlopType::JK);
        excitationDoc->setNextState(0, 5, digitalforge::editor::KarnaughCellValue::One);

        project.saveToFile(kProjectPath);
        CHECK_FALSE(project.hasUnsavedChanges());
    }

    Project loaded;
    loaded.loadFromFile(kProjectPath);

    // El circuito sigue siendo el documento activo tras recargar (mismo
    // criterio que un mapa de Karnaugh: las tablas de excitacion van al
    // final del arreglo del manifiesto).
    CHECK(loaded.documentIds().size() == 1);
    CHECK(loaded.document(loaded.activeDocumentId())->componentIds().size() == 4);

    REQUIRE(loaded.excitationTableDocumentIds().size() == 1);
    const uint32_t excitationId = loaded.excitationTableDocumentIds().front();
    CHECK(loaded.excitationTableDocumentName(excitationId) == QString("Excitacion"));
    auto* excitationDoc = loaded.excitationTableDocument(excitationId);
    CHECK(excitationDoc->stateBitCount() == 3);
    CHECK(excitationDoc->stateBitName(0) == QString("X"));
    CHECK(excitationDoc->flipFlopType(1) == digitalforge::editor::FlipFlopType::JK);
    CHECK(excitationDoc->nextState(0, 5) == digitalforge::editor::KarnaughCellValue::One);

    std::remove(kProjectPath);
    std::remove("Circuito.dfc");
    std::remove("Excitacion.dfe");
}
