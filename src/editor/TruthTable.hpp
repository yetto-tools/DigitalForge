#pragma once

#include <QString>

#include <cstddef>
#include <vector>

namespace digitalforge::editor {

class CircuitDocument;

// Una fila de la tabla: un caracter ('0'/'1'/'Z'/'X'/'E', ver core::toChar)
// por columna, en el mismo orden que TruthTable::inputHeaders seguido de
// TruthTable::outputHeaders.
struct TruthTableRow {
    std::vector<char> values;
};

// Formula booleana (suma de productos canonica) de una columna de salida --
// ver computeTruthTable(). `complete` es false si alguna fila tuvo, para
// esa salida, un valor distinto de '0'/'1' (Z/X/E: red flotante,
// inestable, o error) -- esas filas se excluyen de la suma en vez de
// arriesgar un termino incorrecto, asi que la formula puede no cubrir
// todas las combinaciones de entrada.
struct TruthTableFormula {
    QString expression;
    bool complete = true;
};

struct TruthTable {
    std::vector<QString> inputHeaders;
    std::vector<QString> outputHeaders;
    std::vector<TruthTableRow> rows;
    // Paralelo a outputHeaders: la formula de suma de productos de cada
    // salida, en terminos de inputHeaders.
    std::vector<TruthTableFormula> outputFormulas;
};

// Barre exhaustivamente todas las combinaciones de las wiring.input de
// `document` (2^N filas) y lee cada wiring.output para armar la tabla de
// verdad resultante. Restaura los valores originales de cada entrada al
// terminar - el barrido nunca debe dejar la simulacion en vivo del usuario
// en un estado arbitrario.
//
// Logica pura sobre CircuitDocument, sin ningun widget - ui::TruthTablePanel
// es quien la muestra. Lanza std::invalid_argument si el documento no tiene
// al menos una wiring.input y una wiring.output, o si tiene mas de
// `maxInputs` entradas (un barrido de mas de eso es impracticable: no hay
// una ruta de evaluacion async/incremental en este simulador).
[[nodiscard]] TruthTable computeTruthTable(CircuitDocument& document, std::size_t maxInputs = 20);

} // namespace digitalforge::editor
