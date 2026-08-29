#pragma once

#include <QPointF>

#include <optional>
#include <vector>

#include "CircuitDocument.hpp"

class QGraphicsSceneMouseEvent;
class QGraphicsPathItem;
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
    // Cierra el trazado multi-segmento en curso conectando al destino bajo el
    // cursor (doble clic). No-op si no hay un destino valido ahi.
    void finishAt(QPointF scenePos);
    // Aborta un trazado en curso (Esc / clic derecho). Publico para que
    // CircuitScene lo dispare desde el teclado/menu contextual.
    void cancel();
    // True si hay un trazado multi-segmento en curso (para que CircuitScene
    // sepa que Esc/clic-derecho deben cancelarlo en vez de su accion normal).
    [[nodiscard]] bool isDrawing() const noexcept { return drawing_; }

private:
    // Eje del tramo que se esta trazando. Lo fija la direccion del primer
    // movimiento del mouse tras el ultimo punto fijado y NO cambia hasta que
    // se fija una esquina: asi, arrancar bajando hace crecer el cable hacia
    // abajo, y recien al moverse a los costados aparece la esquina y ese
    // segundo tramo se alarga. Si el eje se recalculara todo el tiempo, el
    // codo se daria vuelta solo al cruzar la diagonal.
    enum class Axis {
        None, // todavia sin movimiento suficiente para decidir
        Horizontal,
        Vertical,
    };

    [[nodiscard]] WireGestureEndpoint hitTest(QPointF scenePos) const;
    // Fija el eje si todavia no lo esta y el cursor ya se alejo lo bastante
    // del ultimo punto -- y en una direccion lo bastante dominante -- como para
    // que la eleccion sea inequivoca. Sobre la diagonal no fija nada.
    void updateAxis(QPointF cursorScenePos);
    // Ultimo vertice del trazado en curso: la ultima esquina acumulada, o el
    // ultimo punto fijado con clic si todavia no hay ninguna.
    [[nodiscard]] QPointF lastVertex() const;
    // Agrega una esquina cada vez que el cursor se desvia del eje actual mas
    // que el umbral de creacion, y la quita si vuelve sobre sus pasos por
    // debajo del umbral (mas chico) de retraccion. Moverse en diagonal va
    // dejando asi una escalera de escalones en angulo recto.
    void accumulateSteps(QPointF cursorScenePos);
    // La esquina del tramo en curso segun el eje fijado, o nullopt si el tramo
    // sale recto (no hace falta codo) o si todavia no hay eje.
    [[nodiscard]] std::optional<QPointF> autoCorner(QPointF from, QPointF to) const;
    // Los vertices del trazado en curso hasta `cursorScenePos`, incluida la
    // esquina automatica. Compartido por la vista previa y por el commit, para
    // que el cable que queda sea exactamente el que se estaba viendo.
    [[nodiscard]] std::vector<QPointF> pointsThrough(QPointF cursorScenePos) const;
    // Convierte un WireGestureEndpoint ya confirmado (extremo final del
    // gesto) en un WireEndpoint concreto, materializando una derivacion
    // pendiente en un punto de union nuevo (via SplitWireCommand) si hace falta.
    [[nodiscard]] WireEndpoint resolveEndpoint(const WireGestureEndpoint& hit);
    // Envia el/los comando(s) para crear el cable desde el extremo inicial,
    // pasando por los quiebres ya fijados, hasta `end`. Si `end` esta vacio
    // (soltar en el vacio), crea un punto de union libre en `endScenePos`
    // (snappeado) -- la conectividad geometrica decide si ahi toca algo.
    // Resetea el estado.
    void commitTo(const WireGestureEndpoint& end, QPointF endScenePos);
    void updatePreview(QPointF cursorScenePos);

    CircuitScene* scene_;
    CircuitDocument* document_;
    QUndoStack* undoStack_;

    // Estado de un trazado en curso. `startHit_` es el extremo inicial (pin/
    // union/derivacion); `points_` son sus vertices ya fijados en coordenadas
    // de escena, empezando por el ancla inicial (points_[0]) y siguiendo con
    // cada quiebre confirmado por clic. En modo arrastre simple points_ queda
    // con un solo elemento y el cable se cierra al soltar.
    bool drawing_ = false;
    WireGestureEndpoint startHit_;
    std::vector<QPointF> points_;
    // Esquinas generadas por el propio movimiento del mouse (ver
    // accumulateSteps()), todavia no confirmadas con un clic. Se pliegan sobre
    // points_ en cuanto el usuario fija un punto, y se descartan al cancelar.
    std::vector<QPointF> autoPoints_;
    QPointF pressPos_;
    QGraphicsPathItem* previewPath_ = nullptr;
    // Se reinicia a None cada vez que se fija una esquina: cada tramo elige su
    // propia direccion.
    Axis axis_ = Axis::None;
};

} // namespace digitalforge::editor
