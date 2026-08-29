#pragma once

#include <QGraphicsScene>
#include <QUndoStack>

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "CircuitDocument.hpp"

class QKeyEvent;
class QGraphicsSceneContextMenuEvent;

namespace digitalforge::editor {

class ComponentItem;
class WireItem;
class JunctionItem;
class PinItem;
class SelectionTool;
class WireTool;
class PlacementTool;

enum class EditorMode { Selection, Wiring, Placement };

// Es propietaria de los elementos graficos y reacciona a las senales de
// CircuitDocument para mantenerlos sincronizados; nunca calcula por si misma
// la logica de simulacion. Distribuye los eventos de raton a la herramienta
// que corresponda segun el EditorMode actual.
class CircuitScene : public QGraphicsScene {
    Q_OBJECT

public:
    CircuitScene(CircuitDocument* document, QUndoStack* undoStack, QObject* parent = nullptr);
    // Declarado (no default) aqui y definido en el .cpp: el destructor
    // generado implicitamente necesitaria que SelectionTool/WireTool/
    // PlacementTool fueran tipos completos en este punto (para destruir los
    // miembros unique_ptr), pero en este header solo estan declarados por
    // adelantado (forward-declared).
    ~CircuitScene() override;

    [[nodiscard]] CircuitDocument* document() const noexcept { return document_; }
    [[nodiscard]] QUndoStack* undoStack() const noexcept { return undoStack_; }

    void setMode(EditorMode mode);
    [[nodiscard]] EditorMode mode() const noexcept { return mode_; }
    void beginPlacement(const std::string& typeId);
    // Descarta una colocacion pendiente y vuelve a modo Selection. La llama
    // CircuitView::dropEvent() -el arrastre desde la paleta ya materializo la
    // intencion del usuario- y la tecla Escape.
    void cancelPlacement();

    [[nodiscard]] ComponentItem* componentItem(uint32_t componentId) const;
    // Componente cuya etiqueta de instancia (arrastrable, ver
    // ComponentItem::labelHitTest()) cae bajo scenePos, si lo hay -- una
    // busqueda manual aparte porque QGraphicsScene::items()/itemAt() usan
    // shape() para el hit-testing, y ComponentItem::shape() excluye a
    // proposito esa franja (ver su comentario).
    [[nodiscard]] ComponentItem* componentWithLabelAt(QPointF scenePos) const;
    [[nodiscard]] JunctionItem* junctionItem(uint32_t junctionId) const;
    [[nodiscard]] WireItem* wireItem(uint32_t wireId) const;
    [[nodiscard]] PinItem* pinItemAt(QPointF scenePos) const;
    [[nodiscard]] JunctionItem* junctionItemAt(QPointF scenePos) const;
    // Cable bajo scenePos, si lo hay -- usado por WireTool para detectar una
    // "derivacion pendiente" al presionar/soltar sobre el cuerpo de un cable
    // ya trazado (en vez de sobre un pin o un punto de union existente).
    [[nodiscard]] WireItem* wireItemAt(QPointF scenePos) const;
    void selectComponent(uint32_t componentId);
    // Selecciona el ComponentItem (por su pin) o JunctionItem que corresponda
    // a cada WireEndpoint, reemplazando la seleccion actual -- usado por
    // MainWindow al activar una fila del panel de errores/advertencias
    // (ver ui::DiagnosticsPanel::diagnosticActivated()). Al quedar
    // seleccionado un unico cable/union, esto ya dispara el resaltado de
    // nodo completo (ver updateNetHighlight()) sin ningun paso extra.
    void selectEndpoints(const std::vector<WireEndpoint>& endpoints);

    [[nodiscard]] bool snapToGridEnabled() const noexcept { return snapToGrid_; }
    void setSnapToGridEnabled(bool enabled) { snapToGrid_ = enabled; }
    [[nodiscard]] bool gridVisible() const noexcept { return showGrid_; }
    void setGridVisible(bool visible) {
        showGrid_ = visible;
        update();
    }

    void drawBackground(QPainter* painter, const QRectF& rect) override;

    // Expuesto para el menu Edit de MainWindow (acciones Delete / Rotate),
    // que debe poder disparar el mismo comportamiento que la tecla Delete / 'R'.
    void deleteSelected();
    void rotateSelected();

    // Orden de apilado (ComponentPlacement::zOrder) de los ComponentItem
    // seleccionados, estilo draw.io - expuestos para el menu contextual de
    // MainWindow (ver onComponentContextMenuRequested()). Con varios
    // seleccionados, se preserva el orden relativo entre ellos: "al frente"/
    // "al fondo" los reordena como grupo por encima/debajo de todo lo demas
    // (no los apila todos al mismo z), "adelante"/"atras" simplemente
    // adelanta/atrasa cada uno un paso de forma independiente.
    void bringSelectedToFront();
    void sendSelectedToBack();
    void bringSelectedForward();
    void sendSelectedBackward();

    // Copiar/cortar/pegar de los ComponentItem seleccionados (y de los
    // cables entre dos componentes que esten ambos copiados -- un cable
    // hacia un componente o un punto de union que quedo afuera de la
    // seleccion no se copia). El portapapeles se comparte entre todas las
    // CircuitScene (ver CircuitScene.cpp), asi que copiar en un documento y
    // pegar en otro del mismo proyecto funciona igual que en cualquier otro
    // editor con portapapeles.
    void copySelected();
    void cutSelected();
    void pasteClipboard();

signals:
    // Emitida al hacer clic derecho sobre un ComponentItem (en cualquier
    // modo/estado). MainWindow la usa para ofrecer "Propiedades" en un menu
    // contextual - la via explicita para ver/editar propiedades mientras la
    // simulacion esta en ejecucion, ya que en ese estado la seleccion normal
    // ya no actualiza el inspector automaticamente (ver
    // MainWindow::onSceneSelectionChanged).
    void componentContextMenuRequested(uint32_t componentId, QPoint screenPos);
    // Emitida al hacer doble clic sobre un ComponentItem en modo Seleccion
    // (incluida la simulacion en vivo, salvo que el doble clic ya haya sido
    // consumido por tryToggleInput()). MainWindow la usa para abrir el
    // dialogo modal de Propiedades.
    void componentDoubleClicked(uint32_t componentId);
    // Emitida al hacer doble clic sobre un JunctionItem en modo Seleccion.
    // MainWindow la usa para mostrar informacion del punto de union
    // (posicion, valor logico, conexiones) -- mismo gesto que
    // componentDoubleClicked(), pero un punto de union no tiene propiedades
    // editables, asi que no abre el dialogo de Propiedades.
    void junctionDoubleClicked(uint32_t junctionId);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private slots:
    void onComponentAdded(uint32_t componentId);
    void onComponentAboutToBeRemoved(uint32_t componentId);
    void onWireAdded(uint32_t wireId);
    void onWireAboutToBeRemoved(uint32_t wireId);
    void onWireGeometryChanged(uint32_t wireId);
    void onJunctionAdded(uint32_t junctionId);
    void onJunctionAboutToBeRemoved(uint32_t junctionId);
    void onJunctionPositionChanged(uint32_t junctionId);
    void onPropertyChanged(uint32_t componentId);
    void onComponentPlacementChanged(uint32_t componentId);
    void onSimulationChanged();

private:
    void applyPlacement(ComponentItem* item, const ComponentPlacement& placement);
    // Mueve los ComponentItem seleccionados `delta` (en coordenadas de
    // escena, sin pasar por el snap-a-grilla de un arrastre con el mouse -
    // ver ComponentItem::itemChange) - usado por keyPressEvent() para las
    // flechas del teclado, pensadas para el ajuste fino que el arrastre no
    // permite.
    void moveSelectedBy(QPointF delta);
    // Si hay un wiring.input en scenePos, alterna su valor 0/1 y devuelve
    // true. Usado tanto por el doble clic (modo edicion) como por el clic
    // simple mientras la simulacion esta en ejecucion.
    bool tryToggleInput(QPointF scenePos);

    // Posicion de escena de un extremo de cable (pin de componente o punto de
    // union), resuelta desde los items graficos vivos -- usado solo para
    // reconstruir el trazado al fusionar dos cables (ver
    // mergedWaypointsAcrossJunction()).
    [[nodiscard]] QPointF endpointScenePosition(const WireEndpoint& endpoint) const;
    // El trazado que deberia tener el cable unico que resulta de fusionar
    // `w1`/`w2` en `junctionId` (que se asume en grado exactamente 2, sin
    // otra derivacion) -- concatena sus waypoints en el orden que sale del
    // punto de union, y simplifica el punto de union si quedo colineal entre
    // sus vecinos (el caso comun: la derivacion que motivo el split ya se
    // borro, asi que las dos mitades vuelven a ser un tramo recto).
    [[nodiscard]] std::vector<QPointF> mergedWaypointsAcrossJunction(uint32_t junctionId, const WireConnection& w1,
                                                                      const WireConnection& w2) const;
    // Resalta el nodo electrico completo cuando la seleccion actual es
    // exactamente UN WireItem o UN JunctionItem (via
    // CircuitDocument::endpointsOnSameNet()) -- conectado a
    // QGraphicsScene::selectionChanged en el constructor. Cualquier otra
    // seleccion (vacia, un componente, o 2+ items) limpia el resaltado
    // anterior.
    void updateNetHighlight();

    CircuitDocument* document_;
    QUndoStack* undoStack_;
    EditorMode mode_ = EditorMode::Selection;
    bool snapToGrid_ = true;
    bool showGrid_ = true;
    // Posicion (en coordenadas de escena) del ultimo press en modo Selection
    // que no inicio un cable; mouseReleaseEvent la compara contra la posicion
    // de release para distinguir un clic simple de un arrastre, de modo que
    // alternar un wiring.input con un solo clic (mientras la simulacion esta
    // en ejecucion) no interfiera con mover el componente.
    QPointF pressScenePos_;

    // Estado de un arrastre de etiqueta en curso -- ver mousePressEvent()/
    // mouseMoveEvent()/mouseReleaseEvent(). nullptr = ningun arrastre en
    // curso. labelDragStartLocalPos_ esta en el espacio local de
    // labelDragTarget_ (mapFromScene()), para que el gesto se sienta natural
    // sin importar la rotacion del componente.
    ComponentItem* labelDragTarget_ = nullptr;
    QPointF labelDragStartOffset_;
    QPointF labelDragStartLocalPos_;

    std::map<uint32_t, ComponentItem*> componentItems_;
    std::map<uint32_t, WireItem*> wireItems_;
    std::map<uint32_t, JunctionItem*> junctionItems_;
    // Items actualmente resaltados por updateNetHighlight() -- se limpian
    // ahi mismo antes de recalcular, para no depender de recorrer TODO
    // wireItems_/junctionItems_ en cada cambio de seleccion.
    std::vector<uint32_t> highlightedWireIds_;
    std::vector<uint32_t> highlightedJunctionIds_;

    std::unique_ptr<SelectionTool> selectionTool_;
    std::unique_ptr<WireTool> wireTool_;
    std::unique_ptr<PlacementTool> placementTool_;
};

} // namespace digitalforge::editor
