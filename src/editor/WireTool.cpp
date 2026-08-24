#include "WireTool.hpp"

#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QString>
#include <QUndoStack>
#include <algorithm>
#include <cmath>
#include <optional>
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
        // Media unidad, igual que los quiebres que se arrastran despues (ver
        // kWireGridSize en WireItem.cpp): los cables pueden apoyarse tanto en
        // las intersecciones como en el centro de cada celda.
        return snapToGrid(scenePos, ComponentItem::kGridSize / 2.0);
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

// Un pin acepta un solo cable directo (ver CircuitDocument::pinHasWire()) --
// ramificar desde un pin ya conectado necesita pasar por un punto de union,
// asi que terminar ahi es un destino invalido igual que "el mismo punto de
// partida" (mismo tratamiento visual/de commit en todo este archivo: rojo en
// el preview, gesto cancelado en vez de intentar una conexion que
// CircuitDocument igual rechazaria).
bool isOccupiedPin(const CircuitDocument* document, const WireGestureEndpoint& hit) {
    return hit.pin != nullptr &&
           document->pinHasWire(PinRef{hit.pin->componentId(), hit.pin->pinIndex()});
}

} // namespace

WireTool::WireTool(CircuitScene* scene, CircuitDocument* document, QUndoStack* undoStack)
    : scene_(scene), document_(document), undoStack_(undoStack) {}

QPointF WireTool::lastVertex() const {
    if (!autoPoints_.empty()) {
        return autoPoints_.back();
    }
    return points_.empty() ? QPointF() : points_.back();
}

void WireTool::accumulateSteps(QPointF cursorScenePos) {
    if (axis_ == Axis::None || points_.empty()) {
        return;
    }
    // Un escalon nace cuando el cursor se aparta del eje vigente mas que
    // kStepCreate, y se deshace recien cuando vuelve por debajo de kStepRelease.
    // Los dos umbrales son distintos A PROPOSITO (histeresis): con un umbral
    // unico, justo sobre el limite el escalon aparecia y desaparecia con cada
    // temblor del mouse, porque crear y deshacer miden exactamente el mismo
    // desvio. Ademas kStepCreate son varias unidades de grilla y no una sola:
    // con una sola (8 px, dos celdas de snap) un movimiento diagonal dejaba una
    // escalera de escalones minusculos y el gesto se sentia hipersensible.
    constexpr qreal kStepCreate = 3.0 * ComponentItem::kGridSize;
    constexpr qreal kStepRelease = kStepCreate / 2.0;

    // Retraccion primero: cada esquina acumulada existe por un desvio concreto
    // (horizontal si el eje quedo en Horizontal, vertical si quedo en
    // Vertical). Si ese desvio se deshizo, la esquina sobra.
    while (!autoPoints_.empty()) {
        const QPointF corner = autoPoints_.back();
        const bool undone = axis_ == Axis::Horizontal
                                ? std::abs(cursorScenePos.x() - corner.x()) < kStepRelease
                                : std::abs(cursorScenePos.y() - corner.y()) < kStepRelease;
        if (!undone) {
            break;
        }
        autoPoints_.pop_back();
        axis_ = axis_ == Axis::Horizontal ? Axis::Vertical : Axis::Horizontal;
    }

    const QPointF last = lastVertex();
    if (axis_ == Axis::Vertical) {
        if (std::abs(cursorScenePos.x() - last.x()) >= kStepCreate) {
            autoPoints_.push_back(QPointF(last.x(), cursorScenePos.y()));
            axis_ = Axis::Horizontal;
        }
    } else if (std::abs(cursorScenePos.y() - last.y()) >= kStepCreate) {
        autoPoints_.push_back(QPointF(cursorScenePos.x(), last.y()));
        axis_ = Axis::Vertical;
    }
}

void WireTool::updateAxis(QPointF cursorScenePos) {
    if (axis_ != Axis::None || points_.empty()) {
        return; // ya fijado para este tramo
    }
    const QPointF delta = cursorScenePos - lastVertex();
    const qreal dx = std::abs(delta.x());
    const qreal dy = std::abs(delta.y());
    // Hasta no alejarse un poco del ultimo punto, la direccion dominante es
    // ruido: fijar el eje ahi haria que el codo saliera para cualquier lado.
    constexpr qreal kAxisLockThreshold = 6.0;
    if (std::max(dx, dy) < kAxisLockThreshold) {
        return;
    }
    // Y aunque ya se haya alejado, sobre la diagonal ninguna de las dos
    // direcciones manda de verdad: un eje elegido ahi por un pixel de
    // diferencia se siente arbitrario, y como el eje ya no se recalcula, ese
    // volado queda fijo para todo el tramo. Mientras el movimiento siga siendo
    // ambiguo se espera (sin eje no hay escalera, y wireVertices() ya dibuja un
    // codo en L por defecto); apenas el usuario define una direccion, se fija.
    constexpr qreal kAxisDominance = ComponentItem::kGridSize;
    if (std::abs(dx - dy) < kAxisDominance) {
        return;
    }
    axis_ = dy > dx ? Axis::Vertical : Axis::Horizontal;
}

std::optional<QPointF> WireTool::autoCorner(QPointF from, QPointF to) const {
    if (axis_ == Axis::None) {
        return std::nullopt;
    }
    // Tramo recto: no hace falta ningun codo.
    if (std::abs(from.x() - to.x()) <= kWireAlignTolerance ||
        std::abs(from.y() - to.y()) <= kWireAlignTolerance) {
        return std::nullopt;
    }
    // Vertical: primero baja/sube hasta la altura del cursor y despues va en
    // horizontal. Horizontal: al reves.
    return axis_ == Axis::Vertical ? QPointF(from.x(), to.y()) : QPointF(to.x(), from.y());
}

std::vector<QPointF> WireTool::pointsThrough(QPointF cursorScenePos) const {
    std::vector<QPointF> points = points_;
    points.insert(points.end(), autoPoints_.begin(), autoPoints_.end());
    if (!points.empty()) {
        if (const std::optional<QPointF> corner = autoCorner(points.back(), cursorScenePos)) {
            points.push_back(*corner);
        }
    }
    points.push_back(cursorScenePos);
    return points;
}

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
        // Inicio de un trazado: solo desde un pin/union/cuerpo de cable. Un
        // pin que ya tiene otro cable tampoco sirve de arranque -- el destino
        // final quedaria igual de invalido, asi que se rechaza aca de una vez
        // (mismo criterio que isOccupiedPin() aplica al destino).
        const WireGestureEndpoint start = hitTest(pos);
        if (start.empty() || isOccupiedPin(document_, start)) {
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
    if (!hit.empty() && !isSameConnectionPoint(startHit_, hit) && !isOccupiedPin(document_, hit)) {
        commitTo(hit, pos);
        return;
    }
    // Fijar una esquina consolida la escalera que se estaba viendo (los
    // escalones acumulados por el movimiento pasan a ser puntos fijos) mas el
    // codo del ultimo tramo, y reinicia el eje para que el tramo siguiente
    // elija su propia direccion.
    const QPointF bend = hit.empty() ? snapCursor(scene_, pos) : hit.anchorPos;
    points_.insert(points_.end(), autoPoints_.begin(), autoPoints_.end());
    autoPoints_.clear();
    if (!points_.empty()) {
        if (const std::optional<QPointF> corner = autoCorner(points_.back(), bend)) {
            points_.push_back(*corner);
        }
    }
    if (points_.empty() || bend != points_.back()) {
        points_.push_back(bend);
    }
    axis_ = Axis::None;
    pressPos_ = pos;
}

void WireTool::move(QGraphicsSceneMouseEvent* event) {
    if (!drawing_) {
        return;
    }
    // La escalera se construye sobre el cursor ya ajustado a la grilla, para
    // que cada escalon caiga sobre la reticula igual que cualquier quiebre.
    const QPointF snapped = snapCursor(scene_, event->scenePos());
    updateAxis(snapped);
    accumulateSteps(snapped);
    updatePreview(event->scenePos());
}

void WireTool::updatePreview(QPointF cursorScenePos) {
    if (previewPath_ == nullptr) {
        return;
    }
    const WireGestureEndpoint end = hitTest(cursorScenePos);
    const QPointF endPos = end.empty() ? snapCursor(scene_, cursorScenePos) : end.anchorPos;
    // Mismo motor y mismos puntos que el cable que va a quedar: lo que se ve
    // en el preview es exactamente lo que se comete.
    previewPath_->setPath(buildWirePath(pointsThrough(endPos)));

    // Color del preview segun el destino bajo el cursor: verde si soltar ahi
    // haria una conexion valida, rojo si es un destino invalido (el mismo
    // punto de partida, o un pin que ya tiene otro cable), gris neutro si
    // todavia no hay destino (vacio/grilla).
    QPen pen;
    if (end.empty()) {
        pen = QPen(Qt::darkGray, 1, Qt::DashLine);
    } else if (isSameConnectionPoint(startHit_, end) || isOccupiedPin(document_, end)) {
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
        if (isSameConnectionPoint(startHit_, end) || isOccupiedPin(document_, end)) {
            cancel(); // arrastre de vuelta al mismo punto, o a un pin ya ocupado: sin efecto
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
    if (isSameConnectionPoint(startHit_, end) || isOccupiedPin(document_, end)) {
        return; // terminar sobre el mismo punto de partida, o un pin ya ocupado, no es valido
    }
    commitTo(end, scenePos); // destino valido o vacio (crea punto de union libre)
}

void WireTool::commitTo(const WireGestureEndpoint& end, QPointF endScenePos) {
    const WireGestureEndpoint start = startHit_;
    // El ancla final es el punto real de conexion cuando hay destino; solo en
    // el vacio se usa el cursor snappeado (ahi se creara un punto de union).
    const QPointF endAnchor = end.empty() ? snapCursor(scene_, endScenePos) : end.anchorPos;
    // Los mismos vertices que se estaban viendo en el preview, incluida la
    // esquina automatica del ultimo tramo, menos las dos anclas (que las fijan
    // los extremos).
    const std::vector<QPointF> preview = pointsThrough(endAnchor);
    std::vector<QPointF> waypoints;
    if (preview.size() > 2) {
        waypoints.assign(preview.begin() + 1, preview.end() - 1);
    }
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
    // Preservar el trazado ya acomodado del cable que se esta derivando: sin
    // esto, SplitWireCommand partia el cable con los dos segmentos rectos
    // (waypoints vacios), asi que cualquier quiebre que el usuario le hubiera
    // dado se perdia de golpe al conectarle otro cable por encima.
    const WireSplit split = hit.pendingBranchWire->splitWaypointsAt(hit.anchorPos);
    auto* splitCommand = new SplitWireCommand(document_, hit.pendingBranchWire->wireId(), hit.anchorPos,
                                               split.before, split.after);
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
    autoPoints_.clear();
    axis_ = Axis::None;
}

} // namespace digitalforge::editor
