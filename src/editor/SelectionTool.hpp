#pragma once

#include <QPointF>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

class QGraphicsSceneMouseEvent;
class QUndoStack;

namespace digitalforge::editor {

class CircuitScene;
class CircuitDocument;
class WireItem;

// El modo Selection se apoya en la seleccion por "rubber-band" integrada de
// QGraphicsScene y en el arrastre por item (via ItemIsMovable) para la
// mecanica; esta clase agrega lo que Qt no provee gratuitamente: convertir un
// gesto de arrastre completado en un unico MoveComponentCommand deshacible
// por cada componente movido, y arrastrar junto con la seleccion los
// waypoints de cualquier cable con quiebres guardados que cuelgue de algo
// seleccionado (sin esto el cable queda "anclado" a su forma vieja mientras
// sus extremos se mueven - ver WireItem::setLiveWaypointOffset()).
class SelectionTool {
public:
    SelectionTool(CircuitScene* scene, CircuitDocument* document, QUndoStack* undoStack);

    // Llamar despues de que QGraphicsScene::mousePressEvent haya actualizado
    // la seleccion.
    void afterPress(QGraphicsSceneMouseEvent* event);
    // Llamar despues de cada QGraphicsScene::mouseMoveEvent mientras dura un
    // arrastre: adelanta visualmente los waypoints de los cables afectados
    // (offset transitorio, nada se persiste todavia).
    void afterMove(QGraphicsSceneMouseEvent* event);
    // Llamar despues de que QGraphicsScene::mouseReleaseEvent haya
    // finalizado el arrastre.
    void afterRelease(QGraphicsSceneMouseEvent* event);

private:
    // Delta actual (posicion en vivo menos la de afterPress) del extremo de
    // `wire` que pertenece a la seleccion en arrastre, si alguno. Si ambos
    // extremos pertenecen a la seleccion se usa el de anchorA (en un
    // arrastre de grupo comparten el mismo delta). std::nullopt si ninguno
    // de los dos extremos de `wire` esta entre lo que se esta arrastrando.
    [[nodiscard]] std::optional<QPointF> deltaForWire(const WireItem* wire) const;

    CircuitScene* scene_;
    CircuitDocument* document_;
    QUndoStack* undoStack_;
    std::map<uint32_t, QPointF> dragStartPositions_;
    // Mismo patron que dragStartPositions_, para los JunctionItem
    // seleccionados (ver su comentario de clase: ahora tambien son
    // arrastrables). Un mapa separado en vez de reusar dragStartPositions_
    // porque componentId y junctionId son espacios de id independientes que
    // podrian coincidir por casualidad.
    std::map<uint32_t, QPointF> junctionDragStartPositions_;
    // Waypoints originales (al momento de afterPress) de cada cable con al
    // menos un extremo entre lo seleccionado -- solo cables que ya tenian
    // waypoints guardados (los demas se auto-rutean solos, sin nada que
    // arrastrar). Fuente de verdad para el offset en vivo y para el
    // SetWireWaypointsCommand final en afterRelease.
    std::map<uint32_t, std::vector<QPointF>> wireStartWaypoints_;
    // Cables con waypoints guardados pero con UN SOLO extremo entre lo
    // seleccionado (el otro se queda quieto) -- a esos no se les arrastra el
    // trazado en bloque (ver el comentario de collectWire() en el .cpp): se
    // resetean a auto-ruteo en afterRelease() si el gesto realmente los movio,
    // en vez de deformarse porque el trazado viejo quedo pensado para una
    // geometria que ya no existe.
    std::vector<uint32_t> resetWireIds_;
    // Posicion de escena del press que inicio el gesto -- ancla fija para
    // calcular el delta total del arrastre de grupo (ver afterMove()).
    QPointF pressScenePos_;
};

} // namespace digitalforge::editor
