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

    // Limites del zoom, en factor de escala (1.0 = 100%). Mismo rango que usa
    // Excel en su control de la barra de estado.
    static constexpr qreal kMinZoom = 0.10;
    static constexpr qreal kMaxZoom = 4.00;

    // Factor de escala actual. La vista siempre escala x e y por igual, asi
    // que alcanza con la componente horizontal de la transformacion.
    [[nodiscard]] qreal zoomFactor() const;

public slots:
    void zoomIn();
    void zoomOut();
    void resetZoom();
    // Fija el zoom absoluto (se recorta a kMinZoom..kMaxZoom). Lo usa el
    // control de la barra de estado, donde el usuario elige el porcentaje.
    void setZoomFactor(qreal factor);

signals:
    // Emitida en cada cambio efectivo de escala, venga de la rueda, del menu o
    // del control de la barra de estado.
    void zoomChanged(qreal factor);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Zoom relativo (rueda, menu, botones +/-) conservando el anclaje bajo el
    // cursor; setZoomFactor() en cambio ancla al centro de la vista.
    void stepZoom(qreal multiplier);

    bool panning_ = false;
    QPoint lastPanPoint_;
};

} // namespace digitalforge::editor
