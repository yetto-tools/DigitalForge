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
    SplitWireCommand(CircuitDocument* document, uint32_t existingWireId, QPointF splitPosition,
                      QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

    [[nodiscard]] uint32_t junctionId() const noexcept { return junctionId_; }

private:
    CircuitDocument* document_;
    uint32_t originalWireId_;
    WireConnection originalWire_;
    QPointF splitPosition_;
    uint32_t junctionId_;
    uint32_t wireId1_;
    uint32_t wireId2_;
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
