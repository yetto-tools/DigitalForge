#pragma once

#include <QString>

#include <vector>

#include "editor/FlipFlopExcitation.hpp"
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

// Un bit de estado para sintesis SECUENCIAL completa (con flip-flops de
// verdad, no solo su logica de excitacion) -- ver synthesizeSequentialCircuit().
struct FlipFlopSpec {
    QString bitName;
    // SR no soportado: la biblioteca integrada no tiene ningun flip-flop SR
    // con reloj (solo memory.srLatch, sin CLK) -- synthesizeSequentialCircuit()
    // lanza si aparece.
    editor::FlipFlopType type;
    // Resultado(s) YA minimizados (editor::minimize()) de la excitacion de
    // este bit, en el MISMO variableCount/orden de variables que todos los
    // demas bits del conjunto (los Q de cada uno): 1 elemento para D o T, 2
    // para JK en orden {J, K} -- mismo orden que devuelve
    // editor::computeExcitation().
    std::vector<editor::KarnaughResult> excitationResults;
};

// Version "circuito completo" de synthesizeMultiOutputToCircuit(): coloca un
// flip-flop de verdad (memory.dFlipFlop/tFlipFlop/jkFlipFlop, sin
// preset/clear asincronos) por cada FlipFlopSpec de `flipFlops`, un unico
// wiring.clock compartido cableado al CLK de todos, y sintetiza (mismo motor
// AND->OR que synthesizeMultiOutputToCircuit()) la logica combinacional de
// cada entrada de excitacion cableada DIRECTO al pin correspondiente del
// flip-flop, en vez de a una wiring.output. Las variables de esa logica son
// los propios Q/Q' de los flip-flops (realimentacion): a diferencia de la
// sintesis puramente combinacional, no hay ningun wiring.input ni gates.not
// para ellas -- Q' ya es una salida real del componente.
//
// Vacia `target` primero. Lanza std::invalid_argument si `flipFlops` esta
// vacio, si algun FlipFlopSpec::type es FlipFlopType::SR, si algun
// excitationResults.size() no coincide con lo que su tipo espera (1 para
// D/T, 2 para JK), o si algun KarnaughResult no coincide en variableCount
// con flipFlops.size() o referencia un variableIndex fuera de rango.
void synthesizeSequentialCircuit(editor::CircuitDocument& target, const std::vector<FlipFlopSpec>& flipFlops,
                                  const SynthesisOptions& options = {});

} // namespace digitalforge::formats
