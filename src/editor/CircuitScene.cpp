#include "CircuitScene.hpp"

#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleHints>
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "ComponentItem.hpp"
#include "JunctionItem.hpp"
#include "PinItem.hpp"
#include "PlacementTool.hpp"
#include "SelectionTool.hpp"
#include "UndoCommands.hpp"
#include "WireItem.hpp"
#include "WireRouting.hpp"
#include "WireTool.hpp"
#include "components/ComponentInstance.hpp"

namespace digitalforge::editor {

namespace {

struct ClipboardComponent {
    std::string typeId;
    components::PropertyMap properties;
    ComponentPlacement placement;
};

// Un punto de union libre copiado (ver Junction en CircuitDocument.hpp): solo
// hace falta su posicion, la identidad la asigna el pegado.
struct ClipboardJunction {
    QPointF position;
};

// Referencia relativa a un extremo de cable copiado: o bien el pin de un
// componente copiado (isJunction=false, index en ClipboardData::components) o
// un punto de union copiado (isJunction=true, index en
// ClipboardData::junctions). Nunca ids reales: al pegar, cada componente/
// union recibe una identidad nueva.
struct ClipboardEndpoint {
    bool isJunction = false;
    std::size_t index = 0;
    uint16_t pinIndex = 0;
};

struct ClipboardWire {
    ClipboardEndpoint a;
    ClipboardEndpoint b;
    // Trazado tal como se veia (ver WireConnection::waypoints) -- sin esto el
    // pegado reconstruia el cable con auto-ruteo y perdia cualquier quiebre
    // que el usuario le hubiera acomodado a mano.
    std::vector<QPointF> waypoints;
};

struct ClipboardData {
    std::vector<ClipboardComponent> components;
    std::vector<ClipboardJunction> junctions;
    std::vector<ClipboardWire> wires;
    int pasteCount = 0; // para que pegar repetidas veces desplace en cascada
};

// Compartido entre todas las CircuitScene (y por lo tanto entre documentos
// distintos del mismo Project): copiar en un documento y pegar en otro
// funciona igual que en cualquier editor con portapapeles.
ClipboardData g_clipboard;

} // namespace

CircuitScene::CircuitScene(CircuitDocument* document, QUndoStack* undoStack, QObject* parent)
    : QGraphicsScene(parent), document_(document), undoStack_(undoStack) {
    setSceneRect(-2000, -2000, 4000, 4000);

    selectionTool_ = std::make_unique<SelectionTool>(this, document_, undoStack_);
    wireTool_ = std::make_unique<WireTool>(this, document_, undoStack_);
    placementTool_ = std::make_unique<PlacementTool>(this, document_, undoStack_);

    connect(document_, &CircuitDocument::componentAdded, this, &CircuitScene::onComponentAdded);
    connect(document_, &CircuitDocument::componentAboutToBeRemoved, this, &CircuitScene::onComponentAboutToBeRemoved);
    connect(document_, &CircuitDocument::wireAdded, this, &CircuitScene::onWireAdded);
    connect(document_, &CircuitDocument::wireAboutToBeRemoved, this, &CircuitScene::onWireAboutToBeRemoved);
    connect(document_, &CircuitDocument::wireGeometryChanged, this, &CircuitScene::onWireGeometryChanged);
    connect(document_, &CircuitDocument::junctionAdded, this, &CircuitScene::onJunctionAdded);
    connect(document_, &CircuitDocument::junctionAboutToBeRemoved, this, &CircuitScene::onJunctionAboutToBeRemoved);
    connect(document_, &CircuitDocument::junctionPositionChanged, this, &CircuitScene::onJunctionPositionChanged);
    connect(document_, &CircuitDocument::propertyChanged, this, &CircuitScene::onPropertyChanged);
    connect(document_, &CircuitDocument::componentPlacementChanged, this, &CircuitScene::onComponentPlacementChanged);
    connect(document_, &CircuitDocument::simulationRebuilt, this, &CircuitScene::onSimulationChanged);
    connect(document_, &CircuitDocument::simulationStepped, this, &CircuitScene::onSimulationChanged);
    connect(document_, &CircuitDocument::liveSimulationChanged, this, &CircuitScene::onSimulationChanged);

    // drawBackground() ya relee el palette activo en cada repintado; esta
    // conexion solo adelanta ese repintado en el momento en que el usuario
    // cambia el tema claro/oscuro del sistema, en lugar de esperar al
    // siguiente evento de pintado natural (mover el mouse, editar, etc.).
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { update(); });

}

CircuitScene::~CircuitScene() = default;

void CircuitScene::setMode(EditorMode mode) {
    mode_ = mode;
    clearSelection();
}

void CircuitScene::beginPlacement(const std::string& typeId) {
    placementTool_->setPendingType(typeId);
    setMode(EditorMode::Placement);
}

void CircuitScene::cancelPlacement() {
    if (mode_ != EditorMode::Placement) {
        return;
    }
    placementTool_->setPendingType({});
    setMode(EditorMode::Selection);
}

ComponentItem* CircuitScene::componentItem(uint32_t componentId) const {
    const auto it = componentItems_.find(componentId);
    return it == componentItems_.end() ? nullptr : it->second;
}

ComponentItem* CircuitScene::componentWithLabelAt(QPointF scenePos) const {
    for (const auto& [componentId, item] : componentItems_) {
        if (item->labelHitTest(scenePos)) {
            return item;
        }
    }
    return nullptr;
}

JunctionItem* CircuitScene::junctionItem(uint32_t junctionId) const {
    const auto it = junctionItems_.find(junctionId);
    return it == junctionItems_.end() ? nullptr : it->second;
}

WireItem* CircuitScene::wireItem(uint32_t wireId) const {
    const auto it = wireItems_.find(wireId);
    return it == wireItems_.end() ? nullptr : it->second;
}

PinItem* CircuitScene::pinItemAt(QPointF scenePos) const {
    const QList<QGraphicsItem*> hits = items(scenePos);
    for (QGraphicsItem* item : hits) {
        if (auto* pin = dynamic_cast<PinItem*>(item)) {
            return pin;
        }
    }
    return nullptr;
}

JunctionItem* CircuitScene::junctionItemAt(QPointF scenePos) const {
    const QList<QGraphicsItem*> hits = items(scenePos);
    for (QGraphicsItem* item : hits) {
        if (auto* junction = dynamic_cast<JunctionItem*>(item)) {
            return junction;
        }
    }
    return nullptr;
}

WireItem* CircuitScene::wireItemAt(QPointF scenePos) const {
    const QList<QGraphicsItem*> hits = items(scenePos);
    for (QGraphicsItem* item : hits) {
        if (auto* wire = dynamic_cast<WireItem*>(item)) {
            return wire;
        }
    }
    return nullptr;
}

void CircuitScene::selectComponent(uint32_t componentId) {
    clearSelection();
    if (ComponentItem* item = componentItem(componentId)) {
        item->setSelected(true);
    }
}

QPointF CircuitScene::endpointScenePosition(const WireEndpoint& endpoint) const {
    if (endpoint.isJunction) {
        return document_->junctionPosition(endpoint.id);
    }
    if (ComponentItem* item = componentItem(endpoint.id)) {
        return item->pinScenePos(endpoint.pinIndex);
    }
    return QPointF();
}

std::vector<QPointF> CircuitScene::mergedWaypointsAcrossJunction(uint32_t junctionId, const WireConnection& w1,
                                                                  const WireConnection& w2) const {
    const WireEndpoint junction = WireEndpoint::junction(junctionId);
    const WireEndpoint otherEnd1 = (w1.a == junction) ? w1.b : w1.a;
    const WireEndpoint otherEnd2 = (w2.a == junction) ? w2.b : w2.a;

    // La cadena completa [otroExtremo1, ...waypoints de w1 en orden hacia la
    // union..., posicion de la union, ...waypoints de w2 en orden desde la
    // union..., otroExtremo2]; simplifyOrthogonalPolyline() colapsa la union
    // si quedo colineal entre sus vecinos (el caso comun tras borrar la
    // derivacion que la origino).
    std::vector<QPointF> chain;
    chain.push_back(endpointScenePosition(otherEnd1));
    if (w1.a == junction) {
        chain.insert(chain.end(), w1.waypoints.rbegin(), w1.waypoints.rend());
    } else {
        chain.insert(chain.end(), w1.waypoints.begin(), w1.waypoints.end());
    }
    chain.push_back(document_->junctionPosition(junctionId));
    if (w2.a == junction) {
        chain.insert(chain.end(), w2.waypoints.begin(), w2.waypoints.end());
    } else {
        chain.insert(chain.end(), w2.waypoints.rbegin(), w2.waypoints.rend());
    }
    chain.push_back(endpointScenePosition(otherEnd2));

    const std::vector<QPointF> simplified = simplifyOrthogonalPolyline(chain);
    if (simplified.size() <= 2) {
        return {};
    }
    return std::vector<QPointF>(simplified.begin() + 1, simplified.end() - 1);
}

void CircuitScene::drawBackground(QPainter* painter, const QRectF& rect) {
    // Se parte del color "Base" del palette activo (no de un blanco fijo)
    // para que el lienzo seleccion en modo claro/oscuro del sistema en lugar
    // de quedar siempre casi blanco. Un tinte suave hacia el verde permite
    // distinguir de un vistazo "en ejecucion" (wiring/placement bloqueados,
    // entradas conmutables) de "edicion" (en pausa), sin depender
    // unicamente del texto de la barra de estado; se aplica restando un poco
    // de rojo/azul en vez de sumar verde, ya que sumar satura enseguida
    // sobre un Base claro (ej. blanco puro).
    const QColor base = QApplication::palette().color(QPalette::Base);
    QColor background = base;
    if (document_->isLiveSimulation()) {
        background.setRed(std::max(0, base.red() - 10));
        background.setBlue(std::max(0, base.blue() - 8));
    }
    painter->fillRect(rect, background);
    if (!showGrid_) {
        return;
    }
    const qreal grid = ComponentItem::kGridSize;
    const bool dark = background.lightness() < 128;
    // La grilla debe distinguirse del fondo sin importar si el tema es claro
    // u oscuro: aclarar un fondo oscuro y oscurecer uno claro logra ambos
    // casos con la misma expresion.
    const QColor gridColor = dark ? background.lighter(140) : background.darker(110);
    // Media unidad, mas tenue: marca el CENTRO de cada celda. Los componentes
    // se ajustan a la unidad entera, pero los cables tambien pueden apoyarse
    // en estos centros (ver kWireGridSize en WireItem.cpp), que es donde caen
    // los pines centrados -- sin dibujarlos no habria referencia visual de
    // adonde va a caer un quiebre.
    const QColor halfGridColor = dark ? background.lighter(115) : background.darker(103);
    const qreal half = grid / 2.0;
    // Paso puramente VISUAL de la reticula principal: un multiplo entero de
    // `grid` para que sus lineas sigan cayendo exactamente sobre posiciones
    // de snap reales, pero mas separadas que kGridSize solo (que a zoom 1.0
    // se ve demasiado tupido). No afecta al snap de componentes/pines
    // (ComponentItem::kGridSize) ni al de cables (WireItem::kWireGridSize,
    // que sigue marcado por `half` arriba).
    constexpr qreal kVisualGridScale = 2.0;
    const qreal visualGrid = grid * kVisualGridScale;

    const auto drawLattice = [&](qreal step, const QColor& color) {
        painter->setPen(QPen(color, 0));
        const qreal left = std::floor(rect.left() / step) * step;
        const qreal top = std::floor(rect.top() / step) * step;
        for (qreal x = left; x < rect.right(); x += step) {
            painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        }
        for (qreal y = top; y < rect.bottom(); y += step) {
            painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        }
    };

    // Las medias solo cuando de verdad se separan en pantalla: a poco zoom una
    // linea cada 4px de escena se empasta en una mancha solida y ensucia el
    // lienzo en vez de ayudar a ubicar los centros.
    qreal scale = 1.0;
    if (!views().isEmpty()) {
        scale = views().first()->transform().m11();
    }
    constexpr qreal kMinLatticeSpacingPx = 6.0;
    if (half * scale >= kMinLatticeSpacingPx) {
        // Las medias primero y las enteras encima, para que las enteras sigan
        // leyendose como la referencia principal.
        drawLattice(half, halfGridColor);
    }
    drawLattice(visualGrid, gridColor);
}

void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // Un trazado en curso se comporta igual en cualquier modo (modeless, como
    // en Proteus): cada clic fija una esquina o cierra el cable sobre el
    // destino que haya debajo. No se llama a la clase base para que ese clic
    // no altere ademas la seleccion.
    if (wireTool_->isDrawing()) {
        wireTool_->press(event);
        return;
    }

    switch (mode_) {
        case EditorMode::Selection: {
            // La etiqueta de un componente se maneja aparte de la seleccion/
            // arrastre normal de Qt: ComponentItem::shape() excluye a
            // proposito esa franja (para que no cuente como clic sobre el
            // componente durante la simulacion), asi que Qt nunca la
            // entregaria como un clic sobre el item. Si el clic cae ahi, se
            // consume por completo el gesto (nunca se llega a la clase base)
            // para no arrancar ademas una seleccion por goma elastica.
            ComponentItem* labelTarget = componentWithLabelAt(event->scenePos());
            if (labelTarget != nullptr && document_->requireEditable(QStringLiteral("mover la etiqueta"))) {
                clearSelection();
                labelTarget->setSelected(true);
                labelDragTarget_ = labelTarget;
                labelDragStartOffset_ = labelTarget->labelOffset();
                labelDragStartLocalPos_ = labelTarget->mapFromScene(event->scenePos());
                event->accept();
                break;
            }
            // Siempre se deja que la gestion interna de Qt para el
            // grabber/foco se ejecute primero (para esto sirve el accept()
            // de PinItem::mousePressEvent: detiene la propagacion hacia el
            // ComponentItem padre movible sin saltarse el contrato normal de
            // eventos de QGraphicsScene). Omitir esta llamada a la clase
            // base dejaba antes el seguimiento interno del grabber de Qt en
            // un estado inconsistente y provocaba, mas adelante, un crash
            // intermitente en un evento diferido no relacionado.
            QGraphicsScene::mousePressEvent(event);
            // Pulsar directamente sobre un pin siempre inicia un cable, en
            // cualquier modo - no existe un paso separado para "entrar en
            // modo wiring".
            if (pinItemAt(event->scenePos()) != nullptr) {
                wireTool_->press(event);
            } else if ((event->modifiers() & Qt::ControlModifier) != 0 &&
                       wireItemAt(event->scenePos()) != nullptr) {
                // Ctrl+arrastre sobre un cable saca una derivacion desde ese
                // punto. Sin Ctrl el cuerpo del cable pertenece a WireItem,
                // que lo usa para desplazar el segmento (ver WireItem.hpp).
                wireTool_->press(event);
            } else {
                pressScenePos_ = event->scenePos();
                selectionTool_->afterPress(event);
            }
            break;
        }
        case EditorMode::Wiring:
            wireTool_->press(event);
            break;
        case EditorMode::Placement:
            placementTool_->press(event);
            break;
    }
}

void CircuitScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (wireTool_->isDrawing()) {
        wireTool_->move(event);
        return;
    }
    if (labelDragTarget_ != nullptr) {
        const QPointF localPos = labelDragTarget_->mapFromScene(event->scenePos());
        labelDragTarget_->setLiveLabelOffset(labelDragStartOffset_ + (localPos - labelDragStartLocalPos_));
        return;
    }
    if (mode_ == EditorMode::Selection) {
        QGraphicsScene::mouseMoveEvent(event);
        selectionTool_->afterMove(event);
    }
}

void CircuitScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (wireTool_->isDrawing()) {
        // Si el gesto fue un arrastre de A a B, release() cierra el cable; si
        // fue un clic simple, el trazado queda abierto y se sigue con clics
        // sucesivos (esquina) hasta cerrarlo sobre un destino.
        wireTool_->release(event);
        return;
    }
    if (labelDragTarget_ != nullptr) {
        const QPointF localPos = labelDragTarget_->mapFromScene(event->scenePos());
        const QPointF finalOffset = labelDragStartOffset_ + (localPos - labelDragStartLocalPos_);
        labelDragTarget_->setLiveLabelOffset(std::nullopt);
        if (finalOffset != labelDragStartOffset_) {
            undoStack_->push(
                new SetLabelOffsetCommand(document_, labelDragTarget_->componentId(), labelDragStartOffset_, finalOffset));
        }
        labelDragTarget_ = nullptr;
        return;
    }
    if (mode_ == EditorMode::Selection) {
        QGraphicsScene::mouseReleaseEvent(event);
    }
    switch (mode_) {
        case EditorMode::Selection: {
            // Mientras la simulacion esta en ejecucion, un solo clic (sin
            // arrastre) sobre un wiring.input alterna su valor - no hace
            // falta doble clic, ya que en modo ejecucion no hay nada mas que
            // hacer con un clic simple (mover/editar sigue bloqueado por
            // requireEditable en las herramientas que lo verifican).
            const bool wasDrag = (pressScenePos_ - event->scenePos()).manhattanLength() > 2.0;
            if (document_->isLiveSimulation() && !wasDrag && tryToggleInput(event->scenePos())) {
                return;
            }
            selectionTool_->afterRelease(event);
            break;
        }
        case EditorMode::Wiring:
            wireTool_->release(event);
            break;
        case EditorMode::Placement:
            break;
    }
}

bool CircuitScene::tryToggleInput(QPointF scenePos) {
    const QList<QGraphicsItem*> hits = items(scenePos);
    for (QGraphicsItem* hit : hits) {
        if (auto* component = dynamic_cast<ComponentItem*>(hit)) {
            const components::ComponentInstance* instance = document_->component(component->componentId());
            if (instance != nullptr && instance->typeId() == "wiring.input") {
                const core::LogicValue current = document_->pinValue(component->componentId(), 0);
                const core::LogicValue next =
                    (current == core::LogicValue::One) ? core::LogicValue::Zero : core::LogicValue::One;
                document_->setInputValue(component->componentId(), next);
                return true;
            }
        }
    }
    return false;
}

void CircuitScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (mode_ == EditorMode::Wiring) {
        // Doble clic cierra el trazado multi-segmento en curso sobre el
        // destino bajo el cursor (si es valido).
        wireTool_->finishAt(event->scenePos());
        event->accept();
        return;
    }
    if (mode_ != EditorMode::Selection) {
        QGraphicsScene::mouseDoubleClickEvent(event);
        return;
    }
    // tryToggleInput() es logica de simulacion (conmuta el valor impulsado
    // de una entrada) - no debe aplicarse fuera de modo simulacion, aunque
    // no se propague a nada (setInputValue() solo corre a estable si
    // isLiveSimulation() es true): el propio wiring.input igual cambiaria
    // de color de inmediato en edicion, algo confuso que no correspondia a
    // ninguna accion de edicion real.
    if (document_->isLiveSimulation() && tryToggleInput(event->scenePos())) {
        event->accept();
        return;
    }
    // Doble clic sobre el cuerpo de un componente abre el dialogo de
    // Propiedades - antes de dejar que el item lo procese, para que no
    // dependa de que ComponentItem implemente su propio
    // mouseDoubleClickEvent (no lo hace).
    for (QGraphicsItem* hit : items(event->scenePos())) {
        if (auto* component = dynamic_cast<ComponentItem*>(hit)) {
            emit componentDoubleClicked(component->componentId());
            event->accept();
            return;
        }
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void CircuitScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    // Clic derecho durante un trazado multi-segmento: cancela el gesto en vez
    // de abrir un menu contextual.
    if (mode_ == EditorMode::Wiring && wireTool_->isDrawing()) {
        wireTool_->cancel();
        event->accept();
        return;
    }
    for (QGraphicsItem* hit : items(event->scenePos())) {
        if (auto* component = dynamic_cast<ComponentItem*>(hit)) {
            clearSelection();
            component->setSelected(true);
            emit componentContextMenuRequested(component->componentId(), event->screenPos());
            event->accept();
            return;
        }
    }
    QGraphicsScene::contextMenuEvent(event);
}

void CircuitScene::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && wireTool_->isDrawing()) {
        wireTool_->cancel();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && mode_ == EditorMode::Placement) {
        // Salida del modo colocacion sin tener que gastar el clic en algun
        // lugar del lienzo donde no molestara.
        cancelPlacement();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelected();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_R) {
        rotateSelected();
        event->accept();
        return;
    }
    // Movimiento de 1px por pulsacion, sin pasar por el snap-a-grilla de
    // ComponentItem::itemChange (que solo se aplica ahi, no a
    // setComponentPlacement()) - el arrastre con el mouse ya cubre el
    // reacomodo alineado a grilla, las flechas son para el ajuste fino que
    // ese modo no permite.
    constexpr qreal kNudgeStep = 1.0;
    if (event->key() == Qt::Key_Left) {
        moveSelectedBy(QPointF(-kNudgeStep, 0.0));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Right) {
        moveSelectedBy(QPointF(kNudgeStep, 0.0));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Up) {
        moveSelectedBy(QPointF(0.0, -kNudgeStep));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        moveSelectedBy(QPointF(0.0, kNudgeStep));
        event->accept();
        return;
    }
    QGraphicsScene::keyPressEvent(event);
}

void CircuitScene::moveSelectedBy(QPointF delta) {
    std::vector<ComponentItem*> components;
    for (QGraphicsItem* item : selectedItems()) {
        if (auto* component = dynamic_cast<ComponentItem*>(item)) {
            components.push_back(component);
        }
    }
    if (components.empty()) {
        return;
    }
    if (components.size() > 1) {
        undoStack_->beginMacro("Mover seleccion");
    }
    for (ComponentItem* component : components) {
        const QPointF oldPosition = document_->componentPlacement(component->componentId()).position;
        undoStack_->push(
            new MoveComponentCommand(document_, component->componentId(), oldPosition, oldPosition + delta));
    }
    if (components.size() > 1) {
        undoStack_->endMacro();
    }
}

void CircuitScene::deleteSelected() {
    const QList<QGraphicsItem*> selected = selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    if (!document_->requireEditable(QStringLiteral("eliminar"))) {
        return;
    }

    // Todos los IDs se extraen ANTES de ejecutar ningun comando. Borrar un
    // componente en cascada borra (via CircuitDocument::wireAboutToBeRemoved)
    // los WireItem de sus cables conectados -- si uno de esos mismos
    // QGraphicsItem* tambien estaba en `selected` (p.ej. un rubber-band que
    // agarra un componente y su cable a la vez), seguir iterando `selected`
    // mientras se ejecutan comandos terminaba leyendo un WireItem ya
    // liberado (use-after-free: el crash reportado al eliminar varios
    // elementos juntos). Con los IDs ya copiados aparte, el resto del
    // proceso nunca vuelve a tocar los QGraphicsItem* originales.
    std::vector<uint32_t> componentIds;
    std::vector<uint32_t> wireIds;
    for (QGraphicsItem* item : selected) {
        if (auto* component = dynamic_cast<ComponentItem*>(item)) {
            componentIds.push_back(component->componentId());
        } else if (auto* wireItem = dynamic_cast<WireItem*>(item)) {
            wireIds.push_back(wireItem->wireId());
        } else if (auto* junctionItem = dynamic_cast<JunctionItem*>(item)) {
            for (const WireConnection& w : document_->wiresAttachedToJunction(junctionItem->junctionId())) {
                wireIds.push_back(w.id);
            }
        }
    }
    // Un mismo cable puede llegar tanto por seleccion directa como por una
    // junction seleccionada que lo toca.
    std::sort(wireIds.begin(), wireIds.end());
    wireIds.erase(std::unique(wireIds.begin(), wireIds.end()), wireIds.end());

    // Todos los cables que van a desaparecer, incluidos los que un componente
    // arrastra en cascada -- hace falta el conjunto completo para predecir,
    // ANTES de ejecutar nada, si algun punto de union va a quedar en grado 2
    // (ver mas abajo). Tambien se snapshotea aca, antes de que ningun comando
    // corra, cualquier punto de union que estos cables tocaban.
    std::set<uint32_t> removedWireIds(wireIds.begin(), wireIds.end());
    for (uint32_t componentId : componentIds) {
        for (const WireConnection& w : document_->wiresAttachedToComponent(componentId)) {
            removedWireIds.insert(w.id);
        }
    }
    std::set<uint32_t> candidateJunctionIds;
    for (uint32_t wireId : removedWireIds) {
        if (const WireConnection* w = document_->wire(wireId)) {
            for (const WireEndpoint& e : {w->a, w->b}) {
                if (e.isJunction) {
                    candidateJunctionIds.insert(e.id);
                }
            }
        }
    }
    // Un punto de union que sobrevive con exactamente 2 cables ya no es una
    // derivacion real (solo lo era mientras un tercer cable pasaba por ahi) --
    // se fusiona en uno solo para no dejar su punto visible sin motivo (ver
    // MergeJunctionCommand). Solo se predice el CONTEO ac -- asi el macro se
    // arma bien -- los sobrevivientes en si se vuelven a leer del documento
    // mas abajo, justo antes de cada push, nunca de este snapshot: dos
    // uniones encadenadas (cada una comparte el cable de en medio como
    // "sobreviviente") pueden perder su tercera derivacion en el mismo
    // gesto, y ese cable compartido ya no existe para cuando le toca el
    // turno al segundo merge -- se reconstruye a partir del primero.
    std::vector<uint32_t> pendingMergeJunctionIds;
    for (uint32_t junctionId : candidateJunctionIds) {
        std::size_t survivorCount = 0;
        for (const WireConnection& w : document_->wiresAttachedToJunction(junctionId)) {
            survivorCount += removedWireIds.contains(w.id) ? 0 : 1;
        }
        if (survivorCount == 2) {
            pendingMergeJunctionIds.push_back(junctionId);
        }
    }

    const std::size_t commandCount = componentIds.size() + wireIds.size() + pendingMergeJunctionIds.size();
    if (commandCount == 0) {
        return;
    }
    if (commandCount > 1) {
        undoStack_->beginMacro("Delete selection");
    }
    for (uint32_t componentId : componentIds) {
        undoStack_->push(new DeleteComponentCommand(document_, componentId));
    }
    for (uint32_t wireId : wireIds) {
        // Un componente eliminado arriba puede haber hecho cascada sobre
        // este mismo cable (ver comentario de arriba) -- se re-verifica por
        // ID (nunca por puntero) antes de apilar el comando.
        if (document_->wire(wireId) != nullptr) {
            undoStack_->push(new DeleteWireCommand(document_, wireId));
        }
    }
    for (uint32_t junctionId : pendingMergeJunctionIds) {
        // Releido del documento a proposito (no del snapshot de mas arriba):
        // un merge anterior en este mismo macro puede haber reemplazado el
        // cable que este junction comparte con el (ver comentario arriba de
        // pendingMergeJunctionIds). Si ya no esta en grado 2 -- porque quedo
        // absorbido por completo en la cascada del merge anterior -- no hay
        // nada que fusionar aca.
        const std::vector<WireConnection> survivors = document_->wiresAttachedToJunction(junctionId);
        if (survivors.size() != 2) {
            continue;
        }
        const std::vector<QPointF> merged = mergedWaypointsAcrossJunction(junctionId, survivors[0], survivors[1]);
        undoStack_->push(new MergeJunctionCommand(document_, junctionId, survivors[0], survivors[1], merged));
    }
    if (commandCount > 1) {
        undoStack_->endMacro();
    }
}

void CircuitScene::rotateSelected() {
    const QList<QGraphicsItem*> selected = selectedItems();
    std::vector<ComponentItem*> components;
    for (QGraphicsItem* item : selected) {
        if (auto* component = dynamic_cast<ComponentItem*>(item)) {
            components.push_back(component);
        }
    }
    if (components.empty()) {
        return;
    }
    if (components.size() > 1) {
        undoStack_->beginMacro("Rotate selection");
    }
    for (ComponentItem* component : components) {
        const int oldRotation = document_->componentPlacement(component->componentId()).rotationDegrees;
        const int newRotation = (oldRotation + 90) % 360;
        undoStack_->push(new RotateComponentCommand(document_, component->componentId(), oldRotation, newRotation));
    }
    if (components.size() > 1) {
        undoStack_->endMacro();
    }
}

namespace {
std::vector<ComponentItem*> selectedComponentsSortedByZOrder(const CircuitScene* scene,
                                                               const CircuitDocument* document) {
    std::vector<ComponentItem*> components;
    for (QGraphicsItem* item : scene->selectedItems()) {
        if (auto* component = dynamic_cast<ComponentItem*>(item)) {
            components.push_back(component);
        }
    }
    std::sort(components.begin(), components.end(), [document](ComponentItem* a, ComponentItem* b) {
        return document->componentPlacement(a->componentId()).zOrder <
               document->componentPlacement(b->componentId()).zOrder;
    });
    return components;
}
} // namespace

void CircuitScene::bringSelectedToFront() {
    const std::vector<ComponentItem*> components = selectedComponentsSortedByZOrder(this, document_);
    if (components.empty()) {
        return;
    }
    int nextZ = 0;
    for (const uint32_t id : document_->componentIds()) {
        nextZ = std::max(nextZ, document_->componentPlacement(id).zOrder + 1);
    }
    if (components.size() > 1) {
        undoStack_->beginMacro("Traer al frente");
    }
    for (ComponentItem* component : components) {
        const int oldZ = document_->componentPlacement(component->componentId()).zOrder;
        undoStack_->push(new SetZOrderCommand(document_, component->componentId(), oldZ, nextZ++));
    }
    if (components.size() > 1) {
        undoStack_->endMacro();
    }
}

void CircuitScene::sendSelectedToBack() {
    const std::vector<ComponentItem*> components = selectedComponentsSortedByZOrder(this, document_);
    if (components.empty()) {
        return;
    }
    int minZ = 0;
    for (const uint32_t id : document_->componentIds()) {
        minZ = std::min(minZ, document_->componentPlacement(id).zOrder - 1);
    }
    int nextZ = minZ - static_cast<int>(components.size()) + 1;
    if (components.size() > 1) {
        undoStack_->beginMacro("Enviar al fondo");
    }
    for (ComponentItem* component : components) {
        const int oldZ = document_->componentPlacement(component->componentId()).zOrder;
        undoStack_->push(new SetZOrderCommand(document_, component->componentId(), oldZ, nextZ++));
    }
    if (components.size() > 1) {
        undoStack_->endMacro();
    }
}

void CircuitScene::bringSelectedForward() {
    const std::vector<ComponentItem*> components = selectedComponentsSortedByZOrder(this, document_);
    if (components.empty()) {
        return;
    }
    if (components.size() > 1) {
        undoStack_->beginMacro("Traer adelante");
    }
    for (ComponentItem* component : components) {
        const int oldZ = document_->componentPlacement(component->componentId()).zOrder;
        undoStack_->push(new SetZOrderCommand(document_, component->componentId(), oldZ, oldZ + 1));
    }
    if (components.size() > 1) {
        undoStack_->endMacro();
    }
}

void CircuitScene::sendSelectedBackward() {
    const std::vector<ComponentItem*> components = selectedComponentsSortedByZOrder(this, document_);
    if (components.empty()) {
        return;
    }
    if (components.size() > 1) {
        undoStack_->beginMacro("Enviar atras");
    }
    for (ComponentItem* component : components) {
        const int oldZ = document_->componentPlacement(component->componentId()).zOrder;
        undoStack_->push(new SetZOrderCommand(document_, component->componentId(), oldZ, oldZ - 1));
    }
    if (components.size() > 1) {
        undoStack_->endMacro();
    }
}

void CircuitScene::copySelected() {
    std::vector<ComponentItem*> components;
    std::vector<JunctionItem*> junctions;
    for (QGraphicsItem* item : selectedItems()) {
        if (auto* component = dynamic_cast<ComponentItem*>(item)) {
            components.push_back(component);
        } else if (auto* junction = dynamic_cast<JunctionItem*>(item)) {
            junctions.push_back(junction);
        }
    }
    if (components.empty() && junctions.empty()) {
        return;
    }

    ClipboardData clipboard;
    std::map<uint32_t, std::size_t> componentIndexOf;
    for (ComponentItem* component : components) {
        const uint32_t id = component->componentId();
        const components::ComponentInstance* instance = document_->component(id);
        if (instance == nullptr) {
            continue;
        }
        componentIndexOf[id] = clipboard.components.size();
        ClipboardComponent c;
        c.typeId = instance->typeId();
        for (const components::PropertyDescriptor& descriptor : instance->definition().properties) {
            c.properties[descriptor.id] = instance->property(descriptor.id);
        }
        c.placement = document_->componentPlacement(id);
        clipboard.components.push_back(std::move(c));
    }

    std::map<uint32_t, std::size_t> junctionIndexOf;
    for (JunctionItem* junction : junctions) {
        junctionIndexOf[junction->junctionId()] = clipboard.junctions.size();
        clipboard.junctions.push_back(ClipboardJunction{junction->scenePos()});
    }

    // Un extremo solo se puede reconstruir si lo que apunta tambien quedo
    // copiado (pin de un componente copiado, o un punto de union copiado);
    // hacia afuera de la seleccion no tiene sentido reconstruirlo a medias.
    const auto resolveEndpoint = [&](const WireEndpoint& e) -> std::optional<ClipboardEndpoint> {
        if (e.isJunction) {
            const auto it = junctionIndexOf.find(e.id);
            if (it == junctionIndexOf.end()) {
                return std::nullopt;
            }
            return ClipboardEndpoint{true, it->second, 0};
        }
        const auto it = componentIndexOf.find(e.id);
        if (it == componentIndexOf.end()) {
            return std::nullopt;
        }
        return ClipboardEndpoint{false, it->second, e.pinIndex};
    };

    std::set<uint32_t> seenWires;
    std::vector<WireConnection> candidates;
    for (ComponentItem* component : components) {
        for (const WireConnection& w : document_->wiresAttachedToComponent(component->componentId())) {
            if (seenWires.insert(w.id).second) {
                candidates.push_back(w);
            }
        }
    }
    for (JunctionItem* junction : junctions) {
        for (const WireConnection& w : document_->wiresAttachedToJunction(junction->junctionId())) {
            if (seenWires.insert(w.id).second) {
                candidates.push_back(w);
            }
        }
    }
    for (const WireConnection& w : candidates) {
        const std::optional<ClipboardEndpoint> a = resolveEndpoint(w.a);
        const std::optional<ClipboardEndpoint> b = resolveEndpoint(w.b);
        if (!a.has_value() || !b.has_value()) {
            continue;
        }
        clipboard.wires.push_back(ClipboardWire{*a, *b, w.waypoints});
    }

    g_clipboard = std::move(clipboard);
}

void CircuitScene::cutSelected() {
    copySelected();
    deleteSelected();
}

void CircuitScene::pasteClipboard() {
    if (g_clipboard.components.empty() && g_clipboard.junctions.empty()) {
        return;
    }
    if (!document_->requireEditable(QStringLiteral("pegar"))) {
        return;
    }

    // Multiplo de ComponentItem::kGridSize para que el pegado siga cayendo
    // en la grilla; cada pegado sucesivo del mismo contenido se desplaza un
    // poco mas, en cascada, para no apilar copias exactamente unas sobre otras.
    ++g_clipboard.pasteCount;
    const QPointF offset(ComponentItem::kGridSize * 3 * g_clipboard.pasteCount,
                          ComponentItem::kGridSize * 3 * g_clipboard.pasteCount);

    undoStack_->beginMacro("Pegar");
    std::vector<uint32_t> newComponentIds;
    newComponentIds.reserve(g_clipboard.components.size());
    for (const ClipboardComponent& c : g_clipboard.components) {
        ComponentPlacement placement = c.placement;
        placement.position += offset;
        auto* command = new PlaceComponentCommand(document_, c.typeId, c.properties, placement);
        undoStack_->push(command);
        newComponentIds.push_back(command->componentId());
    }
    std::vector<uint32_t> newJunctionIds;
    newJunctionIds.reserve(g_clipboard.junctions.size());
    for (const ClipboardJunction& j : g_clipboard.junctions) {
        auto* command = new AddJunctionCommand(document_, j.position + offset);
        undoStack_->push(command);
        newJunctionIds.push_back(command->junctionId());
    }
    const auto resolveEndpoint = [&](const ClipboardEndpoint& e) -> WireEndpoint {
        if (e.isJunction) {
            return WireEndpoint::junction(newJunctionIds[e.index]);
        }
        return WireEndpoint(PinRef{newComponentIds[e.index], e.pinIndex});
    };
    for (const ClipboardWire& w : g_clipboard.wires) {
        std::vector<QPointF> waypoints = w.waypoints;
        for (QPointF& point : waypoints) {
            point += offset;
        }
        undoStack_->push(
            new AddWireCommand(document_, resolveEndpoint(w.a), resolveEndpoint(w.b), std::move(waypoints)));
    }
    undoStack_->endMacro();

    clearSelection();
    for (const uint32_t id : newComponentIds) {
        if (ComponentItem* item = componentItem(id)) {
            item->setSelected(true);
        }
    }
    for (const uint32_t id : newJunctionIds) {
        if (JunctionItem* item = junctionItem(id)) {
            item->setSelected(true);
        }
    }
}

void CircuitScene::applyPlacement(ComponentItem* item, const ComponentPlacement& placement) {
    item->setPos(placement.position);
    item->setTransformOriginPoint(item->boundingRect().center());
    item->setRotation(placement.rotationDegrees);
    item->setZValue(placement.zOrder);
}

void CircuitScene::onComponentAdded(uint32_t componentId) {
    auto* item = new ComponentItem(document_, componentId);
    addItem(item);
    componentItems_[componentId] = item;
    applyPlacement(item, document_->componentPlacement(componentId));
}

void CircuitScene::onComponentAboutToBeRemoved(uint32_t componentId) {
    const auto it = componentItems_.find(componentId);
    if (it == componentItems_.end()) {
        return;
    }
    // Borrar el componente mientras se arrastra su etiqueta (p.ej. Supr sin
    // soltar el mouse) no pasa por mouseReleaseEvent, asi que labelDragTarget_
    // quedaria apuntando al ComponentItem que se borra abajo si no se limpia
    // aca: el siguiente mouseMoveEvent/mouseReleaseEvent lo desreferenciaria
    // ya liberado.
    if (labelDragTarget_ == it->second) {
        labelDragTarget_ = nullptr;
    }
    removeItem(it->second);
    delete it->second;
    componentItems_.erase(it);
}

void CircuitScene::onWireAdded(uint32_t wireId) {
    const WireConnection* w = document_->wire(wireId);
    if (w == nullptr) {
        return;
    }
    auto resolveAnchor = [this](const WireEndpoint& endpoint) -> WireAnchor {
        WireAnchor anchor;
        if (endpoint.isJunction) {
            anchor.junction = junctionItem(endpoint.id);
        } else {
            anchor.component = componentItem(endpoint.id);
        }
        return anchor;
    };
    const WireAnchor anchorA = resolveAnchor(w->a);
    const WireAnchor anchorB = resolveAnchor(w->b);
    if ((anchorA.component == nullptr && anchorA.junction == nullptr) ||
        (anchorB.component == nullptr && anchorB.junction == nullptr)) {
        return;
    }
    auto* wireItem = new WireItem(document_, undoStack_, wireId, w->a, w->b, anchorA, anchorB);
    addItem(wireItem);
    // El constructor de WireItem ya llamo a updateGeometry() una vez, pero en
    // ese momento scene() todavia era nullptr (se llama antes del addItem() de
    // arriba) - se recalcula aca, con el item ya dentro de la escena.
    wireItem->updateGeometry();
    wireItems_[wireId] = wireItem;
}

void CircuitScene::onWireAboutToBeRemoved(uint32_t wireId) {
    const auto it = wireItems_.find(wireId);
    if (it == wireItems_.end()) {
        return;
    }
    removeItem(it->second);
    delete it->second;
    wireItems_.erase(it);
}

void CircuitScene::onWireGeometryChanged(uint32_t wireId) {
    const auto it = wireItems_.find(wireId);
    if (it != wireItems_.end()) {
        it->second->updateGeometry();
    }
}

void CircuitScene::onJunctionAdded(uint32_t junctionId) {
    auto* item = new JunctionItem(document_, junctionId);
    addItem(item);
    junctionItems_[junctionId] = item;
}

void CircuitScene::onJunctionAboutToBeRemoved(uint32_t junctionId) {
    const auto it = junctionItems_.find(junctionId);
    if (it == junctionItems_.end()) {
        return;
    }
    removeItem(it->second);
    delete it->second;
    junctionItems_.erase(it);
}

void CircuitScene::onJunctionPositionChanged(uint32_t junctionId) {
    const auto it = junctionItems_.find(junctionId);
    if (it != junctionItems_.end()) {
        it->second->setPos(document_->junctionPosition(junctionId));
    }
}

void CircuitScene::onPropertyChanged(uint32_t componentId) {
    const auto it = componentItems_.find(componentId);
    if (it == componentItems_.end()) {
        return;
    }
    it->second->rebuildPins();
    it->second->update();
}

void CircuitScene::onComponentPlacementChanged(uint32_t componentId) {
    const auto it = componentItems_.find(componentId);
    if (it != componentItems_.end()) {
        applyPlacement(it->second, document_->componentPlacement(componentId));
    }
}

void CircuitScene::onSimulationChanged() { update(); }

} // namespace digitalforge::editor
