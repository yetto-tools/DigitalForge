#include "SelectionTool.hpp"

#include <QGraphicsSceneMouseEvent>
#include <QUndoStack>

#include "CircuitScene.hpp"
#include "ComponentItem.hpp"
#include "JunctionItem.hpp"
#include "UndoCommands.hpp"

namespace digitalforge::editor {

SelectionTool::SelectionTool(CircuitScene* scene, CircuitDocument* document, QUndoStack* undoStack)
    : scene_(scene), document_(document), undoStack_(undoStack) {}

void SelectionTool::afterPress(QGraphicsSceneMouseEvent*) {
    dragStartPositions_.clear();
    junctionDragStartPositions_.clear();
    for (QGraphicsItem* item : scene_->selectedItems()) {
        if (auto* component = dynamic_cast<ComponentItem*>(item)) {
            dragStartPositions_[component->componentId()] = document_->componentPlacement(component->componentId()).position;
        } else if (auto* junction = dynamic_cast<JunctionItem*>(item)) {
            junctionDragStartPositions_[junction->junctionId()] = document_->junctionPosition(junction->junctionId());
        }
    }
}

void SelectionTool::afterRelease(QGraphicsSceneMouseEvent*) {
    for (const auto& [componentId, startPos] : dragStartPositions_) {
        ComponentItem* item = scene_->componentItem(componentId);
        if (item == nullptr) {
            continue;
        }
        const QPointF currentPos = item->pos();
        if (currentPos != startPos) {
            undoStack_->push(new MoveComponentCommand(document_, componentId, startPos, currentPos));
        }
    }
    dragStartPositions_.clear();

    for (const auto& [junctionId, startPos] : junctionDragStartPositions_) {
        JunctionItem* item = scene_->junctionItem(junctionId);
        if (item == nullptr) {
            continue;
        }
        const QPointF currentPos = item->pos();
        if (currentPos != startPos) {
            undoStack_->push(new MoveJunctionCommand(document_, junctionId, startPos, currentPos));
        }
    }
    junctionDragStartPositions_.clear();
}

} // namespace digitalforge::editor
