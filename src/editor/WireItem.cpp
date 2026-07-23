#include "WireItem.hpp"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QUndoStack>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include "CircuitScene.hpp"
#include "ComponentItem.hpp"
#include "GridSnap.hpp"
#include "JunctionItem.hpp"
#include "LogicColors.hpp"
#include "PinItem.hpp"
#include "UndoCommands.hpp"
#include "WireRouting.hpp"

namespace digitalforge::editor {

namespace {

constexpr qreal kVertexHitRadius = 6.0;
// Distancia minima (en pixeles de escena) que debe recorrer el mouse tras un
// press sobre el cuerpo del cable antes de que eso cuente como "arrastre" e
// inserte un vertice nuevo -- por debajo de este umbral se trata como un
// clic simple (solo seleccion), sin alterar el trazado del cable.
constexpr qreal kInsertDragThreshold = 4.0;

// Umbral (en pixeles de escena) por debajo del cual dos puntos se tratan
// como si ya compartieran x/y -- igual a PinItem::kRadius, para que
// cualquier correccion que haga falta quede escondida dentro del propio
// punto de pin en vez de ser visible en el tramo abierto del cable. Sin
// esto, dos pines que el usuario espera ver alineados (misma fila logica en
// dos componentes con distinto pinPitch/alto) pero que difieren por un par
// de pixeles generaban un quiebre en angulo recto perfectamente correcto
// mate pero visualmente leido como un error de trazado ("no se ve recto") --
// el defecto reportado.
qreal distanceToSegment(QPointF p, QPointF a, QPointF b, QPointF* projectionOut = nullptr) {
    const QPointF ab = b - a;
    const qreal lengthSquared = QPointF::dotProduct(ab, ab);
    QPointF projection = a;
    if (lengthSquared > 0.0) {
        qreal t = QPointF::dotProduct(p - a, ab) / lengthSquared;
        t = std::clamp(t, 0.0, 1.0);
        projection = a + t * ab;
    }
    if (projectionOut != nullptr) {
        *projectionOut = projection;
    }
    return QLineF(p, projection).length();
}

QPointF maybeSnap(const CircuitScene* scene, QPointF point) {
    if (scene != nullptr && scene->snapToGridEnabled()) {
        return snapToGrid(point, ComponentItem::kGridSize);
    }
    return point;
}

} // namespace

WireItem::WireItem(CircuitDocument* document, QUndoStack* undoStack, uint32_t wireId, WireEndpoint a, WireEndpoint b,
                    WireAnchor anchorA, WireAnchor anchorB)
    : document_(document),
      undoStack_(undoStack),
      wireId_(wireId),
      a_(a),
      b_(b),
      anchorA_(anchorA),
      anchorB_(anchorB) {
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setAcceptHoverEvents(true);
    setZValue(-1.0); // dibuja los cables detras de los cuerpos de los componentes
    // Un pen ancho a nivel de item solo ensancha boundingRect() para facilitar
    // el clic; paint() siempre define su propio pen antes de dibujar y shape()
    // recalcula la banda clickeable segun el zoom.
    setPen(QPen(Qt::black, 6.0));
    if (anchorA_.component != nullptr) anchorA_.component->addAttachedWire(this);
    if (anchorA_.junction != nullptr) anchorA_.junction->addAttachedWire(this);
    if (anchorB_.component != nullptr) anchorB_.component->addAttachedWire(this);
    if (anchorB_.junction != nullptr) anchorB_.junction->addAttachedWire(this);
    updateGeometry();
}

WireItem::~WireItem() {
    if (anchorA_.component != nullptr) anchorA_.component->removeAttachedWire(this);
    if (anchorA_.junction != nullptr) anchorA_.junction->removeAttachedWire(this);
    if (anchorB_.component != nullptr) anchorB_.component->removeAttachedWire(this);
    if (anchorB_.junction != nullptr) anchorB_.junction->removeAttachedWire(this);
}

QPointF WireItem::endpointScenePos(const WireEndpoint& endpoint, const WireAnchor& anchor) const {
    if (endpoint.isJunction) {
        return anchor.junction->scenePos();
    }
    return anchor.component->pinScenePos(endpoint.pinIndex);
}

std::vector<QPointF> WireItem::storedWaypoints() const {
    const WireConnection* w = document_->wire(wireId_);
    return w != nullptr ? w->waypoints : std::vector<QPointF>{};
}

std::vector<QRectF> WireItem::obstacleRects() const {
    auto* circuitScene = qobject_cast<CircuitScene*>(scene());
    if (circuitScene == nullptr) {
        return {};
    }
    std::set<uint32_t> excludeIds;
    if (!a_.isJunction) {
        excludeIds.insert(a_.id);
    }
    if (!b_.isJunction) {
        excludeIds.insert(b_.id);
    }
    return circuitScene->componentObstacleRects(excludeIds);
}

std::size_t WireItem::nearestSegmentInsertIndex(QPointF point, const std::vector<QPointF>& waypoints) const {
    const std::vector<QRectF> obstacles = obstacleRects();
    std::vector<QPointF> logicalPoints;
    logicalPoints.push_back(endpointScenePos(a_, anchorA_));
    logicalPoints.insert(logicalPoints.end(), waypoints.begin(), waypoints.end());
    logicalPoints.push_back(endpointScenePos(b_, anchorB_));

    std::size_t insertAt = 0;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (std::size_t i = 0; i + 1 < logicalPoints.size(); ++i) {
        std::vector<QPointF> rendered{logicalPoints[i]};
        appendElbowVertices(rendered, logicalPoints[i], logicalPoints[i + 1], obstacles);
        for (std::size_t r = 0; r + 1 < rendered.size(); ++r) {
            const qreal distance = distanceToSegment(point, rendered[r], rendered[r + 1]);
            if (distance < bestDistance) {
                bestDistance = distance;
                insertAt = i;
            }
        }
    }
    return insertAt;
}

QPointF WireItem::nearestPointOnPath(QPointF scenePos) const {
    const std::vector<QRectF> obstacles = obstacleRects();
    std::vector<QPointF> logicalPoints;
    logicalPoints.push_back(endpointScenePos(a_, anchorA_));
    const std::vector<QPointF> waypoints = storedWaypoints();
    logicalPoints.insert(logicalPoints.end(), waypoints.begin(), waypoints.end());
    logicalPoints.push_back(endpointScenePos(b_, anchorB_));

    QPointF best = logicalPoints.front();
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (std::size_t i = 0; i + 1 < logicalPoints.size(); ++i) {
        std::vector<QPointF> rendered{logicalPoints[i]};
        appendElbowVertices(rendered, logicalPoints[i], logicalPoints[i + 1], obstacles);
        for (std::size_t r = 0; r + 1 < rendered.size(); ++r) {
            QPointF projection;
            const qreal distance = distanceToSegment(scenePos, rendered[r], rendered[r + 1], &projection);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = projection;
            }
        }
    }
    return best;
}

QPointF WireItem::endpointHandlePos(bool isA) const {
    const QPointF e = endpointScenePos(isA ? a_ : b_, isA ? anchorA_ : anchorB_);
    const std::vector<QPointF> wp = storedWaypoints();
    QPointF neighbor;
    if (isA) {
        neighbor = wp.empty() ? endpointScenePos(b_, anchorB_) : wp.front();
    } else {
        neighbor = wp.empty() ? endpointScenePos(a_, anchorA_) : wp.back();
    }
    QPointF d = neighbor - e;
    const qreal len = std::hypot(d.x(), d.y());
    if (len < 1e-3) {
        return e;
    }
    d /= len;
    const qreal offset = std::min(10.0, len * 0.5);
    return e + d * offset;
}

void WireItem::updateGeometry() {
    prepareGeometryChange();
    const QPointF start =
        (endpointDragging_ && endpointDragIsA_) ? endpointDragPos_ : endpointScenePos(a_, anchorA_);
    const QPointF end =
        (endpointDragging_ && !endpointDragIsA_) ? endpointDragPos_ : endpointScenePos(b_, anchorB_);
    const std::vector<QPointF> waypoints = dragging_ ? dragWaypoints_ : storedWaypoints();

    std::vector<QPointF> points;
    points.reserve(waypoints.size() + 2);
    points.push_back(start);
    points.insert(points.end(), waypoints.begin(), waypoints.end());
    points.push_back(end);

    const std::vector<QRectF> obstacles = obstacleRects();
    setPath(buildOrthogonalPath(points, obstacles));
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    const core::LogicValue value = document_->endpointValue(a_);
    const bool selected = (option->state & QStyle::State_Selected) != 0;

    QPen pen(logicValueColor(value));
    pen.setWidth(selected ? 3 : (hovered_ ? 3 : 2));
    if (selected) {
        pen.setStyle(Qt::DashLine);
    }
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());

    // Handles de extremo: cuadritos en a_/b_ cuando el cable es la unica
    // seleccion, para senalar que esos puntos son agarrables (la reconexion de
    // extremos en si llega en la fase siguiente). Se dibujan a tamano constante
    // en pixeles compensando el zoom, para que no crezcan/encojan con la vista.
    if (selected && !endpointDragging_) {
        qreal scale = 1.0;
        if (scene() != nullptr && !scene()->views().isEmpty()) {
            scale = scene()->views().first()->transform().m11();
        }
        if (scale <= 0.0) {
            scale = 1.0;
        }
        const qreal half = 3.0 / scale;
        painter->setPen(QPen(QColor(30, 90, 220), 1.0 / scale));
        painter->setBrush(QColor(255, 255, 255));
        for (const QPointF& p : {endpointHandlePos(true), endpointHandlePos(false)}) {
            painter->drawRect(QRectF(p.x() - half, p.y() - half, 2.0 * half, 2.0 * half));
        }
    }
}

QPainterPath WireItem::shape() const {
    qreal scale = 1.0;
    if (scene() != nullptr && !scene()->views().isEmpty()) {
        scale = scene()->views().first()->transform().m11();
    }
    if (scale <= 0.0) {
        scale = 1.0;
    }
    QPainterPathStroker stroker;
    // ~10px de banda clickeable en coordenadas de pantalla a cualquier zoom,
    // con un piso de 6px en coordenadas de escena para no perder tolerancia al
    // alejar mucho.
    stroker.setWidth(std::max(6.0, 10.0 / scale));
    return stroker.createStroke(path());
}

void WireItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = true;
    update();
    QGraphicsPathItem::hoverEnterEvent(event);
}

void WireItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = false;
    update();
    QGraphicsPathItem::hoverLeaveEvent(event);
}

void WireItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // Handles de extremo (solo con el cable seleccionado): agarrar uno inicia
    // la reconexion de ese extremo a otro destino. Se prueba antes que los
    // waypoints porque tiene prioridad sobre editar el trazado.
    if (isSelected()) {
        for (const bool isA : {true, false}) {
            if (QLineF(event->scenePos(), endpointHandlePos(isA)).length() <= kVertexHitRadius) {
                endpointDragging_ = true;
                endpointDragIsA_ = isA;
                endpointDragPos_ = endpointScenePos(isA ? a_ : b_, isA ? anchorA_ : anchorB_);
                event->accept();
                return;
            }
        }
    }

    const std::vector<QPointF> waypoints = storedWaypoints();
    for (std::size_t i = 0; i < waypoints.size(); ++i) {
        if (QLineF(event->scenePos(), waypoints[i]).length() <= kVertexHitRadius) {
            dragging_ = true;
            dragIndex_ = static_cast<int>(i);
            dragWaypoints_ = waypoints;
            event->accept();
            return;
        }
    }
    // Press sobre el cuerpo del cable, lejos de cualquier vertice existente:
    // la insercion de un vertice nuevo se posterga hasta mouseMoveEvent (ver
    // ahi), para que un clic simple sin arrastre real siga sirviendo solo
    // para seleccionar el cable.
    pendingInsertAt_ = event->scenePos();
    QGraphicsPathItem::mousePressEvent(event);
}

void WireItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (endpointDragging_) {
        auto* circuitScene = qobject_cast<CircuitScene*>(scene());
        // El preview sigue al destino bajo el cursor (pin/union) si lo hay, o
        // al cursor snappeado a grilla; asi se ve a donde va a quedar el
        // extremo antes de soltar.
        QPointF pos = event->scenePos();
        if (circuitScene != nullptr) {
            if (PinItem* pin = circuitScene->pinItemAt(pos)) {
                pos = pin->scenePos();
            } else if (JunctionItem* junction = circuitScene->junctionItemAt(pos)) {
                pos = junction->scenePos();
            } else {
                pos = maybeSnap(circuitScene, pos);
            }
        }
        endpointDragPos_ = pos;
        updateGeometry();
        return;
    }
    if (dragging_) {
        dragWaypoints_[static_cast<std::size_t>(dragIndex_)] =
            maybeSnap(qobject_cast<CircuitScene*>(scene()), event->scenePos());
        updateGeometry();
        return;
    }
    if (pendingInsertAt_.has_value() && QLineF(*pendingInsertAt_, event->scenePos()).length() > kInsertDragThreshold) {
        // Recien ahora se confirma que es un arrastre real (no un clic
        // simple): se inserta un unico vertice nuevo en el punto donde
        // arranco el press y se lo empieza a arrastrar desde ahi.
        const std::vector<QPointF> waypoints = storedWaypoints();
        const std::size_t insertAt = nearestSegmentInsertIndex(*pendingInsertAt_, waypoints);
        dragWaypoints_ = waypoints;
        dragWaypoints_.insert(dragWaypoints_.begin() + static_cast<std::ptrdiff_t>(insertAt), *pendingInsertAt_);
        dragIndex_ = static_cast<int>(insertAt);
        dragging_ = true;
        pendingInsertAt_.reset();
        updateGeometry();
        return;
    }
    QGraphicsPathItem::mouseMoveEvent(event);
}

void WireItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (endpointDragging_) {
        endpointDragging_ = false;
        auto* circuitScene = qobject_cast<CircuitScene*>(scene());
        const bool isA = endpointDragIsA_;
        const QPointF dropPoint = event->scenePos();

        // Resolver el destino: pin o punto de union existente (soltar sobre el
        // cuerpo de otro cable o en vacio no reconecta en esta fase).
        std::optional<WireEndpoint> target;
        if (circuitScene != nullptr) {
            if (PinItem* pin = circuitScene->pinItemAt(dropPoint)) {
                target = WireEndpoint(PinRef{pin->componentId(), pin->pinIndex()});
            } else if (JunctionItem* junction = circuitScene->junctionItemAt(dropPoint)) {
                target = WireEndpoint::junction(junction->junctionId());
            }
        }
        const WireEndpoint current = isA ? a_ : b_;
        const WireEndpoint other = isA ? b_ : a_;
        if (!target.has_value() || *target == current || *target == other ||
            !document_->requireEditable(QStringLiteral("reconectar un cable"))) {
            updateGeometry(); // revertir el preview
            return;
        }

        // Diferido al proximo tick: retargetWire() dispara wireAboutToBeRemoved
        // que hace `delete` de este mismo WireItem -- destruir `this` dentro de
        // su propio mouseReleaseEvent seria undefined behavior (mismo motivo
        // que trySpliceAt). Solo se capturan copias por valor.
        CircuitDocument* document = document_;
        QUndoStack* undoStack = undoStack_;
        const uint32_t wireId = wireId_;
        const WireEndpoint newEndpoint = *target;
        QTimer::singleShot(0, document, [document, undoStack, wireId, isA, newEndpoint] {
            undoStack->push(new RetargetWireEndpointCommand(document, wireId, isA, newEndpoint));
        });
        return;
    }

    pendingInsertAt_.reset();
    if (!dragging_) {
        QGraphicsPathItem::mouseReleaseEvent(event);
        return;
    }
    dragging_ = false;
    const int draggedIndex = dragIndex_;
    dragIndex_ = -1;

    if (draggedIndex >= 0 && static_cast<std::size_t>(draggedIndex) < dragWaypoints_.size() &&
        trySpliceAt(draggedIndex, dragWaypoints_[static_cast<std::size_t>(draggedIndex)])) {
        return;
    }

    const std::vector<QPointF> oldWaypoints = storedWaypoints();
    if (dragWaypoints_ != oldWaypoints) {
        undoStack_->push(new SetWireWaypointsCommand(document_, wireId_, oldWaypoints, dragWaypoints_));
    } else {
        updateGeometry(); // sin cambios reales -- vuelve a leer del documento
    }
}

bool WireItem::trySpliceAt(int waypointIndex, QPointF dropPoint) {
    WireItem* targetWire = nullptr;
    for (QGraphicsItem* item : scene()->items(dropPoint)) {
        if (auto* wire = dynamic_cast<WireItem*>(item); wire != nullptr && wire != this) {
            targetWire = wire;
            break;
        }
    }

    JunctionItem* targetJunction = nullptr;
    if (targetWire == nullptr) {
        for (QGraphicsItem* item : scene()->items(dropPoint)) {
            if (auto* junction = dynamic_cast<JunctionItem*>(item)) {
                // Ninguno de los dos extremos propios de este cable -- caer
                // ahi (p. ej. arrastrar el vertice mas cercano de vuelta
                // hacia su propio extremo) no es un empalme, es un no-op.
                if (junction != anchorA_.junction && junction != anchorB_.junction) {
                    targetJunction = junction;
                }
                break;
            }
        }
    }

    if (targetWire == nullptr && targetJunction == nullptr) {
        return false;
    }
    if (!document_->requireEditable(QStringLiteral("empalmar un cable"))) {
        return false;
    }

    const std::vector<QPointF> before(dragWaypoints_.begin(), dragWaypoints_.begin() + waypointIndex);
    const std::vector<QPointF> after(dragWaypoints_.begin() + waypointIndex + 1, dragWaypoints_.end());

    // El propio wireId_ se reemplaza por los dos tramos nuevos como parte de
    // este gesto -- pospuesto al proximo tick del event loop
    // (QTimer::singleShot(0, ...)) en vez de ejecutarse aca mismo: un
    // DeleteWireCommand::redo() dispara CircuitDocument::wireAboutToBeRemoved,
    // que CircuitScene::onWireAboutToBeRemoved() usa para hacer `delete` de
    // este mismo WireItem de inmediato -- destruir `this` mientras su propio
    // mouseReleaseEvent todavia esta en la pila de llamadas es undefined
    // behavior. Solo se capturan copias por valor (nunca `this`) para la
    // lambda diferida.
    CircuitDocument* document = document_;
    QUndoStack* undoStack = undoStack_;
    const uint32_t oldWireId = wireId_;
    const WireEndpoint a = a_;
    const WireEndpoint b = b_;
    const uint32_t targetWireId = targetWire != nullptr ? targetWire->wireId() : 0;
    const uint32_t targetJunctionId = targetJunction != nullptr ? targetJunction->junctionId() : 0;
    const bool splitsAWire = targetWire != nullptr;

    QTimer::singleShot(0, document,
                        [document, undoStack, oldWireId, a, b, before, after, splitsAWire, targetWireId,
                         targetJunctionId, dropPoint] {
                            undoStack->beginMacro(QStringLiteral("Empalmar cable"));

                            WireEndpoint junctionEndpoint;
                            if (splitsAWire) {
                                auto* splitCommand = new SplitWireCommand(document, targetWireId, dropPoint);
                                undoStack->push(splitCommand);
                                junctionEndpoint = WireEndpoint::junction(splitCommand->junctionId());
                            } else {
                                junctionEndpoint = WireEndpoint::junction(targetJunctionId);
                            }

                            undoStack->push(new DeleteWireCommand(document, oldWireId));

                            auto* firstHalf = new AddWireCommand(document, a, junctionEndpoint);
                            undoStack->push(firstHalf);
                            if (!before.empty()) {
                                undoStack->push(
                                    new SetWireWaypointsCommand(document, firstHalf->wireId(), {}, before));
                            }

                            auto* secondHalf = new AddWireCommand(document, junctionEndpoint, b);
                            undoStack->push(secondHalf);
                            if (!after.empty()) {
                                undoStack->push(
                                    new SetWireWaypointsCommand(document, secondHalf->wireId(), {}, after));
                            }

                            undoStack->endMacro();
                        });

    return true;
}

void WireItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    const std::vector<QPointF> waypoints = storedWaypoints();
    for (std::size_t i = 0; i < waypoints.size(); ++i) {
        if (QLineF(event->scenePos(), waypoints[i]).length() <= kVertexHitRadius) {
            std::vector<QPointF> next = waypoints;
            next.erase(next.begin() + static_cast<std::ptrdiff_t>(i));
            undoStack_->push(new SetWireWaypointsCommand(document_, wireId_, waypoints, next));
            event->accept();
            return;
        }
    }

    // Doble clic sobre el cuerpo del cable (no sobre un vertice existente):
    // inserta un vertice nuevo en el segmento mas cercano al punto clickeado.
    // Se usa el punto proyectado sobre el trazado real (no el punto crudo
    // del clic, y sin snap a grilla despues) para que el vertice nuevo caiga
    // exactamente sobre el cable, nunca desalineado de el.
    const std::size_t insertAt = nearestSegmentInsertIndex(event->scenePos(), waypoints);
    const QPointF newPoint = nearestPointOnPath(event->scenePos());
    std::vector<QPointF> next = waypoints;
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(insertAt), newPoint);
    undoStack_->push(new SetWireWaypointsCommand(document_, wireId_, waypoints, next));
    event->accept();
}

} // namespace digitalforge::editor
