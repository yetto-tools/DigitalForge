#pragma once

#include <QGraphicsEllipseItem>

#include <cstdint>

#include "core/Pin.hpp"

namespace digitalforge::editor {

class CircuitDocument;

// Un unico punto de conexion representado como un pequeno punto relleno,
// coloreado segun el valor actual de la red (net) a la que pertenece. Es
// puramente presentacional: lee su valor desde CircuitDocument en el momento
// de dibujar y nunca evalua logica.
class PinItem : public QGraphicsEllipseItem {
public:
    // `radius` por defecto es kRadius; io.ledMatrix pasa uno mayor (ver
    // ComponentItem::rebuildPins()) porque ahi el pin en si hace las veces
    // de celda de LED, no solo un punto de conexion chico.
    PinItem(CircuitDocument* document, uint32_t componentId, uint16_t pinIndex, core::PinDirection direction,
            QGraphicsItem* parent, qreal radius = kRadius);

    [[nodiscard]] uint32_t componentId() const noexcept { return componentId_; }
    [[nodiscard]] uint16_t pinIndex() const noexcept { return pinIndex_; }
    [[nodiscard]] core::PinDirection direction() const noexcept { return direction_; }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    // Ampliado respecto del circulo del pin para que quepa el halo de hover
    // sin dejar artefactos de repintado (Qt recorta el pintado al boundingRect).
    [[nodiscard]] QRectF boundingRect() const override;

    static constexpr qreal kRadius = 3.0;

protected:
    // Aceptar el evento de presion (en lugar de dejarlo sin manejar)
    // mantiene a este item como el "mouse grabber" de Qt durante todo el
    // gesto de presion/movimiento/liberacion e impide que el evento se
    // propague hacia el ComponentItem padre (que es movible y de otro modo
    // empezaria a arrastrar todo el componente en lugar de iniciar un
    // cable). CircuitScene igualmente siempre llama primero a
    // QGraphicsScene::mousePressEvent()/mouseReleaseEvent() y superpone el
    // dibujo de cables sobre eso, en lugar de saltarse por completo el
    // seguimiento interno del grabber de Qt - omitirlo dejaba inconsistente
    // el seguimiento interno de foco/grabber de Qt y provocaba mas adelante
    // un cierre inesperado intermitente, en un evento diferido no
    // relacionado.
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    CircuitDocument* document_;
    uint32_t componentId_;
    uint16_t pinIndex_;
    core::PinDirection direction_;
    bool hovered_ = false;
};

} // namespace digitalforge::editor
