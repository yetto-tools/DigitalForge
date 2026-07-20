#pragma once

#include <QGraphicsView>

class QTimer;

namespace digitalforge::editor {
class CircuitScene;
class CircuitView;
} // namespace digitalforge::editor

namespace digitalforge::ui {

// Miniatura del lienzo activo, estilo Proteus: muestra todo el circuito
// achicado para que entre entero, dibuja encima un rectangulo con el area
// que CircuitView esta mostrando ahora mismo, y permite centrar la vista
// principal ahi con un clic o arrastre - util para orientarse en un
// circuito grande sin tener que alejar el zoom de la vista real.
//
// Es una QGraphicsView de verdad sobre la MISMA CircuitScene (no un
// QWidget que reimplemente el dibujado a mano): setInteractive(false) +
// las barras de desplazamiento apagadas la vuelven puramente de lectura,
// asi que el resto de Qt sigue dibujando componentes/cables/etc
// exactamente como en la vista real, sin duplicar logica de pintado.
class MiniMapView : public QGraphicsView {
    Q_OBJECT

public:
    explicit MiniMapView(QWidget* parent = nullptr);

    // Cambia que CircuitScene/CircuitView se estan mostrando en miniatura -
    // MainWindow lo llama al cambiar de documento activo, mismo patron que
    // PropertyInspector::setDocument(). `scene` puede ser el mismo objeto
    // que ya se le paso antes (no-op en ese caso); `mainView` es la
    // CircuitView cuyo rectangulo visible se dibuja encima y que se recentra
    // al interactuar con el minimapa.
    void setTarget(editor::CircuitScene* scene, editor::CircuitView* mainView);

protected:
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // Reencuadra la escena completa para que entre en el viewport del
    // minimapa (con un margen), preservando la relacion de aspecto -
    // llamado al cambiar de escena y en cada resizeEvent().
    void refit();
    // Centra CircuitView en el punto de escena correspondiente a `viewportPos`
    // (coordenadas locales de este widget).
    void centerMainViewAt(const QPoint& viewportPos);

    editor::CircuitView* mainView_ = nullptr;
    bool dragging_ = false;
    // Sondea periodicamente en vez de intentar enganchar cada gesto posible
    // que puede cambiar el area visible de CircuitView (rueda, arrastre con
    // el boton del medio, teclado, autoscroll de un drag-drop, redimensionar
    // la ventana...) - mas simple y robusto queintentar cubrir cada uno por
    // separado, y el costo de un update() de mas cada rato es insignificante.
    QTimer* refreshTimer_ = nullptr;
};

} // namespace digitalforge::ui
