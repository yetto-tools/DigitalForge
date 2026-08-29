#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>

class QUndoStack;

namespace digitalforge::editor {

class CircuitDocument;
class CircuitScene;

// Capa (columna logica) de cada componente de `scope` (o de TODOS los
// componentes del documento si `scope` es nullopt), calculada por camino MAS
// LARGO desde las fuentes (componentes sin predecesores dentro del scope,
// tipicamente wiring.input/wiring.constant) sobre el grafo de dependencia
// que arma el cableado real: una arista componente-fuente -> componente-
// consumidor por cada pin Output de uno conectado (directo o via puntos de
// union) a un pin no-Output del otro. Los ciclos de realimentacion (latches/
// flip-flops armados a mano con compuertas cruzadas) se rompen con un DFS
// antes de asignar capas -- back-edges no participan del calculo, tecnica
// estandar de layout por capas (Sugiyama). Expuesta aca (no confinada al
// .cpp) para poder testear el algoritmo de capas sin necesitar una
// CircuitScene real -- ver autoArrangeCircuit() para el resto del proceso
// (orden dentro de cada capa, posiciones, ruteo de cables) que si la
// necesita.
[[nodiscard]] std::map<uint32_t, int> computeCircuitLayers(const CircuitDocument& document,
                                                            const std::optional<std::set<uint32_t>>& scope);

// Reacomoda un circuito ya dibujado: reposiciona sus componentes en columnas
// segun el sentido de la senal (fuentes a la izquierda, consumidores a la
// derecha, ver computeCircuitLayers()) y recablea sus cables/puntos de union
// en carriles de bus -- mismo espiritu que
// formats::synthesizeMultiOutputToCircuit() (columnas + wireNet() en
// KarnaughSynthesizer.cpp), pero aplicado a un circuito YA EXISTENTE en vez
// de sintetizado desde cero.
//
// `componentScope` vacio (nullopt) reordena TODO el documento activo de
// `scene`. Con un conjunto de ids, solo esos componentes se reposicionan
// ("Auto-ordenar seleccion"): un punto de union solo se reubica si TODAS sus
// ramas terminan (directo o a traves de otras uniones) en un componente
// del scope; si alguna sale afuera, el punto de union queda fijo donde
// estaba y solo se recalculan las ramas que van hacia adentro del scope,
// ancladas a esa posicion sin cambiar.
//
// Un unico paso de undo (macro de QUndoStack) con un MoveComponentCommand
// por componente reposicionado, un MoveJunctionCommand por union
// reposicionada, y un SetWireWaypointsCommand por cable recableado. No hace
// nada (sin macro vacio) si el scope resultante no tiene ningun componente.
void autoArrangeCircuit(CircuitScene& scene, QUndoStack& undoStack,
                         const std::optional<std::set<uint32_t>>& componentScope = std::nullopt);

} // namespace digitalforge::editor
