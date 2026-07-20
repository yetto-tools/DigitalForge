#include "CircuitView.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
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

void CircuitView::zoomIn() { scale(1.15, 1.15); }
void CircuitView::zoomOut() { scale(1.0 / 1.15, 1.0 / 1.15); }
void CircuitView::resetZoom() { resetTransform(); }

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

    auto* command = new PlaceComponentCommand(circuitScene->document(), typeId, {}, placement);
    circuitScene->undoStack()->push(command);
    circuitScene->selectComponent(command->componentId());
    event->acceptProposedAction();
}

} // namespace digitalforge::editor
