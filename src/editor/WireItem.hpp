#pragma once

#include <QGraphicsPathItem>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "CircuitDocument.hpp"
#include "WireRouting.hpp"

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

// Un unico cable ortogonal de 1 bit entre dos extremos (pin o punto de union).
// Es puramente presentacional: su forma es una funcion pura de las posiciones
// de escena de sus extremos mas los quiebres guardados en
// CircuitDocument::WireConnection::waypoints (ver WireRouting.hpp), y su
// color refleja el valor de la red consultado a CircuitDocument en el momento
// de dibujar - nunca calcula la logica por si mismo.
//
// Maneja sus propios eventos de mouse (no tiene ItemIsMovable) con los gestos
// de Proteus:
//   - arrastrar el CUERPO desplaza ese segmento perpendicular a si mismo,
//     conservando los angulos rectos con sus vecinos;
//   - arrastrar una ESQUINA la lleva a donde este el cursor;
//   - arrastrar el handle de un EXTREMO (con el cable seleccionado) lo
//     reconecta a otro pin/union.
// Ninguno de los tres crea conexiones electricas por proximidad: conectar es
// siempre terminar un cable explicitamente sobre un pin, una union u otro
// cable (ver WireTool).
class WireItem : public QGraphicsPathItem {
public:
    WireItem(CircuitDocument* document, QUndoStack* undoStack, uint32_t wireId, WireEndpoint a, WireEndpoint b,
             WireAnchor anchorA, WireAnchor anchorB);
    ~WireItem() override;

    [[nodiscard]] uint32_t wireId() const noexcept { return wireId_; }
    [[nodiscard]] const WireAnchor& anchorA() const noexcept { return anchorA_; }
    [[nodiscard]] const WireAnchor& anchorB() const noexcept { return anchorB_; }

    // Desplazamiento transitorio (no persistido) que se suma a los waypoints
    // guardados mientras dura un arrastre de seleccion multiple -- ver
    // SelectionTool::afterMove(). std::nullopt = sin desplazamiento (uso
    // normal). El propio SelectionTool es quien comete el resultado final a
    // CircuitDocument via SetWireWaypointsCommand al soltar, y recien ahi
    // limpia el offset.
    void setLiveWaypointOffset(std::optional<QPointF> offset);

    // Marca/desmarca este cable como parte del nodo electrico resaltado --
    // ver CircuitScene::updateNetHighlight(). Puramente visual (un halo
    // adicional en paint(), ver ahi); no cambia nada del modelo.
    void setHighlighted(bool highlighted);
    [[nodiscard]] bool isHighlighted() const noexcept { return highlighted_; }

    // Recalcula el trazado a partir de las posiciones de escena actuales de
    // los extremos (y, si hay un arrastre en curso, de la polilinea en
    // edicion). Se llama cada vez que alguno de los ComponentItem/JunctionItem
    // de los extremos se mueve, o que cambian los waypoints en el documento.
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

    // Waypoints que deberian quedarle a cada mitad si este cable se parte en
    // `point` (tipicamente el resultado de nearestPointOnPath()). Usado por
    // WireTool/SplitWireCommand para que derivar un cable nuevo sobre el
    // cuerpo de este no le borre el trazado ya acomodado.
    [[nodiscard]] WireSplit splitWaypointsAt(QPointF point) const;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    [[nodiscard]] QPointF endpointScenePos(const WireEndpoint& endpoint, const WireAnchor& anchor) const;
    // Waypoints tal como estan guardados hoy en el documento.
    [[nodiscard]] std::vector<QPointF> storedWaypoints() const;
    // La polilinea VISIBLE completa [extremo A, esquinas..., extremo B],
    // incluyendo los codos que WireRouting genera y que todavia no son
    // waypoints guardados. Es sobre esta lista que se agarran los segmentos y
    // las esquinas, de modo que se pueda tomar cualquier tramo que se vea, no
    // solo los que el usuario ya fijo a mano.
    [[nodiscard]] std::vector<QPointF> renderedPolyline() const;
    // Indice del segmento de `polyline` mas cercano a `point` (el segmento i
    // va de polyline[i] a polyline[i+1]).
    [[nodiscard]] static std::size_t nearestSegmentIndex(const std::vector<QPointF>& polyline, QPointF point);
    // Posicion del handle de un extremo (a_ si isA, si no b_): sobre la ruta
    // pero corrido hacia adentro respecto del pin/union, para que se pueda
    // agarrar sin chocar con el pin (que inicia un cable nuevo).
    [[nodiscard]] QPointF endpointHandlePos(bool isA) const;
    // Guarda la polilinea en edicion como waypoints del documento (descarta
    // los dos extremos, que los ancla su pin/union). No hace nada si no
    // cambio nada respecto de lo guardado.
    void commitDragPolyline();

    CircuitDocument* document_;
    QUndoStack* undoStack_;
    uint32_t wireId_;
    WireEndpoint a_;
    WireEndpoint b_;
    WireAnchor anchorA_;
    WireAnchor anchorB_;

    // Que se esta remodelando, si es que hay algo en curso.
    enum class DragKind {
        None,
        Segment, // arrastre del cuerpo: desplaza un tramo entero
        Corner,  // arrastre de una esquina
    };
    DragKind dragKind_ = DragKind::None;
    std::size_t dragIndex_ = 0;
    // Polilinea completa (con extremos) al empezar el gesto. Cada movimiento
    // del mouse se recalcula SIEMPRE desde esta base y no desde el resultado
    // anterior: moveWireSegment() puede insertar vertices de absorcion en los
    // extremos, y aplicarlo de forma acumulativa los iria apilando.
    std::vector<QPointF> dragBasePolyline_;
    // Resultado vigente del gesto (lo que se dibuja mientras dura).
    std::vector<QPointF> dragPolyline_;

    bool hovered_ = false;
    bool highlighted_ = false;

    // Estado de un arrastre de extremo en curso (reconexion): que extremo se
    // esta moviendo y a que punto de escena sigue mientras dura el gesto.
    bool endpointDragging_ = false;
    bool endpointDragIsA_ = false;
    QPointF endpointDragPos_;

    // Ver setLiveWaypointOffset().
    std::optional<QPointF> liveWaypointOffset_;
};

} // namespace digitalforge::editor
