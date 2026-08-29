#include "SelectionTool.hpp"

#include <QGraphicsSceneMouseEvent>
#include <QUndoStack>

#include "CircuitDocument.hpp"
#include "CircuitScene.hpp"
#include "ComponentItem.hpp"
#include "GridSnap.hpp"
#include "JunctionItem.hpp"
#include "UndoCommands.hpp"
#include "WireItem.hpp"

namespace digitalforge::editor {

namespace {
// Un extremo cuenta como "en movimiento" si su componente/union esta
// seleccionado (todo lo seleccionado se arrastra junto via ItemIsMovable) --
// mismo criterio que usa deltaForWire() para decidir si un extremo pertenece
// al gesto en curso.
bool anchorIsMoving(const WireAnchor& anchor) {
    if (anchor.component != nullptr) return anchor.component->isSelected();
    if (anchor.junction != nullptr) return anchor.junction->isSelected();
    return false;
}
} // namespace

SelectionTool::SelectionTool(CircuitScene* scene, CircuitDocument* document, QUndoStack* undoStack)
    : scene_(scene), document_(document), undoStack_(undoStack) {}

void SelectionTool::afterPress(QGraphicsSceneMouseEvent* event) {
    dragStartPositions_.clear();
    junctionDragStartPositions_.clear();
    wireStartWaypoints_.clear();
    resetWireIds_.clear();
    pressScenePos_ = event->scenePos();

    const auto collectWire = [this](WireItem* wire) {
        const uint32_t wireId = wire->wireId();
        if (wireStartWaypoints_.count(wireId) != 0) {
            return; // ya capturado (el otro extremo tambien esta en la seleccion)
        }
        const WireConnection* connection = document_->wire(wireId);
        if (connection == nullptr || connection->waypoints.empty()) {
            return; // sin waypoints propios: se auto-rutea solo, nada que arrastrar
        }
        // Trasladar el trazado en bloque (el camino de siempre, mas abajo) solo
        // tiene sentido si los DOS extremos se mueven juntos como un grupo
        // rigido -- si uno se queda quieto, un quiebre pensado para la
        // geometria vieja ya no tiene por que seguir sirviendo ahi, y
        // trasladarlo igual lo deformaba fuerte (el defecto reportado: mover
        // un componente con una conexion ya trazada dejaba el cable
        // irreconocible). Para ese caso se resetea a auto-ruteo en
        // afterRelease() en vez de arrastrarlo -- el resultado puede verse
        // asimetrico respecto de como estaba antes, que es justamente lo que
        // se pidio en vez de forzar la forma vieja sobre una posicion nueva.
        if (anchorIsMoving(wire->anchorA()) && anchorIsMoving(wire->anchorB())) {
            wireStartWaypoints_[wireId] = connection->waypoints;
        } else {
            resetWireIds_.push_back(wireId);
        }
    };

    for (QGraphicsItem* item : scene_->selectedItems()) {
        if (auto* component = dynamic_cast<ComponentItem*>(item)) {
            dragStartPositions_[component->componentId()] = document_->componentPlacement(component->componentId()).position;
            for (WireItem* wire : component->attachedWires()) {
                collectWire(wire);
            }
        } else if (auto* junction = dynamic_cast<JunctionItem*>(item)) {
            junctionDragStartPositions_[junction->junctionId()] = document_->junctionPosition(junction->junctionId());
            for (WireItem* wire : junction->attachedWires()) {
                collectWire(wire);
            }
        }
    }
}

std::optional<QPointF> SelectionTool::deltaForWire(const WireItem* wire) const {
    const WireAnchor& a = wire->anchorA();
    if (a.component != nullptr) {
        const auto it = dragStartPositions_.find(a.component->componentId());
        if (it != dragStartPositions_.end()) {
            return a.component->pos() - it->second;
        }
    }
    if (a.junction != nullptr) {
        const auto it = junctionDragStartPositions_.find(a.junction->junctionId());
        if (it != junctionDragStartPositions_.end()) {
            return a.junction->pos() - it->second;
        }
    }
    const WireAnchor& b = wire->anchorB();
    if (b.component != nullptr) {
        const auto it = dragStartPositions_.find(b.component->componentId());
        if (it != dragStartPositions_.end()) {
            return b.component->pos() - it->second;
        }
    }
    if (b.junction != nullptr) {
        const auto it = junctionDragStartPositions_.find(b.junction->junctionId());
        if (it != junctionDragStartPositions_.end()) {
            return b.junction->pos() - it->second;
        }
    }
    return std::nullopt;
}

void SelectionTool::afterMove(QGraphicsSceneMouseEvent* event) {
    // Arrastre de mas de un item: Qt mueve cada ComponentItem/JunctionItem
    // seleccionado por separado, y cada uno snapea su propia posicion a la
    // grilla de forma incremental en ComponentItem::itemChange() (cuadro a
    // cuadro del mouse, no de una sola vez sobre el delta total). En un
    // arrastre largo eso puede redondear distinto de un item a otro y el
    // grupo termina desincronizado entre si (el defecto reportado: dos
    // componentes que deberian moverse identico terminan a destinos
    // distintos, y los cables entre medio quedan mal). Con mas de un item
    // seleccionado se pisa esa posicion con un unico delta de grupo -
    // calculado sobre el desplazamiento total desde el press y snapeado una
    // sola vez - igual para todos, como en draw.io/Logisim.
    if (dragStartPositions_.size() + junctionDragStartPositions_.size() > 1) {
        QPointF delta = event->scenePos() - pressScenePos_;
        if (scene_->snapToGridEnabled()) {
            delta = snapToGrid(delta, ComponentItem::kGridSize);
        }
        for (const auto& [componentId, startPos] : dragStartPositions_) {
            ComponentItem* item = scene_->componentItem(componentId);
            if (item != nullptr) {
                item->setPos(startPos + delta);
            }
        }
        for (const auto& [junctionId, startPos] : junctionDragStartPositions_) {
            JunctionItem* item = scene_->junctionItem(junctionId);
            if (item != nullptr) {
                item->setPos(startPos + delta);
            }
        }
    }

    for (const auto& [wireId, startWaypoints] : wireStartWaypoints_) {
        WireItem* wire = scene_->wireItem(wireId);
        if (wire != nullptr) {
            wire->setLiveWaypointOffset(deltaForWire(wire));
        }
    }
}

void SelectionTool::afterRelease(QGraphicsSceneMouseEvent*) {
    for (const auto& [componentId, startPos] : dragStartPositions_) {
        ComponentItem* item = scene_->componentItem(componentId);
        if (item == nullptr) {
            continue;
        }
        const QPointF currentPos = item->pos();
        if (currentPos != startPos) {
            undoStack_->push(new MoveComponentCommand(document_, componentId, startPos, currentPos));
        }
    }

    for (const auto& [junctionId, startPos] : junctionDragStartPositions_) {
        JunctionItem* item = scene_->junctionItem(junctionId);
        if (item == nullptr) {
            continue;
        }
        const QPointF currentPos = item->pos();
        if (currentPos != startPos) {
            undoStack_->push(new MoveJunctionCommand(document_, junctionId, startPos, currentPos));
        }
    }

    // Los waypoints se comprometen recien aca (no en cada afterMove): igual
    // que MoveComponentCommand/MoveJunctionCommand arriba, un solo comando
    // deshacible por gesto de arrastre, no uno por frame de mouse.
    for (const auto& [wireId, startWaypoints] : wireStartWaypoints_) {
        WireItem* wire = scene_->wireItem(wireId);
        if (wire == nullptr) {
            continue;
        }
        const std::optional<QPointF> delta = deltaForWire(wire);
        wire->setLiveWaypointOffset(std::nullopt);
        if (delta.has_value() && *delta != QPointF(0.0, 0.0)) {
            std::vector<QPointF> newWaypoints = startWaypoints;
            for (QPointF& point : newWaypoints) {
                point += *delta;
            }
            undoStack_->push(new SetWireWaypointsCommand(document_, wireId, startWaypoints, newWaypoints));
        }
    }

    // Cables con un solo extremo en movimiento (ver collectWire() en
    // afterPress()): se resetean a auto-ruteo si el gesto de verdad los movio
    // (un clic sin arrastre no debe generar un comando de undo de la nada).
    for (const uint32_t wireId : resetWireIds_) {
        WireItem* wire = scene_->wireItem(wireId);
        if (wire == nullptr) {
            continue;
        }
        const std::optional<QPointF> delta = deltaForWire(wire);
        if (!delta.has_value() || *delta == QPointF(0.0, 0.0)) {
            continue;
        }
        const WireConnection* connection = document_->wire(wireId);
        if (connection == nullptr || connection->waypoints.empty()) {
            continue;
        }
        undoStack_->push(new SetWireWaypointsCommand(document_, wireId, connection->waypoints, {}));
    }

    dragStartPositions_.clear();
    junctionDragStartPositions_.clear();
    wireStartWaypoints_.clear();
    resetWireIds_.clear();
}

} // namespace digitalforge::editor
