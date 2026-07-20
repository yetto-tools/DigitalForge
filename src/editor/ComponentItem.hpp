#pragma once

#include <QGraphicsItem>
#include <QPainterPath>

#include <cstdint>
#include <vector>

#include "core/Pin.hpp"

namespace digitalforge::editor {

class CircuitDocument;
class PinItem;
class WireItem;

// Renderiza un ComponentInstance colocado: cuerpo, etiqueta y pines.
// Puramente presentacional - consulta a CircuitDocument los valores actuales
// en el momento de pintar y nunca evalua ninguna logica por si mismo. La
// posicion/rotacion las aplica CircuitScene en respuesta a
// CircuitDocument::componentPlacementChanged; este item nunca apila comandos
// de undo por su cuenta (lo hace SelectionTool, comparando posiciones a lo
// largo de un gesto de arrastre).
class ComponentItem : public QGraphicsItem {
public:
    ComponentItem(CircuitDocument* document, uint32_t componentId, QGraphicsItem* parent = nullptr);

    [[nodiscard]] uint32_t componentId() const noexcept { return componentId_; }

    QRectF boundingRect() const override;
    // Sin esto, Qt usa por defecto un QPainterPath derivado de
    // boundingRect() para hit-testing (seleccion por clic, CircuitScene::
    // items()/tryToggleInput()) - esa caja incluye la franja de la
    // etiqueta de instancia debajo del cuerpo y el margen del stub de pin
    // (io.seven_segment/io.hexDisplay), asi que un clic ahi (visualmente
    // "afuera" del componente) igual contaba como un clic *sobre* el
    // componente (el bug reportado: conmutaba una entrada haciendo clic
    // fuera de su contorno durante la simulacion). Se acota al cuerpo
    // real (0,0,width_,height_) - no es el contorno exacto de cada forma
    // (el "D" de AND, el trapecio de un mux, etc.), pero excluye los dos
    // casos obvios de "afuera".
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // Vuelve a derivar la disposicion de pines a partir del ComponentInstance
    // actual (llamar tras un cambio de propiedad que pueda haber alterado el
    // numero de pines).
    void rebuildPins();

    [[nodiscard]] QPointF pinScenePos(uint16_t pinIndex) const;
    [[nodiscard]] std::size_t pinCount() const noexcept { return pinLocalPositions_.size(); }

    void addAttachedWire(WireItem* wire);
    void removeAttachedWire(WireItem* wire);

    // Multiplo de 8 a proposito: width_/pinPitch de cada tipo de componente
    // tambien se eligen como multiplos de 8 (ver ComponentItem::rebuildPins),
    // para que sus bordes y pines siempre caigan sobre esta grilla sin
    // importar cuantos pines tenga cada uno.
    static constexpr qreal kGridSize = 8.0;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    void paintGeneric(QPainter* painter, bool selected);
    void paintGate(QPainter* painter, bool selected);
    void paintInput(QPainter* painter, bool selected);
    void paintClock(QPainter* painter, bool selected);
    void paintPowerOnReset(QPainter* painter, bool selected);
    void paintOutput(QPainter* painter, bool selected);
    void paintPullResistor(QPainter* painter, bool selected);
    void paintDoNotConnect(QPainter* painter, bool selected);
    void paintTunnel(QPainter* painter, bool selected);
    void paintGround(QPainter* painter, bool selected);
    void paintTransistor(QPainter* painter, bool selected);
    void paintTransmissionGate(QPainter* painter, bool selected);
    void paintLed(QPainter* painter, bool selected);
    void paintSevenSegment(QPainter* painter, bool selected);
    void paintPlexer(QPainter* painter, bool selected);
    void paintArithmetic(QPainter* painter, bool selected);
    void paintMemory(QPainter* painter, bool selected);
    void paintSubcircuit(QPainter* painter, bool selected);
    void paintIc74ls(QPainter* painter, bool selected);
    void paintHexDisplay(QPainter* painter, bool selected);
    void paintLedMatrix(QPainter* painter, bool selected);
    void paintTerminal(QPainter* painter, bool selected);

    CircuitDocument* document_;
    uint32_t componentId_;
    qreal width_ = 100.0;  // antes 100
    qreal height_ = 60.0; // antes 60
    // Distancia (hacia x negativo) entre el borde izquierdo de la caja y los
    // pines de entrada; 0 para todos los tipos salvo io.seven_segment, cuyos
    // pines quedaban pegados contra el display. Ver rebuildPins()/
    // paintSevenSegment().
    qreal pinStubLength_ = 0.0;
    std::vector<QPointF> pinLocalPositions_;
    // Posiciones de los pines decorativos de un ic74ls.* (ver
    // components::ComponentDefinition::PhysicalPin/physicalPinout) - pines
    // reales del chip fisico que este simulador no modela electricamente
    // (PR/CLR de un flip-flop, VCC/GND, etc.), dibujados en paintIc74ls()
    // con nombre + linea de pin pero sin punto de conexion (no son
    // PinItem, no son cableables), en su posicion fisica real dentro de la
    // secuencia del DIP. Vacios para cualquier tipo que no sea ic74ls.*.
    std::vector<QPointF> decorativeLeftPositions_;
    std::vector<QPointF> decorativeRightPositions_;
    std::vector<PinItem*> pinItems_;
    std::vector<WireItem*> attachedWires_;
};

} // namespace digitalforge::editor
