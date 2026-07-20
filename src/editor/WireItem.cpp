#include "WireItem.hpp"

#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <QPainter>
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
#include "UndoCommands.hpp"

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
constexpr qreal kAlignTolerance = 3.0;

// El tramo vertical de un codo (ver appendElbow()) cruza `rect` si su x cae
// dentro del ancho del rectangulo y su recorrido en y (entre `yMin`/`yMax`)
// se superpone con la altura del rectangulo -- test estandar de
// segmento-vertical contra rectangulo, usado por chooseClearMidX() para
// saber si un candidato de x esta libre.
bool verticalSegmentCrosses(qreal x, qreal yMin, qreal yMax, const QRectF& rect) {
    return x >= rect.left() && x <= rect.right() && yMax >= rect.top() && yMin <= rect.bottom();
}

// Elige la x del tramo vertical del codo entre `from` y `to`: el punto medio
// natural si ya esta libre de `obstacles` (el caso comun), o si no el
// candidato libre mas cercano a ese punto medio, probando corrimientos
// crecientes a ambos lados -- en vez de dejar que el codo pase derecho por
// encima de otro componente que haya quedado en el medio (el defecto
// reportado: cables que atraviesan visualmente otras compuertas en vez de
// esquivarlas). Si ningun candidato dentro del rango de busqueda queda
// libre (obstaculos muy densos), se resigna al punto medio natural -- un
// cable que cruza una compuerta sigue siendo mejor que uno que serpentea
// sin necesidad.
qreal chooseClearMidX(QPointF from, QPointF to, const std::vector<QRectF>& obstacles) {
    const qreal naturalMidX = (from.x() + to.x()) / 2.0;
    if (obstacles.empty()) {
        return naturalMidX;
    }
    const qreal yMin = std::min(from.y(), to.y());
    const qreal yMax = std::max(from.y(), to.y());
    const auto isClear = [&](qreal x) {
        for (const QRectF& rect : obstacles) {
            if (verticalSegmentCrosses(x, yMin, yMax, rect)) {
                return false;
            }
        }
        return true;
    };
    if (isClear(naturalMidX)) {
        return naturalMidX;
    }
    constexpr qreal kStep = 16.0; // 2x ComponentItem::kGridSize
    constexpr int kMaxSteps = 24; // hasta 384px a cada lado del punto medio
    for (int i = 1; i <= kMaxSteps; ++i) {
        const qreal plus = naturalMidX + i * kStep;
        if (isClear(plus)) {
            return plus;
        }
        const qreal minus = naturalMidX - i * kStep;
        if (isClear(minus)) {
            return minus;
        }
    }
    return naturalMidX;
}

// Agrega a `path` (que ya debe empezar en `from`) los quiebres en angulo
// recto que llevan hasta `to`. Se degenera a una sola linea recta cuando
// `from`/`to` ya comparten x o y (exactamente, o dentro de
// kAlignTolerance) -- por eso el mismo helper sirve tanto para el
// auto-ruteo entre extremos como para el tramo entre dos waypoints
// consecutivos, sin duplicar codos donde no hacen falta. `obstacles`
// (bounding rects de otros componentes, ver WireItem::obstacleRects()) se
// usa para elegir donde cae el tramo vertical -- ver chooseClearMidX().
void appendElbow(QPainterPath& path, QPointF from, QPointF to, const std::vector<QRectF>& obstacles) {
    if (std::abs(from.y() - to.y()) <= kAlignTolerance) {
        // Corre recto hasta la columna de `to` a la altura de `from`, y
        // recien ahi (pegado al pin de destino) el salto vertical residual
        // -- nunca en el medio del tramo, donde se leeria como un quiebre
        // suelto.
        path.lineTo(to.x(), from.y());
        path.lineTo(to);
        return;
    }
    if (std::abs(from.x() - to.x()) <= kAlignTolerance) {
        path.lineTo(from.x(), to.y());
        path.lineTo(to);
        return;
    }
    const qreal midX = chooseClearMidX(from, to, obstacles);
    path.lineTo(midX, from.y());
    path.lineTo(midX, to.y());
    path.lineTo(to);
}

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

// Los vertices que updateGeometry() realmente dibujaria para el tramo entre
// `from` y `to` (los dos quiebres del auto-ruteo, mas `to`) -- se degenera
// limpiamente a solo `to` cuando ya son colineales, igual que appendElbow().
// Se usa para que el hit-testing de "punto mas cercano sobre el cable"
// (insertar un vertice, dividir el cable) razone sobre la ruta realmente
// dibujada -- que casi nunca coincide con la cuerda recta entre dos puntos
// logicos consecutivos -- y no aparezca desalineada de lo que se ve.
void appendElbowVertices(std::vector<QPointF>& vertices, QPointF from, QPointF to,
                          const std::vector<QRectF>& obstacles) {
    if (std::abs(from.y() - to.y()) <= kAlignTolerance) {
        vertices.push_back(QPointF(to.x(), from.y()));
        vertices.push_back(to);
        return;
    }
    if (std::abs(from.x() - to.x()) <= kAlignTolerance) {
        vertices.push_back(QPointF(from.x(), to.y()));
        vertices.push_back(to);
        return;
    }
    const qreal midX = chooseClearMidX(from, to, obstacles);
    vertices.push_back(QPointF(midX, from.y()));
    vertices.push_back(QPointF(midX, to.y()));
    vertices.push_back(to);
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
    setZValue(-1.0); // dibuja los cables detras de los cuerpos de los componentes
    // Un pen ancho a nivel de item solo ensancha shape()/boundingRect() para
    // facilitar el clic; paint() siempre define su propio pen antes de dibujar.
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

void WireItem::updateGeometry() {
    prepareGeometryChange();
    const QPointF start = endpointScenePos(a_, anchorA_);
    const QPointF end = endpointScenePos(b_, anchorB_);
    const std::vector<QPointF> waypoints = dragging_ ? dragWaypoints_ : storedWaypoints();

    std::vector<QPointF> points;
    points.reserve(waypoints.size() + 2);
    points.push_back(start);
    points.insert(points.end(), waypoints.begin(), waypoints.end());
    points.push_back(end);

    const std::vector<QRectF> obstacles = obstacleRects();
    QPainterPath path(points.front());
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        appendElbow(path, points[i], points[i + 1], obstacles);
    }
    setPath(path);
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    const core::LogicValue value = document_->endpointValue(a_);
    const bool selected = (option->state & QStyle::State_Selected) != 0;

    QPen pen(logicValueColor(value));
    pen.setWidth(selected ? 3 : 2);
    if (selected) {
        pen.setStyle(Qt::DashLine);
    }
    painter->setPen(pen);
    painter->drawPath(path());
}

void WireItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
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
