#include "JunctionItem.hpp"

#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <algorithm>

#include "CircuitDocument.hpp"
#include "CircuitScene.hpp"
#include "ComponentItem.hpp"
#include "GridSnap.hpp"
#include "LogicColors.hpp"
#include "WireItem.hpp"

namespace digitalforge::editor {

JunctionItem::JunctionItem(CircuitDocument* document, uint32_t junctionId, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-kRadius, -kRadius, 2 * kRadius, 2 * kRadius, parent),
      document_(document),
      junctionId_(junctionId) {
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setAcceptHoverEvents(true);
    setPen(QPen(Qt::black, 1.0));
    setZValue(-0.5); // sobre los cables (zValue -1), debajo de los cuerpos de componentes
    setPos(document_->junctionPosition(junctionId_));
}

void JunctionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    const bool selected = (option->state & QStyle::State_Selected) != 0;
    // Al pasar el mouse, agrandar un poco el punto para que se note que es
    // agarrable/arrastrable, sin cambiar su semantica de color.
    const QRectF r = hovered_ ? rect().adjusted(-1.5, -1.5, 1.5, 1.5) : rect();

    // Halo del nodo resaltado -- ver WireItem::paint() para el mismo criterio
    // (debajo del punto normal, no reemplaza su color/estilo).
    if (highlighted_) {
        painter->setPen(QPen(QColor(0, 200, 255, 130), 4.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(r.adjusted(-2.5, -2.5, 2.5, 2.5));
    }

    if (document_->wiresAttachedToJunction(junctionId_).size() < 2) {
        // Punta de cable "al aire": hueca y en gris (nunca el color de
        // ningun valor logico real, para no sugerir una conexion que no
        // existe), pero SIEMPRE dibujada -- sigue siendo un punto agarrable.
        painter->setPen(QPen(QColor(150, 150, 150), selected ? 2.0 : 1.0, selected ? Qt::SolidLine : Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(r);
        return;
    }

    const core::LogicValue value = document_->endpointValue(WireEndpoint::junction(junctionId_));
    painter->setPen(QPen(Qt::black, selected ? 2.0 : 1.0));
    painter->setBrush(logicValueColor(value));
    painter->drawEllipse(r);
}

void JunctionItem::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    hovered_ = true;
    update();
}

void JunctionItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    hovered_ = false;
    update();
}

void JunctionItem::setHighlighted(bool highlighted) {
    if (highlighted_ == highlighted) {
        return;
    }
    highlighted_ = highlighted;
    update();
}

void JunctionItem::addAttachedWire(WireItem* wire) { attachedWires_.push_back(wire); }

void JunctionItem::removeAttachedWire(WireItem* wire) {
    attachedWires_.erase(std::remove(attachedWires_.begin(), attachedWires_.end(), wire), attachedWires_.end());
}

QVariant JunctionItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == QGraphicsItem::ItemPositionChange) {
        if (document_->isLiveSimulation()) {
            // Mismo criterio que ComponentItem: no se permite reubicar nada
            // mientras la simulacion esta en ejecucion.
            return pos();
        }
        auto* circuitScene = qobject_cast<CircuitScene*>(scene());
        if (circuitScene != nullptr && circuitScene->snapToGridEnabled()) {
            return snapToGrid(value.toPointF(), ComponentItem::kGridSize);
        }
    } else if (change == QGraphicsItem::ItemPositionHasChanged) {
        for (WireItem* wire : attachedWires_) {
            wire->updateGeometry();
        }
    }
    return QGraphicsItem::itemChange(change, value);
}

} // namespace digitalforge::editor
