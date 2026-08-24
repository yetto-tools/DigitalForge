#pragma once

#include <QPointF>
#include <QUndoCommand>

#include <optional>
#include <string>
#include <vector>

#include "CircuitDocument.hpp"
#include "components/Property.hpp"

namespace digitalforge::editor {

// Todos los comandos siguientes comparten la misma estructura: la identidad
// (componentId/wireId) se decide una sola vez, por adelantado (reservada del
// documento, o capturada del objeto que se va a eliminar), y redo()/undo()
// solo reproducen la mutacion del documento - nunca la asignacion de
// identidad. Eso mantiene los ciclos repetidos de deshacer/rehacer
// idempotentes y conserva validas, a lo largo del ciclo, las referencias
// almacenadas por cualquier otro comando (por ejemplo, el componentId del
// extremo de un cable).

class PlaceComponentCommand : public QUndoCommand {
public:
    PlaceComponentCommand(CircuitDocument* document, std::string typeId, components::PropertyMap overrides,
                           ComponentPlacement placement, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

    [[nodiscard]] uint32_t componentId() const noexcept { return componentId_; }

private:
    CircuitDocument* document_;
    std::string typeId_;
    components::PropertyMap overrides_;
    ComponentPlacement placement_;
    uint32_t componentId_;
};

class DeleteComponentCommand : public QUndoCommand {
public:
    DeleteComponentCommand(CircuitDocument* document, uint32_t componentId, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

private:
    CircuitDocument* document_;
    uint32_t componentId_;
    std::string typeId_;
    components::PropertyMap properties_;
    ComponentPlacement placement_;
    std::vector<WireConnection> attachedWires_;
    // Puntos de union que quedarian huerfanos (grado 0) al eliminar
    // attachedWires_; se restauran en undo() antes que los propios cables.
    std::vector<Junction> orphanedJunctions_;
};

class MoveComponentCommand : public QUndoCommand {
public:
    MoveComponentCommand(CircuitDocument* document, uint32_t componentId, QPointF oldPosition, QPointF newPosition,
                          QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    void apply(QPointF position);

    CircuitDocument* document_;
    uint32_t componentId_;
    QPointF oldPosition_;
    QPointF newPosition_;
};

// Reubica la etiqueta de instancia de un componente (ComponentPlacement::
// labelOffset) -- lo empuja ComponentItem mismo al final de un arrastre de la
// etiqueta (ver ComponentItem::mouseReleaseEvent()), nunca SelectionTool: es
// un gesto propio del item, no una reubicacion del componente entero.
class SetLabelOffsetCommand : public QUndoCommand {
public:
    SetLabelOffsetCommand(CircuitDocument* document, uint32_t componentId, QPointF oldOffset, QPointF newOffset,
                           QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    void apply(QPointF offset);

    CircuitDocument* document_;
    uint32_t componentId_;
    QPointF oldOffset_;
    QPointF newOffset_;
};

class MoveJunctionCommand : public QUndoCommand {
public:
    MoveJunctionCommand(CircuitDocument* document, uint32_t junctionId, QPointF oldPosition, QPointF newPosition,
                         QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    void apply(QPointF position);

    CircuitDocument* document_;
    uint32_t junctionId_;
    QPointF oldPosition_;
    QPointF newPosition_;
};

class RotateComponentCommand : public QUndoCommand {
public:
    // newRotationDegrees debe ser uno de 0/90/180/270.
    RotateComponentCommand(CircuitDocument* document, uint32_t componentId, int oldRotationDegrees,
                            int newRotationDegrees, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

private:
    void apply(int rotationDegrees);

    CircuitDocument* document_;
    uint32_t componentId_;
    int oldRotationDegrees_;
    int newRotationDegrees_;
};

// Cambia ComponentPlacement::zOrder -- ver CircuitScene::bringSelectedToFront()
// y companeros (Traer al frente/Enviar al fondo/etc, menu contextual estilo
// draw.io). Misma estructura que RotateComponentCommand (fetch-modify-set
// sobre CircuitDocument::setComponentPlacement, sin id()/mergeWith porque
// cada click del menu contextual es su propio paso de deshacer).
class SetZOrderCommand : public QUndoCommand {
public:
    SetZOrderCommand(CircuitDocument* document, uint32_t componentId, int oldZOrder, int newZOrder,
                      QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

private:
    void apply(int zOrder);

    CircuitDocument* document_;
    uint32_t componentId_;
    int oldZOrder_;
    int newZOrder_;
};

class AddWireCommand : public QUndoCommand {
public:
    AddWireCommand(CircuitDocument* document, WireEndpoint a, WireEndpoint b, std::vector<QPointF> waypoints = {},
                   QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

    [[nodiscard]] uint32_t wireId() const noexcept { return wireId_; }

private:
    CircuitDocument* document_;
    uint32_t wireId_;
    WireEndpoint a_;
    WireEndpoint b_;
    std::vector<QPointF> waypoints_;
};

// Crea un punto de union libre (sin cables todavia) en una posicion dada -- se
// usa para terminar un cable en el vacio (el cable que lo acompana lo lleva a
// grado 1; la conectividad geometrica decide si ese punto toca algo mas). En
// undo se elimina solo si quedo sin cables (lo normal, ya que el AddWireCommand
// que lo acompana en el mismo macro se deshace primero y lo deja en grado 0).
class AddJunctionCommand : public QUndoCommand {
public:
    AddJunctionCommand(CircuitDocument* document, QPointF position, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

    [[nodiscard]] uint32_t junctionId() const noexcept { return junctionId_; }

private:
    CircuitDocument* document_;
    uint32_t junctionId_;
    QPointF position_;
};

class DeleteWireCommand : public QUndoCommand {
public:
    DeleteWireCommand(CircuitDocument* document, uint32_t wireId, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

private:
    CircuitDocument* document_;
    WireConnection wire_;
    // Puntos de union que quedarian huerfanos (grado 0) si este cable se
    // elimina; se restauran en undo() antes que el propio cable, para que
    // sus extremos vuelvan a ser validos.
    std::vector<Junction> orphanedJunctions_;
};

// Reemplaza el cable `existingWireId` por dos cables nuevos que se
// encuentran en un punto de union nuevo en `splitPosition` -- el mecanismo
// detras de "empezar/terminar un cable en la mitad de otro" (derivacion en
// T). Sigue la misma convencion de reservar toda la identidad (junctionId,
// los dos wireId nuevos) en el constructor.
class SplitWireCommand : public QUndoCommand {
public:
    // `waypointsBefore`/`waypointsAfter` son los quiebres que le quedan a cada
    // segmento nuevo (ver WireItem::splitWaypointsAt()/WireRouting::
    // splitWireWaypoints()), para que partir un cable en una derivacion en T
    // no le borre el trazado ya acomodado. Vacios por defecto para el caso
    // simple (cable recto sin quiebres, o llamador que no los necesita).
    SplitWireCommand(CircuitDocument* document, uint32_t existingWireId, QPointF splitPosition,
                      std::vector<QPointF> waypointsBefore = {}, std::vector<QPointF> waypointsAfter = {},
                      QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

    [[nodiscard]] uint32_t junctionId() const noexcept { return junctionId_; }

private:
    CircuitDocument* document_;
    uint32_t originalWireId_;
    WireConnection originalWire_;
    QPointF splitPosition_;
    std::vector<QPointF> waypointsBefore_;
    std::vector<QPointF> waypointsAfter_;
    uint32_t junctionId_;
    uint32_t wireId1_;
    uint32_t wireId2_;
};

// El inverso de SplitWireCommand: reemplaza los dos cables `wire1`/`wire2`
// que se encuentran en `junctionId` (se asume en grado exactamente 2, sin
// ninguna otra derivacion) por un unico cable nuevo entre sus otros dos
// extremos. Sana la fragmentacion que deja un split cuando la derivacion que
// lo origino se borra despues -- sin esto, el punto de union sobrevive para
// siempre en grado 2, con su punto visible aunque ya no marque una
// bifurcacion real (JunctionItem::paint() solo lo oculta en grado <2).
class MergeJunctionCommand : public QUndoCommand {
public:
    // `mergedWaypoints` es el trazado ya resuelto para el cable unico (ver
    // CircuitScene::mergedWaypointsAcrossJunction(), que concatena los
    // waypoints de wire1/wire2 en el orden correcto y colapsa el punto de
    // union si quedo colineal entre sus vecinos).
    MergeJunctionCommand(CircuitDocument* document, uint32_t junctionId, WireConnection wire1, WireConnection wire2,
                          std::vector<QPointF> mergedWaypoints, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

private:
    CircuitDocument* document_;
    uint32_t junctionId_;
    QPointF junctionPosition_;
    WireConnection wire1_;
    WireConnection wire2_;
    std::vector<QPointF> mergedWaypoints_;
    uint32_t mergedWireId_;
};

// Cambia los puntos de quiebre presentacionales de un cable (ver
// WireConnection::waypoints); fusionable dentro de un mismo gesto de
// arrastre, igual que MoveComponentCommand.
class SetWireWaypointsCommand : public QUndoCommand {
public:
    SetWireWaypointsCommand(CircuitDocument* document, uint32_t wireId, std::vector<QPointF> oldWaypoints,
                             std::vector<QPointF> newWaypoints, QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    void apply(const std::vector<QPointF>& waypoints);

    CircuitDocument* document_;
    uint32_t wireId_;
    std::vector<QPointF> oldWaypoints_;
    std::vector<QPointF> newWaypoints_;
};

// Reconecta uno de los dos extremos de un cable a un nuevo destino (pin o
// punto de union) sin borrar el otro extremo -- el mecanismo detras de
// "agarrar la punta de un cable y llevarla a otro pin". Si al mover el extremo
// viejo un punto de union queda huerfano, se captura para restaurarlo en
// undo() (mismo criterio que DeleteWireCommand).
class RetargetWireEndpointCommand : public QUndoCommand {
public:
    RetargetWireEndpointCommand(CircuitDocument* document, uint32_t wireId, bool endIsA, WireEndpoint newEndpoint,
                                QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

private:
    CircuitDocument* document_;
    uint32_t wireId_;
    bool endIsA_;
    WireEndpoint oldEndpoint_;
    WireEndpoint newEndpoint_;
    std::optional<Junction> orphanedJunction_;
};

class ChangePropertyCommand : public QUndoCommand {
public:
    ChangePropertyCommand(CircuitDocument* document, uint32_t componentId, std::string propertyId,
                           components::PropertyValue oldValue, components::PropertyValue newValue,
                           QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

private:
    CircuitDocument* document_;
    uint32_t componentId_;
    std::string propertyId_;
    components::PropertyValue oldValue_;
    components::PropertyValue newValue_;
    // Cables eliminados en cascada por la reduccion del numero de pines en redo(); restaurados en undo().
    std::vector<WireConnection> removedWires_;
    // Puntos de union que quedaron huerfanos por la misma cascada; se
    // restauran en undo() antes que removedWires_.
    std::vector<Junction> orphanedJunctions_;
};

} // namespace digitalforge::editor
