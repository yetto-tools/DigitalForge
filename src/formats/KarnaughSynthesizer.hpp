#pragma once

#include <QString>

#include <vector>

#include "editor/KarnaughMap.hpp"

namespace digitalforge::editor {
class CircuitDocument;
} // namespace digitalforge::editor

namespace digitalforge::formats {

// Como resolver un literal negado al sintetizar: por defecto (false) cada
// variable que aparece negada en algun termino recibe su propia compuerta
// gates.not visible (una sola, compartida entre todos los terminos que la
// necesiten) - mas fiel a como se dibujaria a mano y mas educativo (se ve
// el inversor). Con useInvertMask=true, un termino de 2+ literales que
// necesita una entrada negada usa en cambio la propiedad "invertMask" de
// la compuerta gates.and que lo consume (ver makeVariadicGateDefinition en
// BasicComponentLibrary.cpp) - menos compuertas visibles, pero sin bombita
// de inversor en el dibujo. Un termino de UN solo literal negado (sin
// gates.and que lo consuma) sigue necesitando su gates.not igual, sin
// importar esta opcion -- no hay ninguna otra compuerta a la que colgarle
// un invertMask.
struct SynthesisOptions {
    bool useInvertMask = false;
};

// Coloca compuertas AND/OR/NOT (y wiring.input/wiring.output/
// wiring.constant segun haga falta) en `target` que implementan
// exactamente `result.sopExpression` (la suma de productos ya minimizada
// por editor::minimize()), cableadas segun result.selectedGroups. Vacia
// `target` primero (mismo criterio defensivo que
// formats::importLogisimCircFile) -- pensado para invocarse sobre un
// CircuitDocument recien creado via Project::addDocument(), pero tambien
// deja abierta la puerta a una futura "regeneracion" sobre uno ya
// existente.
//
// Topologia (de izquierda a derecha): una wiring.input por variable ->
// (opcional) una gates.not compartida por variable que aparezca negada ->
// una gates.and por termino de 2+ literales (o el net del unico literal
// directo si el termino tiene exactamente 1, o una wiring.constant si
// tiene 0 -- caso "siempre 1") -> una gates.or si hay 2+ terminos (o el net
// del unico termino directo si hay exactamente 1, o una wiring.constant
// "0" si no hay ninguno) -> una wiring.output final.
//
// Lanza std::invalid_argument si result.variableNames.size() no coincide
// con result.variableCount, o si algun Implicant::literals() de
// result.selectedGroups referencia un variableIndex fuera de rango.
void synthesizeToCircuit(editor::CircuitDocument& target, const editor::KarnaughResult& result,
                          const SynthesisOptions& options = {});

// Una salida con nombre de una sintesis multi-salida (ver
// synthesizeMultiOutputToCircuit()): `result` ya tiene que venir minimizado
// (editor::minimize()) sobre el MISMO variableCount que las demas salidas de
// la misma llamada -- cada salida puede cubrir un subconjunto de minterms
// distinto (esa es la idea), pero las variables de entrada son compartidas.
struct OutputSpec {
    QString name;
    editor::KarnaughResult result;
};

// Version multi-salida de synthesizeToCircuit(): arma una UNICA columna de
// entradas (una wiring.input por variable, mas los gates.not compartidos que
// haga falta) y la reusa entre TODAS las salidas de `outputs`, en vez de
// duplicarla una vez por salida -- asi un circuito con varias salidas que
// comparten entradas (p. ej. un decodificador BCD a 7 segmentos, con una
// tabla de verdad de una columna de salida por segmento) queda como UN solo
// circuito con las entradas cableadas una sola vez, en vez de tener que
// generar cada salida por separado y unirlas a mano despues.
//
// Cada salida de `outputs` se dibuja en su propia banda de filas (una debajo
// de la otra) para que sus compuertas no se superpongan con las de otra
// salida; internamente es la misma topologia de synthesizeToCircuit() por
// cada una (columnas AND -> OR -> wiring.output, con el nombre de
// `OutputSpec::name` en vez de "F" fijo).
//
// Vacia `target` primero. Lanza std::invalid_argument si `outputs` esta
// vacio, si variableNames.size() no coincide con variableCount, si algun
// OutputSpec::result.variableCount no coincide con variableCount, o si algun
// literal de algun result referencia un variableIndex fuera de rango.
void synthesizeMultiOutputToCircuit(editor::CircuitDocument& target, int variableCount,
                                     const std::vector<QString>& variableNames,
                                     const std::vector<OutputSpec>& outputs, const SynthesisOptions& options = {});

} // namespace digitalforge::formats
