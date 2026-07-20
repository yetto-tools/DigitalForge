#include "WireTool.hpp"

#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QString>
#include <QUndoStack>

#include "CircuitScene.hpp"
#include "JunctionItem.hpp"
#include "PinItem.hpp"
#include "UndoCommands.hpp"
#include "WireItem.hpp"

namespace digitalforge::editor {

namespace {

bool isSameConnectionPoint(const WireGestureEndpoint& a, const WireGestureEndpoint& b) {
    if (a.pin != nullptr && b.pin != nullptr) {
        return a.pin->componentId() == b.pin->componentId() && a.pin->pinIndex() == b.pin->pinIndex();
    }
    if (a.junction != nullptr && b.junction != nullptr) {
        return a.junction->junctionId() == b.junction->junctionId();
    }
    if (a.pendingBranchWire != nullptr && b.pendingBranchWire != nullptr) {
        // Misma derivacion pendiente en ambos extremos: dividir el mismo
        // cable dos veces en un solo gesto es ambiguo (y el segundo
        // wireId() ya no seria valido tras la primera division) -- se
        // cancela en vez de intentar resolverlo.
        return a.pendingBranchWire == b.pendingBranchWire;
    }
    return false;
}

} // namespace

WireTool::WireTool(CircuitScene* scene, CircuitDocument* document, QUndoStack* undoStack)
    : scene_(scene), document_(document), undoStack_(undoStack) {}

WireGestureEndpoint WireTool::hitTest(QPointF scenePos) const {
    WireGestureEndpoint hit;
    if (PinItem* pin = scene_->pinItemAt(scenePos)) {
        hit.pin = pin;
        hit.anchorPos = pin->scenePos();
        return hit;
    }
    if (JunctionItem* junction = scene_->junctionItemAt(scenePos)) {
        hit.junction = junction;
        hit.anchorPos = junction->scenePos();
        return hit;
    }
    if (WireItem* wire = scene_->wireItemAt(scenePos)) {
        hit.pendingBranchWire = wire;
        // Proyectado sobre el trazado real del cable (no el punto crudo del
        // cursor): el punto de union que eventualmente cree SplitWireCommand
        // debe caer siempre exactamente sobre el cable, nunca desalineado.
        hit.anchorPos = wire->nearestPointOnPath(scenePos);
        return hit;
    }
    return hit;
}

void WireTool::press(QGraphicsSceneMouseEvent* event) {
    if (!document_->requireEditable(QStringLiteral("conectar un cable"))) {
        return;
    }
    start_ = hitTest(event->scenePos());
    if (start_.empty()) {
        return;
    }
    previewLine_ = scene_->addLine(QLineF(start_.anchorPos, start_.anchorPos), QPen(Qt::darkGray, 1, Qt::DashLine));
    previewLine_->setZValue(100.0);
}

void WireTool::move(QGraphicsSceneMouseEvent* event) {
    if (!start_.empty() && previewLine_ != nullptr) {
        previewLine_->setLine(QLineF(start_.anchorPos, event->scenePos()));
    }
}

void WireTool::release(QGraphicsSceneMouseEvent* event) {
    const WireGestureEndpoint start = start_;
    cancel();
    if (start.empty()) {
        return;
    }
    const WireGestureEndpoint end = hitTest(event->scenePos());
    if (end.empty() || isSameConnectionPoint(start, end)) {
        return;
    }

    // Si alguno de los dos extremos es una derivacion pendiente, hace falta
    // dividir el/los cable(s) correspondiente(s) antes de poder conectar --
    // todo eso se envuelve en un solo macro de undo, para que el gesto
    // completo se deshaga/rehaga de un solo paso.
    const bool needsMacro = start.pendingBranchWire != nullptr || end.pendingBranchWire != nullptr;
    if (needsMacro) {
        undoStack_->beginMacro("Derivar cable");
    }
    const WireEndpoint a = resolveEndpoint(start);
    const WireEndpoint b = resolveEndpoint(end);
    undoStack_->push(new AddWireCommand(document_, a, b));
    if (needsMacro) {
        undoStack_->endMacro();
    }
}

WireEndpoint WireTool::resolveEndpoint(const WireGestureEndpoint& hit) {
    if (hit.pin != nullptr) {
        return WireEndpoint(PinRef{hit.pin->componentId(), hit.pin->pinIndex()});
    }
    if (hit.junction != nullptr) {
        return WireEndpoint::junction(hit.junction->junctionId());
    }
    auto* splitCommand = new SplitWireCommand(document_, hit.pendingBranchWire->wireId(), hit.anchorPos);
    undoStack_->push(splitCommand);
    return WireEndpoint::junction(splitCommand->junctionId());
}

void WireTool::cancel() {
    if (previewLine_ != nullptr) {
        scene_->removeItem(previewLine_);
        delete previewLine_;
        previewLine_ = nullptr;
    }
    start_ = WireGestureEndpoint{};
}

} // namespace digitalforge::editor
