#include "UndoCommands.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace digitalforge::editor {

namespace {

// Para cada extremo-junction en `wiresBeingRemoved` que quedaria en grado 0
// una vez eliminados esos cables (es decir, todos sus cables adjuntos estan
// tambien en `wiresBeingRemoved`), devuelve la Junction (id + posicion) a
// restaurar en undo(). Debe llamarse ANTES de eliminar ningun cable -- el
// resultado se guarda en el constructor del comando junto con el resto de
// la identidad capturada.
std::vector<Junction> junctionsOrphanedByRemoving(CircuitDocument* document,
                                                    const std::vector<WireConnection>& wiresBeingRemoved) {
    std::set<uint32_t> removedWireIds;
    for (const WireConnection& w : wiresBeingRemoved) {
        removedWireIds.insert(w.id);
    }

    std::vector<Junction> orphaned;
    std::set<uint32_t> considered;
    for (const WireConnection& w : wiresBeingRemoved) {
        for (const WireEndpoint& endpoint : {w.a, w.b}) {
            if (!endpoint.isJunction || considered.contains(endpoint.id)) {
                continue;
            }
            considered.insert(endpoint.id);
            const std::vector<WireConnection> attached = document->wiresAttachedToJunction(endpoint.id);
            const bool survives = std::any_of(attached.begin(), attached.end(), [&](const WireConnection& aw) {
                return !removedWireIds.contains(aw.id);
            });
            if (!survives) {
                orphaned.push_back(Junction{endpoint.id, document->junctionPosition(endpoint.id)});
            }
        }
    }
    return orphaned;
}

} // namespace

// --- PlaceComponentCommand --------------------------------------------------

PlaceComponentCommand::PlaceComponentCommand(CircuitDocument* document, std::string typeId,
                                              components::PropertyMap overrides, ComponentPlacement placement,
                                              QUndoCommand* parent)
    : QUndoCommand(parent),
      document_(document),
      typeId_(std::move(typeId)),
      overrides_(std::move(overrides)),
      placement_(placement),
      componentId_(document->reserveComponentId()) {
    setText(QString("Colocar %1").arg(QString::fromStdString(typeId_)));
}

void PlaceComponentCommand::redo() { document_->addComponentWithId(componentId_, typeId_, overrides_, placement_); }

void PlaceComponentCommand::undo() { document_->removeComponent(componentId_); }

// --- DeleteComponentCommand --------------------------------------------------

DeleteComponentCommand::DeleteComponentCommand(CircuitDocument* document, uint32_t componentId, QUndoCommand* parent)
    : QUndoCommand(parent), document_(document), componentId_(componentId) {
    const components::ComponentInstance* instance = document->component(componentId);
    if (instance != nullptr) {
        typeId_ = instance->typeId();
        for (const components::PropertyDescriptor& descriptor : instance->definition().properties) {
            properties_[descriptor.id] = instance->property(descriptor.id);
        }
        placement_ = document->componentPlacement(componentId);
        attachedWires_ = document->wiresAttachedToComponent(componentId);
        orphanedJunctions_ = junctionsOrphanedByRemoving(document, attachedWires_);
    }
    setText(QString("Eliminar %1").arg(QString::fromStdString(typeId_)));
}

void DeleteComponentCommand::redo() { document_->removeComponent(componentId_); }

void DeleteComponentCommand::undo() {
    document_->addComponentWithId(componentId_, typeId_, properties_, placement_);
    for (const Junction& j : orphanedJunctions_) {
        document_->addJunctionWithId(j.id, j.position);
    }
    for (const WireConnection& w : attachedWires_) {
        document_->restoreWire(w);
    }
}

// --- MoveComponentCommand --------------------------------------------------

MoveComponentCommand::MoveComponentCommand(CircuitDocument* document, uint32_t componentId, QPointF oldPosition,
                                            QPointF newPosition, QUndoCommand* parent)
    : QUndoCommand(parent),
      document_(document),
      componentId_(componentId),
      oldPosition_(oldPosition),
      newPosition_(newPosition) {
    setText("Mover componente");
}

void MoveComponentCommand::redo() { apply(newPosition_); }

void MoveComponentCommand::undo() { apply(oldPosition_); }

void MoveComponentCommand::apply(QPointF position) {
    ComponentPlacement placement = document_->componentPlacement(componentId_);
    placement.position = position;
    document_->setComponentPlacement(componentId_, placement);
}

int MoveComponentCommand::id() const { return 1001; }

bool MoveComponentCommand::mergeWith(const QUndoCommand* other) {
    const auto* move = dynamic_cast<const MoveComponentCommand*>(other);
    if (move == nullptr || move->componentId_ != componentId_) {
        return false;
    }
    newPosition_ = move->newPosition_;
    return true;
}

// --- MoveJunctionCommand --------------------------------------------------

MoveJunctionCommand::MoveJunctionCommand(CircuitDocument* document, uint32_t junctionId, QPointF oldPosition,
                                          QPointF newPosition, QUndoCommand* parent)
    : QUndoCommand(parent),
      document_(document),
      junctionId_(junctionId),
      oldPosition_(oldPosition),
      newPosition_(newPosition) {
    setText("Mover punto de union");
}

void MoveJunctionCommand::redo() { apply(newPosition_); }

void MoveJunctionCommand::undo() { apply(oldPosition_); }

void MoveJunctionCommand::apply(QPointF position) { document_->setJunctionPosition(junctionId_, position); }

int MoveJunctionCommand::id() const { return 1004; }

bool MoveJunctionCommand::mergeWith(const QUndoCommand* other) {
    const auto* move = dynamic_cast<const MoveJunctionCommand*>(other);
    if (move == nullptr || move->junctionId_ != junctionId_) {
        return false;
    }
    newPosition_ = move->newPosition_;
    return true;
}

// --- RotateComponentCommand --------------------------------------------------

RotateComponentCommand::RotateComponentCommand(CircuitDocument* document, uint32_t componentId,
                                                int oldRotationDegrees, int newRotationDegrees, QUndoCommand* parent)
    : QUndoCommand(parent),
      document_(document),
      componentId_(componentId),
      oldRotationDegrees_(oldRotationDegrees),
      newRotationDegrees_(newRotationDegrees) {
    setText("Rotar componente");
}

void RotateComponentCommand::redo() { apply(newRotationDegrees_); }

void RotateComponentCommand::undo() { apply(oldRotationDegrees_); }

void RotateComponentCommand::apply(int rotationDegrees) {
    ComponentPlacement placement = document_->componentPlacement(componentId_);
    placement.rotationDegrees = rotationDegrees;
    document_->setComponentPlacement(componentId_, placement);
}

// --- SetZOrderCommand --------------------------------------------------

SetZOrderCommand::SetZOrderCommand(CircuitDocument* document, uint32_t componentId, int oldZOrder, int newZOrder,
                                    QUndoCommand* parent)
    : QUndoCommand(parent),
      document_(document),
      componentId_(componentId),
      oldZOrder_(oldZOrder),
      newZOrder_(newZOrder) {
    setText("Cambiar orden de apilado");
}

void SetZOrderCommand::redo() { apply(newZOrder_); }

void SetZOrderCommand::undo() { apply(oldZOrder_); }

void SetZOrderCommand::apply(int zOrder) {
    ComponentPlacement placement = document_->componentPlacement(componentId_);
    placement.zOrder = zOrder;
    document_->setComponentPlacement(componentId_, placement);
}

// --- AddWireCommand --------------------------------------------------

AddWireCommand::AddWireCommand(CircuitDocument* document, WireEndpoint a, WireEndpoint b, std::vector<QPointF> waypoints,
                               QUndoCommand* parent)
    : QUndoCommand(parent),
      document_(document),
      wireId_(document->reserveWireId()),
      a_(a),
      b_(b),
      waypoints_(std::move(waypoints)) {
    setText("Anadir cable");
}

void AddWireCommand::redo() { document_->restoreWire(WireConnection{wireId_, a_, b_, waypoints_}); }

void AddWireCommand::undo() { document_->removeWire(wireId_); }

// --- AddJunctionCommand --------------------------------------------------

AddJunctionCommand::AddJunctionCommand(CircuitDocument* document, QPointF position, QUndoCommand* parent)
    : QUndoCommand(parent), document_(document), junctionId_(document->reserveJunctionId()), position_(position) {
    setText("Anadir punto de union");
}

void AddJunctionCommand::redo() { document_->addJunctionWithId(junctionId_, position_); }

void AddJunctionCommand::undo() { document_->removeJunction(junctionId_); }

// --- DeleteWireCommand --------------------------------------------------

DeleteWireCommand::DeleteWireCommand(CircuitDocument* document, uint32_t wireId, QUndoCommand* parent)
    : QUndoCommand(parent), document_(document) {
    const WireConnection* w = document->wire(wireId);
    if (w != nullptr) {
        wire_ = *w;
        orphanedJunctions_ = junctionsOrphanedByRemoving(document, {wire_});
    }
    setText("Eliminar cable");
}

void DeleteWireCommand::redo() { document_->removeWire(wire_.id); }

void DeleteWireCommand::undo() {
    for (const Junction& j : orphanedJunctions_) {
        document_->addJunctionWithId(j.id, j.position);
    }
    document_->restoreWire(wire_);
}

// --- SplitWireCommand --------------------------------------------------

SplitWireCommand::SplitWireCommand(CircuitDocument* document, uint32_t existingWireId, QPointF splitPosition,
                                    QUndoCommand* parent)
    : QUndoCommand(parent),
      document_(document),
      originalWireId_(existingWireId),
      splitPosition_(splitPosition),
      junctionId_(document->reserveJunctionId()),
      wireId1_(document->reserveWireId()),
      wireId2_(document->reserveWireId()) {
    const WireConnection* w = document->wire(existingWireId);
    if (w != nullptr) {
        originalWire_ = *w;
    }
    setText("Derivar cable");
}

void SplitWireCommand::redo() {
    document_->addJunctionWithId(junctionId_, splitPosition_);
    document_->restoreWire(WireConnection{wireId1_, originalWire_.a, WireEndpoint::junction(junctionId_), {}});
    document_->restoreWire(WireConnection{wireId2_, WireEndpoint::junction(junctionId_), originalWire_.b, {}});
    document_->removeWire(originalWireId_);
}

void SplitWireCommand::undo() {
    document_->removeWire(wireId2_);
    document_->removeWire(wireId1_); // deja al junction en grado 0 -> se elimina en cascada
    document_->restoreWire(originalWire_);
}

// --- SetWireWaypointsCommand --------------------------------------------------

SetWireWaypointsCommand::SetWireWaypointsCommand(CircuitDocument* document, uint32_t wireId,
                                                  std::vector<QPointF> oldWaypoints, std::vector<QPointF> newWaypoints,
                                                  QUndoCommand* parent)
    : QUndoCommand(parent),
      document_(document),
      wireId_(wireId),
      oldWaypoints_(std::move(oldWaypoints)),
      newWaypoints_(std::move(newWaypoints)) {
    setText("Redefinir trazado de cable");
}

void SetWireWaypointsCommand::redo() { apply(newWaypoints_); }

void SetWireWaypointsCommand::undo() { apply(oldWaypoints_); }

void SetWireWaypointsCommand::apply(const std::vector<QPointF>& waypoints) {
    document_->setWireWaypoints(wireId_, waypoints);
}

int SetWireWaypointsCommand::id() const { return 1003; }

bool SetWireWaypointsCommand::mergeWith(const QUndoCommand* other) {
    const auto* set = dynamic_cast<const SetWireWaypointsCommand*>(other);
    if (set == nullptr || set->wireId_ != wireId_) {
        return false;
    }
    newWaypoints_ = set->newWaypoints_;
    return true;
}

// --- RetargetWireEndpointCommand --------------------------------------------------

RetargetWireEndpointCommand::RetargetWireEndpointCommand(CircuitDocument* document, uint32_t wireId, bool endIsA,
                                                         WireEndpoint newEndpoint, QUndoCommand* parent)
    : QUndoCommand(parent), document_(document), wireId_(wireId), endIsA_(endIsA), newEndpoint_(newEndpoint) {
    const WireConnection* w = document->wire(wireId);
    if (w != nullptr) {
        oldEndpoint_ = endIsA ? w->a : w->b;
        // Si el extremo viejo es un punto de union del que este es el unico
        // cable, la reconexion lo dejara huerfano y retargetWire() lo
        // eliminara -- se captura aca para poder recrearlo en undo().
        if (oldEndpoint_.isJunction && document->wiresAttachedToJunction(oldEndpoint_.id).size() == 1) {
            orphanedJunction_ = Junction{oldEndpoint_.id, document->junctionPosition(oldEndpoint_.id)};
        }
    }
    setText("Reconectar extremo de cable");
}

void RetargetWireEndpointCommand::redo() { document_->retargetWire(wireId_, endIsA_, newEndpoint_); }

void RetargetWireEndpointCommand::undo() {
    if (orphanedJunction_.has_value()) {
        document_->addJunctionWithId(orphanedJunction_->id, orphanedJunction_->position);
    }
    document_->retargetWire(wireId_, endIsA_, oldEndpoint_);
}

// --- ChangePropertyCommand --------------------------------------------------

ChangePropertyCommand::ChangePropertyCommand(CircuitDocument* document, uint32_t componentId, std::string propertyId,
                                              components::PropertyValue oldValue, components::PropertyValue newValue,
                                              QUndoCommand* parent)
    : QUndoCommand(parent),
      document_(document),
      componentId_(componentId),
      propertyId_(std::move(propertyId)),
      oldValue_(std::move(oldValue)),
      newValue_(std::move(newValue)) {
    setText(QString("Cambiar %1").arg(QString::fromStdString(propertyId_)));
}

void ChangePropertyCommand::redo() {
    // setProperty() decide recien adentro que cables quedan invalidos (segun
    // como cambie la cantidad de pines), asi que los candidatos a junction
    // huerfano solo se pueden acotar por adelantado (todo lo que hoy toca a
    // este componente) y confirmar despues comparando contra lo que
    // sobrevivio.
    std::map<uint32_t, QPointF> candidateJunctions;
    for (const WireConnection& w : document_->wiresAttachedToComponent(componentId_)) {
        for (const WireEndpoint& endpoint : {w.a, w.b}) {
            if (endpoint.isJunction) {
                candidateJunctions[endpoint.id] = document_->junctionPosition(endpoint.id);
            }
        }
    }

    removedWires_ = document_->setProperty(componentId_, propertyId_, newValue_);

    const std::vector<uint32_t> survivingJunctions = document_->junctionIds();
    orphanedJunctions_.clear();
    for (const auto& [id, position] : candidateJunctions) {
        if (std::find(survivingJunctions.begin(), survivingJunctions.end(), id) == survivingJunctions.end()) {
            orphanedJunctions_.push_back(Junction{id, position});
        }
    }
}

void ChangePropertyCommand::undo() {
    document_->setProperty(componentId_, propertyId_, oldValue_);
    for (const Junction& j : orphanedJunctions_) {
        document_->addJunctionWithId(j.id, j.position);
    }
    for (const WireConnection& w : removedWires_) {
        document_->restoreWire(w);
    }
    removedWires_.clear();
    orphanedJunctions_.clear();
}

} // namespace digitalforge::editor
