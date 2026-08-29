#pragma once

#include <QString>

#include <vector>

#include "KarnaughMap.hpp"

namespace digitalforge::editor {

// Que flip-flop se quiere usar para un bit de estado -- decide cuantas
// columnas de excitacion produce computeExcitation() y con que tabla.
enum class FlipFlopType : uint8_t { D, T, JK, SR };

// Una columna de excitacion lista para usarse como salida de un
// KarnaughDocument/TruthTableDocument: mismo tamano/orden (indexado por
// minterm de estado actual) que el nextState del que se derivo.
struct ExcitationColumn {
    QString name;
    std::vector<KarnaughCellValue> values;
};

// Deriva la(s) columna(s) de excitacion de un bit de estado a partir de su
// estado ACTUAL (`presentState`, editable por el usuario -- ver
// ExcitationTableDocument::presentState()) y su estado SIGUIENTE deseado
// (`nextState`), ambos del mismo tamano e indexados por minterm de estado
// actual. Devuelve 1 columna (D, T) o 2 (J+K, S+R), nombradas con el
// prefijo del tipo mas `bitName` (p. ej. "J"+bitName).
//
// Tablas de excitacion estandar (identicas a las que usan Logisim/Proteus):
// si presentState[m] O nextState[m] es DontCare, todas las columnas de ese
// minterm tambien lo son -- un estado actual en DontCare marca esa fila como
// inalcanzable (no importa como se llega ni a donde va desde ahi); un estado
// siguiente en DontCare marca solo que no importa a donde va.
[[nodiscard]] std::vector<ExcitationColumn> computeExcitation(FlipFlopType type, const QString& bitName,
                                                               const std::vector<KarnaughCellValue>& presentState,
                                                               const std::vector<KarnaughCellValue>& nextState);

} // namespace digitalforge::editor
