#pragma once

#include <QGraphicsEllipseItem>

#include <cstdint>
#include <vector>

namespace digitalforge::editor {

class CircuitDocument;
class WireItem;

// Representa un punto de union libre (Junction, ver CircuitDocument.hpp):
// un punto donde 2 o mas cables se encuentran sin pasar por un pin de
// componente. Puramente presentacional -- lee su valor desde CircuitDocument
// en el momento de dibujar y nunca evalua logica por si mismo.
//
// Es arrastrable (ItemIsMovable) igual que un ComponentItem: al arrastrarlo,
// itemChange() reposiciona en vivo cada WireItem adjunto (ver
// attachedWires_), y SelectionTool arma un MoveJunctionCommand al soltar que
// persiste la posicion nueva en CircuitDocument -- ambos pasos son
// necesarios: sin el segundo, el punto de union visualmente "vuelve" a su
// posicion vieja la proxima vez que algo lo reconstruye desde el documento
// (el bug originalmente reportado, cuando esta clase todavia no persistia
// el arrastre). Para redibujar la ruta de un cable sin mover ningun
// extremo real, ver la edicion de waypoints en WireItem.
//
// Solo dibuja el punto (dot) cuando tiene grado >=2 (union electrica real);
// un junction en grado 1 (una punta de cable "al aire" tras borrar una de
// sus derivaciones) sigue existiendo, pero no pinta nada, para no leerse
// como una conexion que no existe.
class JunctionItem : public QGraphicsEllipseItem {
public:
    JunctionItem(CircuitDocument* document, uint32_t junctionId, QGraphicsItem* parent = nullptr);

    [[nodiscard]] uint32_t junctionId() const noexcept { return junctionId_; }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void addAttachedWire(WireItem* wire);
    void removeAttachedWire(WireItem* wire);

    static constexpr qreal kRadius = 3.0;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    CircuitDocument* document_;
    uint32_t junctionId_;
    std::vector<WireItem*> attachedWires_;
    bool hovered_ = false;
};

} // namespace digitalforge::editor
