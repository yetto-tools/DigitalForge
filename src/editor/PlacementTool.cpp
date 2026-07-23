#include "PlacementTool.hpp"

#include <QGraphicsSceneMouseEvent>
#include <QString>
#include <QUndoStack>
#include <cmath>

#include "CircuitScene.hpp"
#include "ComponentItem.hpp"
#include "UndoCommands.hpp"

namespace digitalforge::editor {

PlacementTool::PlacementTool(CircuitScene* scene, CircuitDocument* document, QUndoStack* undoStack)
    : scene_(scene), document_(document), undoStack_(undoStack) {}

void PlacementTool::setPendingType(std::string typeId) { pendingTypeId_ = std::move(typeId); }

void PlacementTool::press(QGraphicsSceneMouseEvent* event) {
    if (pendingTypeId_.empty()) {
        // Sin tipo armado no hay nada que colocar, pero quedarse en modo
        // Placement dejaba la escena atascada: ningun clic posterior podia
        // seleccionar ni arrastrar nada.
        scene_->setMode(EditorMode::Selection);
        return;
    }
    if (!document_->requireEditable(QStringLiteral("colocar un componente"))) {
        pendingTypeId_.clear();
        scene_->setMode(EditorMode::Selection);
        return;
    }
    const qreal grid = ComponentItem::kGridSize;
    const QPointF pos = event->scenePos();
    const ComponentPlacement placement{QPointF(std::round(pos.x() / grid) * grid, std::round(pos.y() / grid) * grid),
                                        0};

    auto* command = new PlaceComponentCommand(document_, pendingTypeId_, {}, placement);
    undoStack_->push(command);

    // Volver a modo Selection ANTES de seleccionar: setMode() hace
    // clearSelection(), asi que en el orden inverso el componente recien
    // colocado terminaba deseleccionado.
    pendingTypeId_.clear();
    scene_->setMode(EditorMode::Selection);
    scene_->selectComponent(command->componentId());
}

} // namespace digitalforge::editor
