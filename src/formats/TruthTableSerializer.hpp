#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace digitalforge::editor {
class TruthTableDocument;
}

namespace digitalforge::formats {

// Formato de una tabla de verdad guardada (.dft, schemaVersion 1):
// {
//   "schemaVersion": 1,
//   "kind": "truthtable",
//   "variableCount": 4,
//   "variableNames": ["A", "B", "C", "D"],
//   "outputs": [
//     {"name": "F1", "cells": ["0", "1", "X", ...]},  // 2^variableCount
//     {"name": "F2", "cells": [...]}                   // entradas cada una,
//   ]                                                   // indexadas por
// }                                                      // NUMERO de minterm
// (mismo criterio de un caracter por celda que KarnaughSerializer).
[[nodiscard]] nlohmann::json serializeTruthTableDocument(const editor::TruthTableDocument& document);

// Reemplaza el contenido de `document` con lo descrito por `json`. Lanza
// std::invalid_argument ante un schemaVersion no soportado/ausente,
// variableCount fuera de [2,4], "outputs" vacio o ausente, o un arreglo
// "cells" de tamano distinto a 2^variableCount en cualquier salida.
void loadTruthTableDocument(editor::TruthTableDocument& document, const nlohmann::json& json);

// Lanza std::runtime_error si el archivo no se puede escribir/leer.
void saveTruthTableDocumentToFile(const editor::TruthTableDocument& document, const std::string& path);
void loadTruthTableDocumentFromFile(editor::TruthTableDocument& document, const std::string& path);

} // namespace digitalforge::formats
