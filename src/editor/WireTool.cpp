#include "WireTool.hpp"

#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QString>
#include <QUndoStack>
#include <utility>
#include <vector>

#include "CircuitScene.hpp"
#include "ComponentItem.hpp"
#include "GridSnap.hpp"
#include "JunctionItem.hpp"
#include "PinItem.hpp"
#include "UndoCommands.hpp"
#include "WireItem.hpp"
#include "WireRouting.hpp"

namespace digitalforge::editor {

namespace {

// El extremo del preview sigue al cursor, pero snappeado a la grilla cuando
// no cae sobre un destino concreto -- asi la ruta ortogonal previa coincide
// con lo que quedara al soltar (los pines ya estan sobre la grilla).
QPointF snapCursor(const CircuitScene* scene, QPointF scenePos) {
    if (scene != nullptr && scene->snapToGridEnabled()) {
        return snapToGrid(scenePos, ComponentItem::kGridSize);
    }
    return scenePos;
}

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
    const QPointF pos = event->scenePos();

    if (!drawing_) {
        // Inicio de un trazado: solo desde un pin/union/cuerpo de cable.
        const WireGestureEndpoint start = hitTest(pos);
        if (start.empty()) {
            return;
        }
        drawing_ = true;
        startHit_ = start;
        points_ = {start.anchorPos};
        pressPos_ = pos;
        previewPath_ = scene_->addPath(QPainterPath(), QPen(Qt::darkGray, 1, Qt::DashLine));
        previewPath_->setZValue(100.0);
        return;
    }

    // Ya hay un trazado en curso: cada clic posterior fija un quiebre, salvo
    // que caiga sobre un destino valido distinto del inicio, en cuyo caso
    // cierra el cable.
    const WireGestureEndpoint hit = hitTest(pos);
    if (!hit.empty() && !isSameConnectionPoint(startHit_, hit)) {
        commitTo(hit, pos);
        return;
    }
    const QPointF bend = hit.empty() ? snapCursor(scene_, pos) : hit.anchorPos;
    if (points_.empty() || bend != points_.back()) {
        points_.push_back(bend);
    }
    pressPos_ = pos;
}

void WireTool::move(QGraphicsSceneMouseEvent* event) {
    if (!drawing_) {
        return;
    }
    updatePreview(event->scenePos());
}

void WireTool::updatePreview(QPointF cursorScenePos) {
    if (previewPath_ == nullptr) {
        return;
    }
    const WireGestureEndpoint end = hitTest(cursorScenePos);
    const QPointF endPos = end.empty() ? snapCursor(scene_, cursorScenePos) : end.anchorPos;
    std::vector<QPointF> points = points_;
    points.push_back(endPos);
    previewPath_->setPath(buildOrthogonalPath(points, scene_->componentObstacleRects({})));

    // Color del preview segun el destino bajo el cursor: verde si soltar ahi
    // haria una conexion valida, rojo si es un destino invalido (el mismo
    // punto de partida), gris neutro si todavia no hay destino (vacio/grilla).
    QPen pen;
    if (end.empty()) {
        pen = QPen(Qt::darkGray, 1, Qt::DashLine);
    } else if (isSameConnectionPoint(startHit_, end)) {
        pen = QPen(QColor(220, 60, 60), 2, Qt::DashLine);
    } else {
        pen = QPen(QColor(40, 180, 70), 2);
    }
    previewPath_->setPen(pen);
}

void WireTool::release(QGraphicsSceneMouseEvent* event) {
    if (!drawing_) {
        return;
    }
    // Solo el primer segmento (sin quiebres fijados todavia) conserva el gesto
    // clasico de "arrastrar de A a B": si el usuario arrastro lo suficiente,
    // el cable se cierra al soltar. Un clic simple (sin arrastre) deja el
    // trazado abierto para ir agregando quiebres con clics sucesivos.
    const bool isFirstSegment = points_.size() == 1;
    const bool wasDrag = (event->scenePos() - pressPos_).manhattanLength() > 4.0;
    if (isFirstSegment && wasDrag) {
        const WireGestureEndpoint end = hitTest(event->scenePos());
        if (isSameConnectionPoint(startHit_, end)) {
            cancel(); // arrastre de vuelta al mismo punto: sin efecto
        } else {
            // Destino valido, o vacio (se crea un punto de union libre ahi).
            commitTo(end, event->scenePos());
        }
    }
    // En cualquier otro caso (clic simple, o clic que agrego un quiebre) el
    // trazado sigue abierto; se cierra con un clic sobre un destino valido o
    // con doble clic (finishAt).
}

void WireTool::finishAt(QPointF scenePos) {
    if (!drawing_) {
        return;
    }
    const WireGestureEndpoint end = hitTest(scenePos);
    if (isSameConnectionPoint(startHit_, end)) {
        return; // terminar sobre el mismo punto de partida no es valido
    }
    commitTo(end, scenePos); // destino valido o vacio (crea punto de union libre)
}

void WireTool::commitTo(const WireGestureEndpoint& end, QPointF endScenePos) {
    const WireGestureEndpoint start = startHit_;
    // Quiebres = todos los vertices fijados menos el ancla inicial (points_[0]).
    std::vector<QPointF> waypoints(points_.begin() + (points_.empty() ? 0 : 1), points_.end());
    cancel();

    // Un macro agrupa como un solo paso de undo todo lo que haga falta antes
    // del AddWire: dividir un cable (derivacion en T) en cualquiera de los dos
    // extremos, y/o crear un punto de union libre si se termino en el vacio.
    const bool endIsEmpty = end.empty();
    const bool needsMacro =
        start.pendingBranchWire != nullptr || end.pendingBranchWire != nullptr || endIsEmpty;
    if (needsMacro) {
        undoStack_->beginMacro("Conectar cable");
    }
    const WireEndpoint a = resolveEndpoint(start);
    WireEndpoint b;
    if (endIsEmpty) {
        auto* junctionCommand = new AddJunctionCommand(document_, snapCursor(scene_, endScenePos));
        undoStack_->push(junctionCommand);
        b = WireEndpoint::junction(junctionCommand->junctionId());
    } else {
        b = resolveEndpoint(end);
    }
    undoStack_->push(new AddWireCommand(document_, a, b, std::move(waypoints)));
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
    if (previewPath_ != nullptr) {
        scene_->removeItem(previewPath_);
        delete previewPath_;
        previewPath_ = nullptr;
    }
    drawing_ = false;
    startHit_ = WireGestureEndpoint{};
    points_.clear();
}

} // namespace digitalforge::editor
