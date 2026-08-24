#include "CircuitLayoutGraph.hpp"

#include "CircuitAutoLayout.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

#include "components/ComponentInstance.hpp"
#include "core/Pin.hpp"

namespace digitalforge::editor {

std::vector<PinRef> pinsOnNetOf(const CircuitDocument& document, PinRef startPin) {
    std::vector<PinRef> pins;
    const WireEndpoint startEndpoint{startPin};
    std::vector<uint32_t> junctionsToVisit;
    std::set<uint32_t> visitedJunctions;

    // No hay una API ya expuesta que devuelva "los cables que tocan este pin
    // puntual" (wiresAttachedToComponent() devuelve TODOS los cables del
    // componente, sin filtrar por pin), asi que el primer salto se resuelve
    // a mano recorriendo wireIds(); de ahi en adelante, wiresAttachedToJunction()
    // ya es preciso.
    for (uint32_t wireId : document.wireIds()) {
        const WireConnection* w = document.wire(wireId);
        if (w == nullptr) {
            continue;
        }
        WireEndpoint other;
        if (w->a == startEndpoint) {
            other = w->b;
        } else if (w->b == startEndpoint) {
            other = w->a;
        } else {
            continue;
        }
        if (other.isJunction) {
            if (visitedJunctions.insert(other.id).second) {
                junctionsToVisit.push_back(other.id);
            }
        } else {
            pins.push_back(other.pin());
        }
    }
    while (!junctionsToVisit.empty()) {
        const uint32_t junctionId = junctionsToVisit.back();
        junctionsToVisit.pop_back();
        for (const WireConnection& w : document.wiresAttachedToJunction(junctionId)) {
            for (const WireEndpoint& e : {w.a, w.b}) {
                if (e.isJunction) {
                    if (e.id != junctionId && visitedJunctions.insert(e.id).second) {
                        junctionsToVisit.push_back(e.id);
                    }
                } else {
                    pins.push_back(e.pin());
                }
            }
        }
    }
    return pins;
}

std::vector<PinRef> pinsReachableFromJunction(const CircuitDocument& document, uint32_t junctionId) {
    std::vector<PinRef> pins;
    std::vector<uint32_t> toVisit{junctionId};
    std::set<uint32_t> visited;
    while (!toVisit.empty()) {
        const uint32_t current = toVisit.back();
        toVisit.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        for (const WireConnection& w : document.wiresAttachedToJunction(current)) {
            for (const WireEndpoint& e : {w.a, w.b}) {
                if (e.isJunction) {
                    if (e.id != current) {
                        toVisit.push_back(e.id);
                    }
                } else {
                    pins.push_back(e.pin());
                }
            }
        }
    }
    return pins;
}

std::map<uint32_t, int> computeCircuitLayers(const CircuitDocument& document,
                                              const std::optional<std::set<uint32_t>>& scope) {
    std::set<uint32_t> nodes;
    if (scope.has_value()) {
        nodes = *scope;
    } else {
        for (uint32_t id : document.componentIds()) {
            nodes.insert(id);
        }
    }

    // Grafo de dependencia: componente-fuente -> componente-consumidor por
    // cada pin Output conectado a un pin no-Output de otro componente del
    // scope (directo o via puntos de union).
    std::map<uint32_t, std::vector<uint32_t>> adjacency;
    for (uint32_t id : nodes) {
        adjacency[id]; // asegura una entrada aunque no tenga ninguna arista
    }
    for (uint32_t id : nodes) {
        const components::ComponentInstance* instance = document.component(id);
        if (instance == nullptr) {
            continue;
        }
        const auto& pins = instance->pins();
        for (std::size_t i = 0; i < pins.size(); ++i) {
            if (pins[i].direction != core::PinDirection::Output) {
                continue;
            }
            for (const PinRef& target : pinsOnNetOf(document, PinRef{id, static_cast<uint16_t>(i)})) {
                if (target.componentId != id && nodes.contains(target.componentId)) {
                    adjacency[id].push_back(target.componentId);
                }
            }
        }
    }

    // DFS blanco/gris/negro: descarta las aristas "gris" (back-edges, es
    // decir realimentacion -- un latch/flip-flop armado a mano con
    // compuertas cruzadas) para que el grafo que queda sea un DAG y se le
    // pueda asignar una capa por camino mas largo sin ciclos infinitos.
    enum class Color : uint8_t { White, Gray, Black };
    std::map<uint32_t, Color> color;
    for (uint32_t id : nodes) {
        color[id] = Color::White;
    }
    std::map<uint32_t, std::vector<uint32_t>> acyclic;
    for (uint32_t id : nodes) {
        acyclic[id];
    }
    // Pila explicita (nodo + proximo indice a examinar en su lista de
    // adyacencia) en vez de una lambda recursiva: una cadena de dependencia
    // muy larga (miles de componentes en serie, plausible via importacion)
    // recursaria un stack frame nativo por salto y arriesgaba un
    // desbordamiento de pila. Misma logica de colores/descarte de back-edges
    // que un DFS recursivo, solo que el "call stack" es explicito.
    struct Frame {
        uint32_t node;
        std::size_t nextIndex = 0;
    };
    std::vector<Frame> stack;
    for (uint32_t id : nodes) {
        if (color[id] != Color::White) {
            continue;
        }
        color[id] = Color::Gray;
        stack.push_back(Frame{id, 0});
        while (!stack.empty()) {
            Frame& frame = stack.back();
            const std::vector<uint32_t>& neighbors = adjacency[frame.node];
            if (frame.nextIndex >= neighbors.size()) {
                color[frame.node] = Color::Black;
                stack.pop_back();
                continue;
            }
            const uint32_t next = neighbors[frame.nextIndex++];
            if (color[next] == Color::White) {
                acyclic[frame.node].push_back(next);
                color[next] = Color::Gray;
                stack.push_back(Frame{next, 0});
            } else if (color[next] == Color::Black) {
                acyclic[frame.node].push_back(next); // arista hacia adelante/cruzada: valida en un DAG
            }
            // Color::Gray: back-edge -- se descarta, rompe el ciclo.
        }
    }

    // Capa por camino mas largo (orden topologico de Kahn sobre el DAG ya
    // sin ciclos): capa 0 = sin predecesores; cualquier otro nodo, 1 + la
    // mayor capa entre sus predecesores.
    std::map<uint32_t, std::vector<uint32_t>> predecessors;
    for (uint32_t id : nodes) {
        predecessors[id];
    }
    std::map<uint32_t, int> inDegree;
    for (uint32_t id : nodes) {
        inDegree[id] = 0;
    }
    for (const auto& [from, tos] : acyclic) {
        for (uint32_t to : tos) {
            predecessors[to].push_back(from);
            ++inDegree[to];
        }
    }

    std::map<uint32_t, int> layer;
    std::vector<uint32_t> queue;
    for (uint32_t id : nodes) {
        layer[id] = 0;
        if (inDegree[id] == 0) {
            queue.push_back(id);
        }
    }
    std::size_t cursor = 0;
    while (cursor < queue.size()) {
        const uint32_t node = queue[cursor++];
        for (uint32_t next : acyclic[node]) {
            layer[next] = std::max(layer[next], layer[node] + 1);
            if (--inDegree[next] == 0) {
                queue.push_back(next);
            }
        }
    }
    return layer;
}

} // namespace digitalforge::editor
