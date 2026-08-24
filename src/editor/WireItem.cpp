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

// Los cables se ajustan a MEDIA unidad de grilla, no a la unidad entera que
// usan los componentes: asi un quiebre puede caer tanto en una interseccion
// como en el CENTRO de la celda, que es donde suelen quedar los pines. Con la
// unidad entera ese centro era inalcanzable y el cable entraba corrido al pin.
constexpr qreal kWireGridSize = ComponentItem::kGridSize / 2.0;

QPointF maybeSnap(const CircuitScene* scene, QPointF point) {
    if (scene != nullptr && scene->snapToGridEnabled()) {
        return snapToGrid(point, kWireGridSize);
    }
    return point;
}

// Lleva cada quiebre a la media unidad de grilla mas cercana -- la misma
// retícula (intersecciones + centros) que dibuja CircuitScene::drawBackground().
// Solo los puntos INTERIORES: los dos extremos los ancla su pin/union.
std::vector<QPointF> snapBends(const CircuitScene* scene, std::vector<QPointF> polyline) {
    if (scene == nullptr || !scene->snapToGridEnabled()) {
        return polyline;
    }
    for (std::size_t i = 1; i + 1 < polyline.size(); ++i) {
        polyline[i] = snapToGrid(polyline[i], kWireGridSize);
    }
    return simplifyOrthogonalPolyline(polyline);
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

void WireItem::setLiveWaypointOffset(std::optional<QPointF> offset) {
    if (liveWaypointOffset_ == offset) {
        return;
    }
    liveWaypointOffset_ = offset;
    updateGeometry();
}

std::vector<QPointF> WireItem::storedWaypoints() const {
    const WireConnection* w = document_->wire(wireId_);
    return w != nullptr ? w->waypoints : std::vector<QPointF>{};
}

std::vector<QPointF> WireItem::renderedPolyline() const {
    std::vector<QPointF> points;
    points.push_back(endpointScenePos(a_, anchorA_));
    const std::vector<QPointF> waypoints = storedWaypoints();
    points.insert(points.end(), waypoints.begin(), waypoints.end());
    points.push_back(endpointScenePos(b_, anchorB_));
    return wireVertices(points);
}

std::size_t WireItem::nearestSegmentIndex(const std::vector<QPointF>& polyline, QPointF point) {
    std::size_t best = 0;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (std::size_t i = 0; i + 1 < polyline.size(); ++i) {
        const qreal distance = distanceToSegment(point, polyline[i], polyline[i + 1]);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

QPointF WireItem::nearestPointOnPath(QPointF scenePos) const {
    const std::vector<QPointF> polyline = renderedPolyline();
    if (polyline.empty()) {
        return scenePos;
    }
    QPointF best = polyline.front();
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (std::size_t i = 0; i + 1 < polyline.size(); ++i) {
        QPointF projection;
        const qreal distance = distanceToSegment(scenePos, polyline[i], polyline[i + 1], &projection);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = projection;
        }
    }
    return best;
}

WireSplit WireItem::splitWaypointsAt(QPointF point) const { return splitWireWaypoints(renderedPolyline(), point); }

QPointF WireItem::endpointHandlePos(bool isA) const {
    const std::vector<QPointF> polyline = renderedPolyline();
    if (polyline.size() < 2) {
        return endpointScenePos(isA ? a_ : b_, isA ? anchorA_ : anchorB_);
    }
    const QPointF e = isA ? polyline.front() : polyline.back();
    const QPointF neighbor = isA ? polyline[1] : polyline[polyline.size() - 2];
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

    std::vector<QPointF> points;
    if (dragKind_ != DragKind::None && dragPolyline_.size() >= 2) {
        // Remodelado en curso: la polilinea en edicion manda, pero los
        // extremos siguen pegados a su pin/union en vivo.
        points = dragPolyline_;
        points.front() = start;
        points.back() = end;
    } else {
        std::vector<QPointF> waypoints = storedWaypoints();
        // Arrastre de seleccion multiple en curso (ver SelectionTool::
        // afterMove): los waypoints todavia viven en el documento con su
        // posicion vieja, asi que se corrigen aca nada mas que para dibujar --
        // el commit real llega al soltar, via SetWireWaypointsCommand.
        if (liveWaypointOffset_.has_value() && !waypoints.empty()) {
            for (QPointF& point : waypoints) {
                point += *liveWaypointOffset_;
            }
        }
        points.push_back(start);
        points.insert(points.end(), waypoints.begin(), waypoints.end());
        points.push_back(end);
    }
    setPath(buildWirePath(points));
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    const core::LogicValue value = document_->endpointValue(a_);
    const bool selected = (option->state & QStyle::State_Selected) != 0;

    QPen pen(logicValueColor(value));
    pen.setWidth(selected ? 2 : (hovered_ ? 2 : 1));
    if (selected) {
        pen.setStyle(Qt::DashLine);
    }
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());

    // Handles de extremo: cuadritos en a_/b_ cuando el cable esta
    // seleccionado, para senalar que esos puntos son agarrables (arrastrarlos
    // reconecta ese extremo). Se dibujan a tamano constante en pixeles
    // compensando el zoom, para que no crezcan/encojan con la vista.
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
    // la reconexion de ese extremo a otro destino. Se prueba primero porque
    // tiene prioridad sobre remodelar el trazado.
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

    const std::vector<QPointF> polyline = renderedPolyline();
    if (polyline.size() < 2 || !document_->requireEditable(QStringLiteral("modificar un cable"))) {
        QGraphicsPathItem::mousePressEvent(event);
        return;
    }

    // Cerca de una esquina interior -> se arrastra esa esquina. Si no, se
    // arrastra el segmento entero bajo el cursor (el gesto principal).
    for (std::size_t i = 1; i + 1 < polyline.size(); ++i) {
        if (QLineF(event->scenePos(), polyline[i]).length() <= kVertexHitRadius) {
            dragKind_ = DragKind::Corner;
            dragIndex_ = i;
            dragBasePolyline_ = polyline;
            dragPolyline_ = polyline;
            event->accept();
            return;
        }
    }
    dragKind_ = DragKind::Segment;
    dragIndex_ = nearestSegmentIndex(polyline, event->scenePos());
    dragBasePolyline_ = polyline;
    dragPolyline_ = polyline;
    // No se acepta el evento: se deja seguir a la base para que el clic simple
    // siga seleccionando el cable. El arrastre real se materializa en
    // mouseMoveEvent().
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

    if (dragKind_ != DragKind::None) {
        auto* circuitScene = qobject_cast<CircuitScene*>(scene());
        const QPointF cursor = maybeSnap(circuitScene, event->scenePos());
        // Siempre desde la base, nunca desde el resultado anterior (ver
        // dragBasePolyline_): moveWireSegment() inserta vertices de absorcion
        // en los extremos y aplicarlo en cadena los iria apilando.
        const std::vector<QPointF> moved = dragKind_ == DragKind::Segment
                                                ? moveWireSegment(dragBasePolyline_, dragIndex_, cursor)
                                                : moveWireCorner(dragBasePolyline_, dragIndex_, cursor);
        // No alcanza con snapear el cursor: la coordenada perpendicular de cada
        // quiebre (y la de los vertices de absorcion) se hereda del trazado
        // anterior, que pudo nacer de la posicion de un pin fuera de grilla.
        dragPolyline_ = snapBends(circuitScene, moved);
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

        // Resolver el destino: pin o punto de union existente. Soltar sobre el
        // cuerpo de otro cable o en el vacio no reconecta -- conectar es
        // siempre apuntar a un punto de conexion concreto.
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
        // Un pin ya conectado a OTRO cable tampoco es un destino valido (ver
        // CircuitDocument::pinHasWire()) -- *target == current/other ya cubre
        // el caso de que sea un extremo de este mismo cable, asi que si
        // pinHasWire() da true aca es siempre por un cable distinto.
        const bool targetPinOccupied =
            target.has_value() && !target->isJunction && document_->pinHasWire(target->pin());
        if (!target.has_value() || *target == current || *target == other || targetPinOccupied ||
            !document_->requireEditable(QStringLiteral("reconectar un cable"))) {
            updateGeometry(); // revertir el preview
            return;
        }

        // Diferido al proximo tick: retargetWire() dispara wireAboutToBeRemoved
        // que hace `delete` de este mismo WireItem -- destruir `this` dentro de
        // su propio mouseReleaseEvent seria undefined behavior. Solo se
        // capturan copias por valor.
        CircuitDocument* document = document_;
        QUndoStack* undoStack = undoStack_;
        const uint32_t wireId = wireId_;
        const WireEndpoint newEndpoint = *target;
        QTimer::singleShot(0, document, [document, undoStack, wireId, isA, newEndpoint] {
            undoStack->push(new RetargetWireEndpointCommand(document, wireId, isA, newEndpoint));
        });
        return;
    }

    if (dragKind_ == DragKind::None) {
        QGraphicsPathItem::mouseReleaseEvent(event);
        return;
    }
    commitDragPolyline();
    dragKind_ = DragKind::None;
    dragBasePolyline_.clear();
    dragPolyline_.clear();
    QGraphicsPathItem::mouseReleaseEvent(event);
}

void WireItem::commitDragPolyline() {
    if (dragPolyline_.size() < 2) {
        updateGeometry();
        return;
    }
    // Los dos extremos no se guardan: los ancla su pin/union y se recalculan
    // en cada updateGeometry().
    const std::vector<QPointF> next(dragPolyline_.begin() + 1, dragPolyline_.end() - 1);
    const std::vector<QPointF> previous = storedWaypoints();
    if (next != previous) {
        undoStack_->push(new SetWireWaypointsCommand(document_, wireId_, previous, next));
    } else {
        updateGeometry(); // sin cambios reales -- vuelve a leer del documento
    }
}

void WireItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    // Doble clic sobre una esquina la elimina (el cable vuelve a enderezarse
    // ahi). Agregar esquinas no necesita gesto propio: arrastrar un segmento
    // ya las crea donde hagan falta.
    const std::vector<QPointF> waypoints = storedWaypoints();
    for (std::size_t i = 0; i < waypoints.size(); ++i) {
        if (QLineF(event->scenePos(), waypoints[i]).length() <= kVertexHitRadius) {
            if (!document_->requireEditable(QStringLiteral("modificar un cable"))) {
                return;
            }
            std::vector<QPointF> next = waypoints;
            next.erase(next.begin() + static_cast<std::ptrdiff_t>(i));
            undoStack_->push(new SetWireWaypointsCommand(document_, wireId_, waypoints, next));
            event->accept();
            return;
        }
    }
    QGraphicsPathItem::mouseDoubleClickEvent(event);
}

} // namespace digitalforge::editor
