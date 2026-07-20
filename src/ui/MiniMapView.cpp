#include "MiniMapView.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>

#include "editor/CircuitScene.hpp"
#include "editor/CircuitView.hpp"

namespace digitalforge::ui {

namespace {
// Margen (en unidades de escena) alrededor de itemsBoundingRect() al
// reencuadrar - sin esto, los componentes del borde quedarian pegados al
// marco del minimapa.
constexpr qreal kFitMargin = 40.0;
} // namespace

MiniMapView::MiniMapView(QWidget* parent) : QGraphicsView(parent) {
    setInteractive(false); // solo lectura -- ver el comentario de clase en el .hpp
    setDragMode(QGraphicsView::NoDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setRenderHint(QPainter::Antialiasing, true);
    setFrameShape(QFrame::NoFrame);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(120);
    // Ahora vive arriba de la lista de Componentes, dentro del mismo dock
    // (ver MainWindow::setupDocks()) - sin este techo se repartiria el
    // espacio disponible a la mitad con esa lista, que es lo que se usa
    // todo el tiempo mientras se arma un circuito.
    setMaximumHeight(180);

    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, [this] {
        if (isVisible()) {
            viewport()->update();
        }
    });
    refreshTimer_->start(150);
}

void MiniMapView::setTarget(editor::CircuitScene* scene, editor::CircuitView* mainView) {
    mainView_ = mainView;
    if (scene != this->scene()) {
        setScene(scene);
    }
    refit();
}

void MiniMapView::refit() {
    if (scene() == nullptr) {
        return;
    }
    const QRectF bounds = scene()->itemsBoundingRect();
    if (bounds.isEmpty()) {
        return; // circuito vacio -- nada que encuadrar todavia
    }
    fitInView(bounds.adjusted(-kFitMargin, -kFitMargin, kFitMargin, kFitMargin), Qt::KeepAspectRatio);
}

void MiniMapView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    refit();
}

void MiniMapView::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawForeground(painter, rect);
    if (mainView_ == nullptr) {
        return;
    }
    const QRectF visibleSceneRect = mainView_->mapToScene(mainView_->viewport()->rect()).boundingRect();
    painter->save();
    painter->setPen(QPen(QColor(255, 200, 0), 0)); // ancho 0 = siempre 1px de pantalla, sin importar el zoom del minimapa
    painter->setBrush(QColor(255, 200, 0, 40));
    painter->drawRect(visibleSceneRect);
    painter->restore();
}

void MiniMapView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || mainView_ == nullptr) {
        return;
    }
    dragging_ = true;
    centerMainViewAt(event->pos());
}

void MiniMapView::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        centerMainViewAt(event->pos());
    }
}

void MiniMapView::mouseReleaseEvent(QMouseEvent*) { dragging_ = false; }

void MiniMapView::centerMainViewAt(const QPoint& viewportPos) {
    mainView_->centerOn(mapToScene(viewportPos));
}

} // namespace digitalforge::ui
