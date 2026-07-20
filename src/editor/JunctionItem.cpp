#include "JunctionItem.hpp"

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
    setPen(QPen(Qt::black, 1.0));
    setZValue(-0.5); // sobre los cables (zValue -1), debajo de los cuerpos de componentes
    setPos(document_->junctionPosition(junctionId_));
}

void JunctionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    if (document_->wiresAttachedToJunction(junctionId_).size() < 2) {
        return;
    }
    const core::LogicValue value = document_->endpointValue(WireEndpoint::junction(junctionId_));
    const bool selected = (option->state & QStyle::State_Selected) != 0;
    painter->setPen(QPen(Qt::black, selected ? 2.0 : 1.0));
    painter->setBrush(logicValueColor(value));
    painter->drawEllipse(rect());
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
