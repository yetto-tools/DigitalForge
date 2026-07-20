#include "PinItem.hpp"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>

#include "CircuitDocument.hpp"
#include "LogicColors.hpp"
#include "components/BasicComponentLibrary.hpp"

namespace digitalforge::editor {

namespace {
// Duplica deliberadamente el mismo criterio que
// ComponentItem.cpp::ledBaseColor() (privado a ese archivo) para la
// propiedad "color" de io.ledMatrix - mismo motivo que labelInkColor() ahi:
// evitar acoplar PinItem a un helper interno de otro .cpp por una funcion
// de 5 lineas.
QColor ledMatrixBaseColor(const std::string& colorName) {
    if (colorName == "green") return QColor(40, 220, 90);
    if (colorName == "yellow") return QColor(255, 215, 40);
    if (colorName == "blue") return QColor(60, 130, 255);
    if (colorName == "white") return QColor(235, 235, 235);
    return QColor(255, 50, 50); // rojo (por defecto)
}
} // namespace

PinItem::PinItem(CircuitDocument* document, uint32_t componentId, uint16_t pinIndex, core::PinDirection direction,
                  QGraphicsItem* parent, qreal radius)
    : QGraphicsEllipseItem(-radius, -radius, 2 * radius, 2 * radius, parent),
      document_(document),
      componentId_(componentId),
      pinIndex_(pinIndex),
      direction_(direction) {
    setPen(QPen(Qt::black, 1.0));
    setBrush(Qt::black);
    setAcceptHoverEvents(true);
}

void PinItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    const core::LogicValue value = document_->pinValue(componentId_, pinIndex_);
    // io.ledMatrix posiciona cada pin exactamente sobre su propia celda (ver
    // ComponentItem::rebuildPins()) en vez de en el borde izquierdo/derecho
    // como el resto de los tipos - el pin en si ES la celda, asi que se
    // colorea con el mismo criterio rojo encendido/gris apagado que un LED
    // real (respetando "activeHigh") en vez del esquema generico de 5
    // valores, para no dibujar dos circulos superpuestos con semantica
    // distinta.
    const components::ComponentInstance* instance = document_->component(componentId_);
    if (instance != nullptr && instance->typeId() == "io.ledMatrix") {
        const bool lit = components::ledMatrixCellIsLit(*instance, value);
        const bool conflict = value == core::LogicValue::Error;
        const QColor baseColor = ledMatrixBaseColor(std::get<std::string>(instance->property("color")));
        const QColor color = conflict ? QColor(220, 30, 30) : (lit ? baseColor : baseColor.darker(380));
        painter->setPen(QPen(color.darker(150), 1.0));
        painter->setBrush(color);
        painter->drawEllipse(rect());
        return;
    }
    painter->setPen(QPen(Qt::black, 1.0));
    painter->setBrush(logicValueColor(value));
    painter->drawEllipse(rect());
}

void PinItem::mousePressEvent(QGraphicsSceneMouseEvent* event) { event->accept(); }
void PinItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) { event->accept(); }
void PinItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) { event->accept(); }

} // namespace digitalforge::editor
