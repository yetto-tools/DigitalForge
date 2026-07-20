#pragma once

#include <string>

class QGraphicsSceneMouseEvent;
class QUndoStack;

namespace digitalforge::editor {

class CircuitScene;
class CircuitDocument;

// Colocacion de un solo uso: ComponentPalette llama a setPendingType()
// (normalmente tras un doble clic) para armar la herramienta, luego el
// siguiente clic en el lienzo coloca una instancia de ese tipo, ajustada a
// la grilla, y la escena vuelve al modo Selection.
class PlacementTool {
public:
    PlacementTool(CircuitScene* scene, CircuitDocument* document, QUndoStack* undoStack);

    void setPendingType(std::string typeId);
    [[nodiscard]] bool hasPendingType() const noexcept { return !pendingTypeId_.empty(); }

    void press(QGraphicsSceneMouseEvent* event);

private:
    CircuitScene* scene_;
    CircuitDocument* document_;
    QUndoStack* undoStack_;
    std::string pendingTypeId_;
};

} // namespace digitalforge::editor
