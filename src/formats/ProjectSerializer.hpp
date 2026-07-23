#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

#include "components/ComponentFingerprints.hpp"

namespace digitalforge::editor {
class CircuitDocument;
}

namespace digitalforge::formats {

// Una instancia guardada cuya definicion instalada ya no coincide con la que
// se uso al guardar el proyecto. Ver ProjectCompatibilityReport.
struct ComponentCompatibilityIssue {
    uint32_t componentId = 0;
    std::string typeId;
    // Version declarada por la definicion al guardar y la que hay instalada
    // ahora. Iguales no implica compatibilidad: lo que manda son las huellas
    // (alguien puede haber cambiado la definicion sin subir la version).
    uint32_t storedDefinitionVersion = 0;
    uint32_t currentDefinitionVersion = 0;
    components::CompatibilityVerdict verdict = components::CompatibilityVerdict::Identical;
};

// Un cable que no se pudo restaurar porque la clave de pin que tenia guardada
// ya no existe en el componente. Deliberadamente NO se reconecta por indice:
// hacerlo cablearia el circuito a otro pin en silencio.
struct UnresolvedWire {
    uint32_t wireId = 0;
    uint32_t componentId = 0;
    std::string pinKey;
};

// Resultado de contrastar lo guardado contra las definiciones instaladas al
// abrir un proyecto. Es informativo: la carga NO se aborta por una
// incompatibilidad; el objetivo es que DigitalForge pueda explicar que
// cambio, en vez de aceptarlo en silencio.
struct ProjectCompatibilityReport {
    // false para proyectos guardados antes de que existieran las huellas: no
    // hay nada contra que comparar, y eso no es lo mismo que "todo coincide".
    bool hasStoredMetadata = false;
    // Solo las instancias cuyo veredicto no es Identical.
    std::vector<ComponentCompatibilityIssue> issues;
    // Cables que quedaron sin restaurar por una clave de pin inexistente.
    std::vector<UnresolvedWire> unresolvedWires;

    [[nodiscard]] bool empty() const noexcept { return issues.empty() && unresolvedWires.empty(); }
};

// Formato provisional de proyecto de un solo circuito (schemaVersion 1):
// el typeId/instanceId/properties de cada componente (via
// ComponentInstance::toJson) mas su posicion/rotacion, y los extremos de
// cada cable. No se guardan indices de nodo/puerta - esos son solo en
// tiempo de ejecucion y se reconstruyen mediante
// CircuitDocument::rebuildSimulation() tras la carga. Multiples circuitos,
// subcircuitos y una lista explicita de bibliotecas quedan intencionalmente
// fuera de alcance hasta la fase de circuitos jerarquicos.
[[nodiscard]] nlohmann::json serializeProject(const editor::CircuitDocument& document);

// Reemplaza todo el contenido de `document` con el proyecto descrito por
// `json`. Lanza std::invalid_argument ante un schemaVersion no soportado o
// ausente, un typeId de componente desconocido, o datos de
// componente/cable malformados.
//
// Si `report` no es nulo, se llena con las diferencias entre las huellas
// guardadas y las definiciones instaladas (ver ProjectCompatibilityReport).
// Pasarlo o no NO cambia lo que se carga.
void loadProject(editor::CircuitDocument& document, const nlohmann::json& json,
                 ProjectCompatibilityReport* report = nullptr);

// Lanza std::runtime_error si el archivo no se puede escribir/leer.
void saveProjectToFile(const editor::CircuitDocument& document, const std::string& path);
void loadProjectFromFile(editor::CircuitDocument& document, const std::string& path);

} // namespace digitalforge::formats
