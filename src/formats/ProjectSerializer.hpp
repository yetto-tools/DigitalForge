#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace digitalforge::editor {
class CircuitDocument;
}

namespace digitalforge::formats {

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
void loadProject(editor::CircuitDocument& document, const nlohmann::json& json);

// Lanza std::runtime_error si el archivo no se puede escribir/leer.
void saveProjectToFile(const editor::CircuitDocument& document, const std::string& path);
void loadProjectFromFile(editor::CircuitDocument& document, const std::string& path);

} // namespace digitalforge::formats
