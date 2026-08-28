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
// Siempre dibuja un punto, agarrable igual que un ComponentItem -- un
// junction en grado 1 (una punta de cable "al aire": recien soltada en el
// vacio al trazar un cable, o lo que sobrevive tras borrar 2 de sus 3
// derivaciones en el mismo gesto) se dibuja hueco/tenue en vez de relleno de
// color logico, para que se lea como "hay algo agarrable aca" sin
// confundirse con una union electrica real de grado >=2 (antes no se
// dibujaba nada en absoluto ahi, dejando ese extremo invisible e
// inencontrable para arrastrarlo -- el defecto reportado).
class JunctionItem : public QGraphicsEllipseItem {
public:
    JunctionItem(CircuitDocument* document, uint32_t junctionId, QGraphicsItem* parent = nullptr);

    [[nodiscard]] uint32_t junctionId() const noexcept { return junctionId_; }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void addAttachedWire(WireItem* wire);
    void removeAttachedWire(WireItem* wire);
    [[nodiscard]] const std::vector<WireItem*>& attachedWires() const noexcept { return attachedWires_; }

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
