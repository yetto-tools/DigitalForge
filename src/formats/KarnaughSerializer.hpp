#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace digitalforge::editor {
class KarnaughDocument;
}

namespace digitalforge::formats {

// Formato de un mapa de Karnaugh guardado (.dfk). Se escribe siempre en
// schemaVersion 2 (varias funciones):
// {
//   "schemaVersion": 2,
//   "kind": "karnaugh",
//   "variableCount": 4,
//   "variableNames": ["A", "B", "C", "D"],
//   "outputs": [
//     {"name": "F1", "cells": ["0", "1", "X", ...]},  // 2^variableCount
//     {"name": "F2", "cells": [...]}                   // entradas cada una,
//   ]                                                   // indexadas por
// }                                                      // NUMERO de minterm
// (mismo criterio de un caracter por celda que TruthTableSerializer -- de
// hecho el mismo formato "outputs", ver TruthTableSerializer.hpp).
//
// Tambien se puede LEER schemaVersion 1 (formato viejo, de una sola funcion:
// {"schemaVersion": 1, ..., "cells": [...]} sin "outputs") -- se carga como
// una unica funcion "F1", para no romper ningun .dfk guardado antes de que
// existieran varias funciones por mapa.
[[nodiscard]] nlohmann::json serializeKarnaughDocument(const editor::KarnaughDocument& document);

// Reemplaza el contenido de `document` con lo descrito por `json`. Lanza
// std::invalid_argument ante un schemaVersion no soportado/ausente,
// variableCount fuera de [2,4], o un arreglo "cells" (schemaVersion 1) o
// alguna "cells" de "outputs" (schemaVersion 2) de tamano distinto a
// 2^variableCount.
void loadKarnaughDocument(editor::KarnaughDocument& document, const nlohmann::json& json);

// Lanza std::runtime_error si el archivo no se puede escribir/leer.
void saveKarnaughDocumentToFile(const editor::KarnaughDocument& document, const std::string& path);
void loadKarnaughDocumentFromFile(editor::KarnaughDocument& document, const std::string& path);

} // namespace digitalforge::formats
