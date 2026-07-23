#pragma once

#include <QGraphicsPathItem>

#include <cstdint>
#include <optional>
#include <vector>

#include "CircuitDocument.hpp"

class QUndoStack;

namespace digitalforge::editor {

class CircuitDocument;
class ComponentItem;
class JunctionItem;

// Un extremo grafico de WireItem: a lo sumo uno de los dos punteros es no
// nulo, segun si el WireEndpoint correspondiente es un pin (ComponentItem)
// o un punto de union (JunctionItem).
struct WireAnchor {
    ComponentItem* component = nullptr;
    JunctionItem* junction = nullptr;
};

// Un unico cable ortogonal de 1 bit entre dos extremos (pin o punto de
// union). Es puramente presentacional: su trazado auto-enruta con quiebres
// en angulo recto entre las posiciones de escena actuales de sus extremos,
// atravesando ademas cualquier waypoint que el usuario haya definido a mano
// (ver CircuitDocument::WireConnection::waypoints); su color refleja el
// valor actual de la red consultado a CircuitDocument en el momento de
// dibujar - nunca calcula la logica por si mismo.
//
// Tambien maneja sus propios eventos de mouse (no tiene ItemIsMovable) para
// permitir arrastrar/agregar/quitar los puntos de quiebre de su trazado; a
// diferencia de mover un componente (donde SelectionTool arma el comando de
// undo comparando posiciones antes/despues del gesto), aca el propio
// WireItem arma su SetWireWaypointsCommand al soltar, porque es el unico
// que conoce el estado intermedio del arrastre.
class WireItem : public QGraphicsPathItem {
public:
    WireItem(CircuitDocument* document, QUndoStack* undoStack, uint32_t wireId, WireEndpoint a, WireEndpoint b,
             WireAnchor anchorA, WireAnchor anchorB);
    ~WireItem() override;

    [[nodiscard]] uint32_t wireId() const noexcept { return wireId_; }

    // Recalcula el trazado a partir de las posiciones de escena actuales de
    // los extremos (y, si hay un arrastre de vertice en curso, del punto que
    // se esta arrastrando). Se llama cada vez que alguno de los
    // ComponentItem/JunctionItem de los extremos se mueve, o que cambian los
    // waypoints almacenados en el documento.
    void updateGeometry();

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // Region clickeable: el trazado engrosado. El ancho se compensa con el
    // zoom para que a cualquier escala haya una banda comoda para hacer clic
    // (a mucho zoom un ancho fijo se ve finito; a poco zoom, exagerado).
    [[nodiscard]] QPainterPath shape() const override;

    // Punto sobre el trazado actual del cable mas cercano a `scenePos`
    // (proyectado sobre el segmento correspondiente, nunca "al costado" del
    // cable). Usado por WireTool al dividir este cable con SplitWireCommand,
    // para que el punto de union nuevo caiga siempre exactamente sobre la
    // ruta visible del cable, nunca desalineado de ella.
    [[nodiscard]] QPointF nearestPointOnPath(QPointF scenePos) const;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    [[nodiscard]] QPointF endpointScenePos(const WireEndpoint& endpoint, const WireAnchor& anchor) const;
    // Waypoints tal como estan guardados hoy en el documento (vacio si el
    // cable todavia depende por completo del auto-ruteo).
    [[nodiscard]] std::vector<QPointF> storedWaypoints() const;
    // Bounding rects de todos los demas ComponentItem (nunca los dos propios
    // extremos de este cable, si son pines) - ver CircuitScene::
    // componentObstacleRects(). Vacio si scene() todavia no es una
    // CircuitScene (p. ej. el frame de construccion, antes de addItem()).
    [[nodiscard]] std::vector<QRectF> obstacleRects() const;
    // Indice (en la lista de waypoints) del segmento mas cercano a `point`,
    // considerando la polilinea completa extremo-waypoints-extremo. Usado
    // tanto para insertar un vertice nuevo al arrastrar el cuerpo del cable
    // como al hacer doble clic sobre el.
    [[nodiscard]] std::size_t nearestSegmentInsertIndex(QPointF point, const std::vector<QPointF>& waypoints) const;
    // Llamado al soltar un arrastre de vertice (ver mouseReleaseEvent()): si
    // `dropPoint` cae sobre el cuerpo de otro WireItem o sobre un
    // JunctionItem existente (ninguno de los dos extremos propios de este
    // cable), reemplaza este cable por dos tramos nuevos que se encuentran
    // en un punto de union real ahi -- el mecanismo detras de "llevar una
    // linea a un nodo" estilo Logisim. Devuelve false (sin efecto) si no hay
    // nada que empalmar en ese punto, en cuyo caso el llamador debe seguir
    // con el simple SetWireWaypointsCommand cosmetico de siempre.
    bool trySpliceAt(int waypointIndex, QPointF dropPoint);
    // Posicion del handle de un extremo (a_ si isA, si no b_): sobre la ruta
    // pero corrido hacia adentro respecto del pin/union, para que se pueda
    // agarrar sin chocar con el pin (que en modo Selection inicia un cable
    // nuevo). Ver paint()/mousePressEvent().
    [[nodiscard]] QPointF endpointHandlePos(bool isA) const;

    CircuitDocument* document_;
    QUndoStack* undoStack_;
    uint32_t wireId_;
    WireEndpoint a_;
    WireEndpoint b_;
    WireAnchor anchorA_;
    WireAnchor anchorB_;

    // Estado de un arrastre de vertice en curso (ya sea de un waypoint
    // existente, o de uno recien insertado al empezar a arrastrar el cuerpo
    // del cable).
    bool dragging_ = false;
    bool hovered_ = false;
    int dragIndex_ = -1;
    std::vector<QPointF> dragWaypoints_;

    // Estado de un arrastre de extremo en curso (reconexion): que extremo se
    // esta moviendo y a que punto de escena sigue mientras dura el gesto.
    bool endpointDragging_ = false;
    bool endpointDragIsA_ = false;
    QPointF endpointDragPos_;
    // Posicion del press cuando cayo sobre el cuerpo del cable (lejos de
    // cualquier vertice existente): la insercion de un vertice nuevo se
    // posterga hasta que el mouse efectivamente se mueva mas alla de un
    // umbral, para que un clic simple (sin arrastre) siga sirviendo solo
    // para seleccionar el cable sin alterar su trazado.
    std::optional<QPointF> pendingInsertAt_;
};

} // namespace digitalforge::editor
