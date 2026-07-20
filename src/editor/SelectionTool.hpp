#pragma once

#include <QPointF>

#include <cstdint>
#include <map>

class QGraphicsSceneMouseEvent;
class QUndoStack;

namespace digitalforge::editor {

class CircuitScene;
class CircuitDocument;

// El modo Selection se apoya en la seleccion por "rubber-band" integrada de
// QGraphicsScene y en el arrastre por item (via ItemIsMovable) para la
// mecanica; esta clase solo agrega lo que Qt no provee gratuitamente:
// convertir un gesto de arrastre completado en un unico MoveComponentCommand
// deshacible por cada componente movido.
class SelectionTool {
public:
    SelectionTool(CircuitScene* scene, CircuitDocument* document, QUndoStack* undoStack);

    // Llamar despues de que QGraphicsScene::mousePressEvent haya actualizado
    // la seleccion.
    void afterPress(QGraphicsSceneMouseEvent* event);
    // Llamar despues de que QGraphicsScene::mouseReleaseEvent haya
    // finalizado el arrastre.
    void afterRelease(QGraphicsSceneMouseEvent* event);

private:
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
};

} // namespace digitalforge::editor
