// Ejercita editor::Project (multiples CircuitDocument bajo un mismo
// proyecto) + formats::ProjectManifestSerializer de punta a punta. No
// necesita su propio main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include <QUndoStack>

#include "editor/CircuitDocument.hpp"
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

TEST_CASE("Project::loadFromFile treats a legacy single-circuit .dfproj as an anonymous one-document project",
          "[project][compat]") {
    constexpr const char* kLegacyPath = "test_project_legacy.dfproj";
    CircuitDocument legacy;
    wireAndGateWithLed(legacy);
    formats::saveProjectToFile(legacy, kLegacyPath);

    Project project;
    project.loadFromFile(kLegacyPath);
    std::remove(kLegacyPath);

    CHECK(project.documentIds().size() == 1);
    CHECK(project.filePath().isEmpty()); // sigue siendo "anonimo": no hay manifiesto .dfproj propio
    CHECK(project.document(project.activeDocumentId())->componentIds().size() == 4);
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
