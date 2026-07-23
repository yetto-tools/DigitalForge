#include "CircuitView.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "CircuitScene.hpp"
#include "ComponentItem.hpp"
#include "DragDrop.hpp"
#include "UndoCommands.hpp"

namespace digitalforge::editor {

CircuitView::CircuitView(CircuitScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setDragMode(QGraphicsView::RubberBandDrag);
    setRenderHint(QPainter::Antialiasing, true);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setMouseTracking(true);
    setAcceptDrops(true);
}

qreal CircuitView::zoomFactor() const { return transform().m11(); }

namespace {
// Escala absoluta (no relativa) recortada a los limites: es lo que necesita el
// control de la barra de estado -el usuario elige el porcentaje, no un
// incremento- y de paso hace exacto el recorte, que con scale() relativo se
// podia pasar de largo indefinidamente. Devuelve el factor resultante, o 0 si
// no hubo cambio.
qreal applyAbsoluteZoom(QGraphicsView& view, qreal factor, qreal current) {
    const qreal target = std::clamp(factor, CircuitView::kMinZoom, CircuitView::kMaxZoom);
    if (current <= 0.0 || qFuzzyCompare(target, current)) {
        return 0.0;
    }
    view.scale(target / current, target / current);
    return target;
}
} // namespace

void CircuitView::zoomIn() { stepZoom(1.15); }
void CircuitView::zoomOut() { stepZoom(1.0 / 1.15); }
void CircuitView::resetZoom() { setZoomFactor(1.0); }

void CircuitView::stepZoom(qreal multiplier) {
    // Conserva el anclaje configurado (AnchorUnderMouse): al usar la rueda el
    // punto bajo el cursor se queda quieto, que es lo esperable.
    const qreal applied = applyAbsoluteZoom(*this, zoomFactor() * multiplier, zoomFactor());
    if (applied > 0.0) {
        emit zoomChanged(applied);
    }
}

void CircuitView::setZoomFactor(qreal factor) {
    // Ancla al centro de la vista mientras dure el cambio: este camino lo usa
    // el control de la barra de estado, donde el cursor esta fuera del
    // viewport y anclar bajo el mouse desplazaria el lienzo de forma arbitraria.
    const QGraphicsView::ViewportAnchor previousAnchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    const qreal applied = applyAbsoluteZoom(*this, factor, zoomFactor());
    setTransformationAnchor(previousAnchor);
    if (applied > 0.0) {
        emit zoomChanged(applied);
    }
}

void CircuitView::wheelEvent(QWheelEvent* event) {
    // Shift+rueda: desplazamiento horizontal. Ctrl+rueda: desplazamiento
    // vertical. Rueda sola (sin modificador): zoom, sin cambios - mismo
    // esquema que Proteus.
    if ((event->modifiers() & Qt::ShiftModifier) != 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - event->angleDelta().y());
        event->accept();
        return;
    }
    if ((event->modifiers() & Qt::ControlModifier) != 0) {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - event->angleDelta().y());
        event->accept();
        return;
    }
    if (event->angleDelta().y() > 0) {
        zoomIn();
    } else if (event->angleDelta().y() < 0) {
        zoomOut();
    }
    event->accept();
}

void CircuitView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastPanPoint_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CircuitView::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QPoint delta = event->pos() - lastPanPoint_;
        lastPanPoint_ = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CircuitView::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_ && event->button() == Qt::MiddleButton) {
        panning_ = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void CircuitView::dragEnterEvent(QDragEnterEvent* event) {
    // Siempre se deja que QGraphicsView inicialice primero su propio estado
    // interno de seguimiento del drag. Omitir esto (como hacia antes este
    // codigo) dejaba ese estado inconsistente para cuando llegaba el
    // dragLeaveEvent correspondiente ("drag leave recibido antes de drag
    // enter"), lo cual podia provocar mas adelante un crash en un evento
    // diferido no relacionado - la misma clase de bug que se corrigio en
    // PinItem para las pulsaciones de raton simples.
    QGraphicsView::dragEnterEvent(event);
    if (event->mimeData()->hasFormat(kComponentDragMimeType)) {
        event->acceptProposedAction();
    }
}

void CircuitView::dragMoveEvent(QDragMoveEvent* event) {
    QGraphicsView::dragMoveEvent(event);
    if (event->mimeData()->hasFormat(kComponentDragMimeType)) {
        event->acceptProposedAction();
    }
}

void CircuitView::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat(kComponentDragMimeType)) {
        QGraphicsView::dropEvent(event);
        return;
    }
    auto* circuitScene = qobject_cast<CircuitScene*>(scene());
    if (circuitScene == nullptr) {
        return;
    }
    const std::string typeId = QString::fromUtf8(event->mimeData()->data(kComponentDragMimeType)).toStdString();
    const qreal grid = ComponentItem::kGridSize;
    const QPointF scenePos = mapToScene(event->position().toPoint());
    const ComponentPlacement placement{
        QPointF(std::round(scenePos.x() / grid) * grid, std::round(scenePos.y() / grid) * grid), 0};

    // Antes de colocar: soltar aqui satisface por completo la intencion del
    // usuario, asi que cualquier colocacion pendiente queda sin efecto. Sin
    // esto se duplicaba el componente - arrastrar desde la paleta un item que
    // ya estaba seleccionado hace que el press que inicia el arrastre cuente
    // ademas como doble clic, y ese doble clic deja armado el PlacementTool;
    // el primer clic posterior en el lienzo (tipicamente el que se hace para
    // seleccionar el componente recien soltado) colocaba un segundo ejemplar.
    circuitScene->cancelPlacement();

    auto* command = new PlaceComponentCommand(circuitScene->document(), typeId, {}, placement);
    circuitScene->undoStack()->push(command);
    circuitScene->selectComponent(command->componentId());
    event->acceptProposedAction();
}

} // namespace digitalforge::editor
