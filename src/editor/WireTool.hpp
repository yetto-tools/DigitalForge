#pragma once

#include <QPointF>

#include "CircuitDocument.hpp"

class QGraphicsSceneMouseEvent;
class QGraphicsLineItem;
class QUndoStack;

namespace digitalforge::editor {

class CircuitScene;
class CircuitDocument;
class PinItem;
class JunctionItem;
class WireItem;

// Que hay bajo el cursor al empezar o terminar un gesto de cableado, en
// orden de prioridad: un pin, un punto de union ya existente, o el cuerpo de
// un cable ya trazado (una "derivacion pendiente" -- el punto exacto ya se
// conoce, pero el punto de union ahi todavia no se creo). A lo sumo uno de
// los tres punteros es no nulo.
struct WireGestureEndpoint {
    PinItem* pin = nullptr;
    JunctionItem* junction = nullptr;
    WireItem* pendingBranchWire = nullptr;
    QPointF anchorPos;

    [[nodiscard]] bool empty() const noexcept {
        return pin == nullptr && junction == nullptr && pendingBranchWire == nullptr;
    }
};

// Cableado por arrastre: al presionar sobre un pin, un punto de union ya
// existente, o el cuerpo de un cable ya trazado, se inicia una linea de
// vista previa punteada que sigue al cursor; al soltar sobre cualquiera de
// esos tres tipos de destino se envia el comando de undo correspondiente
// (AddWireCommand directo, o un macro con uno o dos SplitWireCommand +
// AddWireCommand si alguno de los dos extremos era una derivacion
// pendiente). Soltar en cualquier otro lugar, o sobre el mismo punto de
// partida, cancela el gesto.
class WireTool {
public:
    WireTool(CircuitScene* scene, CircuitDocument* document, QUndoStack* undoStack);

    void press(QGraphicsSceneMouseEvent* event);
    void move(QGraphicsSceneMouseEvent* event);
    void release(QGraphicsSceneMouseEvent* event);

private:
    [[nodiscard]] WireGestureEndpoint hitTest(QPointF scenePos) const;
    // Convierte un WireGestureEndpoint ya confirmado (extremo final del
    // gesto) en un WireEndpoint concreto, materializando una derivacion
    // pendiente en un punto de union nuevo (via SplitWireCommand) si hace falta.
    [[nodiscard]] WireEndpoint resolveEndpoint(const WireGestureEndpoint& hit);
    void cancel();

    CircuitScene* scene_;
    CircuitDocument* document_;
    QUndoStack* undoStack_;
    WireGestureEndpoint start_;
    QGraphicsLineItem* previewLine_ = nullptr;
};

} // namespace digitalforge::editor
