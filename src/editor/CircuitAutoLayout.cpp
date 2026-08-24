#include "CircuitAutoLayout.hpp"
#include "CircuitLayoutGraph.hpp"

#include <QPointF>
#include <QRectF>
#include <QUndoStack>

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

#include "CircuitDocument.hpp"
#include "CircuitScene.hpp"
#include "ComponentItem.hpp"
#include "UndoCommands.hpp"
#include "components/ComponentInstance.hpp"
#include "core/Pin.hpp"

namespace digitalforge::editor {

namespace {

constexpr qreal kColumnGap = 64.0;
constexpr qreal kRowGap = 24.0;
// Separacion entre carriles verticales de bus dentro de un mismo hueco entre
// columnas -- mismo valor/proposito que KarnaughSynthesizer.cpp
// (kBusLaneSpacing), pero sin el limite de "como mucho 4" de ahi: un
// circuito arbitrario puede tener cualquier cantidad de redes ramificadas
// compartiendo un mismo hueco.
constexpr qreal kBusLaneSpacing = 16.0;

// Ordena `layerNodes` por el promedio de Y ya asignado a sus predecesores
// (los que caen en una capa YA posicionada, siempre menor por construccion
// de computeCircuitLayers()) -- reduce cruces sin necesitar un algoritmo
// completo de minimizacion (metodo de baricentro, una sola pasada). Un nodo
// sin predecesores posicionados conserva su orden relativo original
// (std::stable_sort) en vez de saltar al principio.
void orderByBarycenter(std::vector<uint32_t>& layerNodes, const std::map<uint32_t, std::vector<uint32_t>>& predecessors,
                        const std::map<uint32_t, qreal>& finalY) {
    std::stable_sort(layerNodes.begin(), layerNodes.end(), [&](uint32_t a, uint32_t b) {
        const auto baryOf = [&](uint32_t node) -> std::optional<qreal> {
            qreal sum = 0.0;
            int count = 0;
            for (uint32_t pred : predecessors.at(node)) {
                const auto it = finalY.find(pred);
                if (it != finalY.end()) {
                    sum += it->second;
                    ++count;
                }
            }
            if (count == 0) {
                return std::nullopt;
            }
            return sum / count;
        };
        const std::optional<qreal> baryA = baryOf(a);
        const std::optional<qreal> baryB = baryOf(b);
        if (baryA.has_value() && baryB.has_value()) {
            return *baryA < *baryB;
        }
        return false; // al menos uno sin predecesores posicionados: stable_sort conserva el orden
    });
}

} // namespace

void autoArrangeCircuit(CircuitScene& scene, QUndoStack& undoStack, const std::optional<std::set<uint32_t>>& componentScope) {
    CircuitDocument& document = *scene.document();

    std::set<uint32_t> scope;
    if (componentScope.has_value()) {
        scope = *componentScope;
    } else {
        for (uint32_t id : document.componentIds()) {
            scope.insert(id);
        }
    }
    if (scope.empty()) {
        return;
    }

    const std::map<uint32_t, int> layerOf = computeCircuitLayers(document, scope);

    // Predecesores por componente (recalculado aca, no reusado de
    // computeCircuitLayers(): esa funcion no expone su DAG interno a
    // proposito, para que su contrato publico sea solo "capa de cada
    // componente" -- lo que necesita el resto de este algoritmo). Se
    // descarta cualquier fuente cruda cuya capa no sea estrictamente menor a
    // la del propio nodo: son las mismas back-edges de realimentacion que
    // computeCircuitLayers() ya descarto para poder asignar capas -- sin
    // este filtro, una de esas aristas podia colarse aca como "predecesor" y
    // desordenar orderByBarycenter() con una vista del grafo inconsistente
    // con las capas ya calculadas.
    std::map<uint32_t, std::vector<uint32_t>> predecessors;
    for (uint32_t id : scope) {
        predecessors[id];
        const components::ComponentInstance* instance = document.component(id);
        if (instance == nullptr) {
            continue;
        }
        const auto& pins = instance->pins();
        for (std::size_t i = 0; i < pins.size(); ++i) {
            if (pins[i].direction == core::PinDirection::Output) {
                continue;
            }
            for (const PinRef& source : pinsOnNetOf(document, PinRef{id, static_cast<uint16_t>(i)})) {
                if (source.componentId != id && scope.contains(source.componentId) &&
                    layerOf.at(source.componentId) < layerOf.at(id)) {
                    predecessors[id].push_back(source.componentId);
                }
            }
        }
    }

    // Agrupa por capa, preservando el orden de insercion original de
    // componentIds() como orden de partida (estable) antes de reordenar.
    int maxLayer = 0;
    std::map<int, std::vector<uint32_t>> layers;
    for (uint32_t id : document.componentIds()) {
        const auto it = layerOf.find(id);
        if (it == layerOf.end()) {
            continue;
        }
        layers[it->second].push_back(id);
        maxLayer = std::max(maxLayer, it->second);
    }

    // Barrido izquierda->derecha: por cada capa, reordena por baricentro
    // (salvo la 0, que no tiene predecesores posicionados) y apila sus
    // componentes usando su tamano REAL en pantalla (via ComponentItem::
    // boundingRect(), solo disponible desde ac -- por eso este algoritmo
    // vive en editor/ y no en formats/, que es headless).
    std::map<uint32_t, qreal> finalX;
    std::map<uint32_t, qreal> finalY;
    qreal columnX = 0.0;
    for (int l = 0; l <= maxLayer; ++l) {
        auto layerIt = layers.find(l);
        if (layerIt == layers.end()) {
            continue;
        }
        std::vector<uint32_t>& nodes = layerIt->second;
        if (l > 0) {
            orderByBarycenter(nodes, predecessors, finalY);
        }
        qreal rowY = 0.0;
        qreal columnWidth = 0.0;
        for (uint32_t id : nodes) {
            ComponentItem* item = scene.componentItem(id);
            const QRectF bounds = item != nullptr ? item->boundingRect() : QRectF(0, 0, 100, 60);
            finalX[id] = columnX;
            finalY[id] = rowY;
            rowY += bounds.height() + kRowGap;
            columnWidth = std::max(columnWidth, bounds.width());
        }
        columnX += columnWidth + kColumnGap;
    }

    // Puntos de union "internos" (todas sus ramas terminan en un componente
    // del scope): se reubican en el hueco entre la capa de su fuente y la de
    // sus consumidores, en un carril de bus propio dentro de ese hueco.
    // Agrupados por hueco primero para poder repartirles un indice de carril
    // sin que se pisen entre si.
    struct JunctionPlan {
        uint32_t junctionId = 0;
        int gapAfterLayer = 0;
        qreal row = 0.0;
    };
    std::vector<JunctionPlan> junctionPlans;
    std::set<uint32_t> internalJunctions;
    for (uint32_t junctionId : document.junctionIds()) {
        const std::vector<PinRef> members = pinsReachableFromJunction(document, junctionId);
        if (members.empty() || std::any_of(members.begin(), members.end(),
                                            [&](const PinRef& p) { return !scope.contains(p.componentId); })) {
            continue;
        }
        // La fuente es el miembro cuyo pin es Output (si hay varias, se usa
        // la de menor capa -- caso raro, pero no debe romper el calculo).
        std::optional<int> sourceLayer;
        qreal sourceRow = 0.0;
        for (const PinRef& pin : members) {
            const components::ComponentInstance* instance = document.component(pin.componentId);
            if (instance == nullptr || pin.pinIndex >= instance->pins().size()) {
                continue;
            }
            if (instance->pins()[pin.pinIndex].direction == core::PinDirection::Output) {
                const int candidateLayer = layerOf.at(pin.componentId);
                if (!sourceLayer.has_value() || candidateLayer < *sourceLayer) {
                    sourceLayer = candidateLayer;
                    sourceRow = finalY.at(pin.componentId);
                }
            }
        }
        if (!sourceLayer.has_value()) {
            // Sin ningun Output entre sus miembros (raro): usa la menor capa
            // presente como aproximacion razonable.
            for (const PinRef& pin : members) {
                const int candidateLayer = layerOf.at(pin.componentId);
                if (!sourceLayer.has_value() || candidateLayer < *sourceLayer) {
                    sourceLayer = candidateLayer;
                    sourceRow = finalY.at(pin.componentId);
                }
            }
        }
        internalJunctions.insert(junctionId);
        junctionPlans.push_back(JunctionPlan{junctionId, *sourceLayer, sourceRow});
    }

    // Reparte un indice de carril (0..N-1, centrado) a cada union dentro de
    // su mismo hueco -- mismo criterio que busLaneX() en
    // KarnaughSynthesizer.cpp, generalizado a "cuantas uniones cayeron en
    // este hueco" en vez de "cuantas variables tiene el mapa de Karnaugh".
    std::map<int, std::vector<std::size_t>> byGap;
    for (std::size_t i = 0; i < junctionPlans.size(); ++i) {
        byGap[junctionPlans[i].gapAfterLayer].push_back(i);
    }
    std::map<uint32_t, QPointF> junctionPositions;
    for (auto& [gap, indices] : byGap) {
        // Centro del hueco: el final de la columna `gap` mas medio kColumnGap.
        qreal columnEndX = 0.0;
        {
            qreal x = 0.0;
            for (int l = 0; l <= gap; ++l) {
                auto it = layers.find(l);
                if (it == layers.end()) {
                    continue;
                }
                qreal width = 0.0;
                for (uint32_t id : it->second) {
                    ComponentItem* item = scene.componentItem(id);
                    const QRectF bounds = item != nullptr ? item->boundingRect() : QRectF(0, 0, 100, 60);
                    width = std::max(width, bounds.width());
                }
                x += width + kColumnGap;
            }
            columnEndX = x - kColumnGap / 2.0;
        }
        for (std::size_t rank = 0; rank < indices.size(); ++rank) {
            const qreal centered = static_cast<qreal>(rank) - (static_cast<qreal>(indices.size()) - 1.0) / 2.0;
            const JunctionPlan& plan = junctionPlans[indices[rank]];
            junctionPositions[plan.junctionId] = QPointF(columnEndX + centered * kBusLaneSpacing, plan.row);
        }
    }

    // Ahora si: aplica todo como un unico paso de undo.
    const std::size_t componentMoves = scope.size();
    const std::size_t junctionMoves = internalJunctions.size();
    std::size_t wireReroutes = 0;
    for (uint32_t wireId : document.wireIds()) {
        const WireConnection* w = document.wire(wireId);
        if (w == nullptr) {
            continue;
        }
        const bool aOk = w->a.isJunction ? internalJunctions.contains(w->a.id) || scope.contains(w->a.id)
                                          : scope.contains(w->a.id);
        const bool bOk = w->b.isJunction ? internalJunctions.contains(w->b.id) || scope.contains(w->b.id)
                                          : scope.contains(w->b.id);
        if (aOk && bOk) {
            ++wireReroutes;
        }
    }
    const std::size_t totalCommands = componentMoves + junctionMoves + wireReroutes;
    if (totalCommands == 0) {
        return;
    }
    if (totalCommands > 1) {
        undoStack.beginMacro("Auto-ordenar circuito");
    }

    for (uint32_t id : scope) {
        const QPointF oldPos = document.componentPlacement(id).position;
        const QPointF newPos(finalX.at(id), finalY.at(id));
        undoStack.push(new MoveComponentCommand(&document, id, oldPos, newPos));
    }
    for (uint32_t junctionId : internalJunctions) {
        const QPointF oldPos = document.junctionPosition(junctionId);
        undoStack.push(new MoveJunctionCommand(&document, junctionId, oldPos, junctionPositions.at(junctionId)));
    }

    const auto rowOf = [&](const WireEndpoint& e) -> qreal {
        if (e.isJunction) {
            const auto it = junctionPositions.find(e.id);
            return it != junctionPositions.end() ? it->second.y() : document.junctionPosition(e.id).y();
        }
        const auto it = finalY.find(e.id);
        return it != finalY.end() ? it->second : document.componentPlacement(e.id).position.y();
    };
    for (uint32_t wireId : document.wireIds()) {
        const WireConnection* w = document.wire(wireId);
        if (w == nullptr) {
            continue;
        }
        const bool aOk = w->a.isJunction ? internalJunctions.contains(w->a.id) || scope.contains(w->a.id)
                                          : scope.contains(w->a.id);
        const bool bOk = w->b.isJunction ? internalJunctions.contains(w->b.id) || scope.contains(w->b.id)
                                          : scope.contains(w->b.id);
        if (!aOk || !bOk) {
            continue;
        }
        std::vector<QPointF> newWaypoints;
        if (w->a.isJunction || w->b.isJunction) {
            // Rama de una red ramificada: un unico quiebre, vertical desde
            // el carril de la union hasta la fila del otro extremo, despues
            // horizontal hacia el -- igual patron que wireNet() en
            // KarnaughSynthesizer.cpp.
            const WireEndpoint& junctionEnd = w->a.isJunction ? w->a : w->b;
            const WireEndpoint& otherEnd = w->a.isJunction ? w->b : w->a;
            const auto posIt = junctionPositions.find(junctionEnd.id);
            const QPointF junctionPos = posIt != junctionPositions.end() ? posIt->second : document.junctionPosition(junctionEnd.id);
            const qreal otherRow = rowOf(otherEnd);
            if (std::abs(otherRow - junctionPos.y()) > 1.0) {
                newWaypoints = {QPointF(junctionPos.x(), otherRow)};
            }
        }
        // Cable directo pin-a-pin: sin waypoints, el codo automatico de
        // WireRouting.hpp alcanza solo.
        undoStack.push(new SetWireWaypointsCommand(&document, wireId, w->waypoints, newWaypoints));
    }

    if (totalCommands > 1) {
        undoStack.endMacro();
    }
}

} // namespace digitalforge::editor
