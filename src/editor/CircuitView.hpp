#pragma once

#include <QGraphicsView>
#include <QPoint>

namespace digitalforge::editor {

class CircuitScene;

// Puramente presentacional: el zoom, el desplazamiento (panning) y la
// seleccion por goma elastica (rubber-band) son cuestiones exclusivas del
// viewport/vista y nunca tocan CircuitDocument.
class CircuitView : public QGraphicsView {
    Q_OBJECT

public:
    explicit CircuitView(CircuitScene* scene, QWidget* parent = nullptr);

public slots:
    void zoomIn();
    void zoomOut();
    void resetZoom();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool panning_ = false;
    QPoint lastPanPoint_;
};

} // namespace digitalforge::editor
