#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace digitalforge::editor {
class ExcitationTableDocument;
}

namespace digitalforge::formats {

// Formato de una tabla de excitacion guardada (.dfe, schemaVersion 1):
// {
//   "schemaVersion": 1,
//   "kind": "excitationtable",
//   "stateBitCount": 2,
//   "bits": [
//     {"name": "Q0", "flipFlopType": "D", "presentState": ["0", "1", ...], // 2^stateBitCount
//      "nextState": ["0", "1", "X", ...]},                                 // entradas cada uno,
//     {"name": "Q1", "flipFlopType": "JK", "presentState": [...], "nextState": [...]} // indexadas
//   ]                                                                       // por NUMERO de minterm
// }
// (mismo criterio de un caracter por celda que KarnaughSerializer/TruthTableSerializer).
[[nodiscard]] nlohmann::json serializeExcitationTableDocument(const editor::ExcitationTableDocument& document);

// Reemplaza el contenido de `document` con lo descrito por `json`. Lanza
// std::invalid_argument ante un schemaVersion no soportado/ausente,
// stateBitCount fuera de [2,4], "bits" de tamano distinto a stateBitCount, o
// un arreglo "presentState"/"nextState" de tamano distinto a 2^stateBitCount
// en cualquier bit.
void loadExcitationTableDocument(editor::ExcitationTableDocument& document, const nlohmann::json& json);

// Lanza std::runtime_error si el archivo no se puede escribir/leer.
void saveExcitationTableDocumentToFile(const editor::ExcitationTableDocument& document, const std::string& path);
void loadExcitationTableDocumentFromFile(editor::ExcitationTableDocument& document, const std::string& path);

} // namespace digitalforge::formats
