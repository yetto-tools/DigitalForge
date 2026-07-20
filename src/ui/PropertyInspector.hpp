#pragma once

#include <QMetaObject>
#include <QWidget>

#include <cstdint>
#include <vector>

#include "components/Property.hpp"

class QFormLayout;
class QLabel;
class QUndoStack;

namespace digitalforge::editor {
class CircuitDocument;
}

namespace digitalforge::ui {

// Formulario del panel derecho generado a partir de las
// ComponentDefinition::properties del/de los componente(s) seleccionado(s).
// Las ediciones se validan y aplican mediante ChangePropertyCommand, nunca
// mutando directamente el ComponentInstance, de modo que toda edicion se
// puede deshacer y se respeta de extremo a extremo la tuberia de la
// especificacion (validar -> registrar deshacer -> actualizar modelo ->
// recalcular pines -> actualizar escena). Con multiples componentes
// seleccionados, solo se muestran las propiedades comunes a todos (mismo id
// y tipo) y cada edicion se aplica a todos a la vez mediante un macro de
// deshacer compuesto (ver pushPropertyChange).
class PropertyInspector : public QWidget {
    Q_OBJECT

public:
    PropertyInspector(editor::CircuitDocument* document, QUndoStack* undoStack, QWidget* parent = nullptr);

    // Rebinds this inspector to a different document/undo stack - usado por
    // MainWindow al cambiar de documento activo dentro de un mismo Project.
    // Descarta la seleccion actual (pertenece al documento anterior).
    void setDocument(editor::CircuitDocument* document);
    void setUndoStack(QUndoStack* undoStack) { undoStack_ = undoStack; }

public slots:
    void setSelectedComponents(std::vector<uint32_t> componentIds);

private slots:
    void onPropertyChanged(uint32_t componentId);
    void onComponentAboutToBeRemoved(uint32_t componentId);

private:
    void rebuildForm();
    // Aplica `newValue` a `propertyId` en todos los componentes
    // seleccionados actualmente vivos, agrupando en un unico macro de
    // deshacer cuando hay mas de uno (mismo patron que
    // CircuitScene::deleteSelected()/rotateSelected()).
    void pushPropertyChange(const std::string& propertyId, const components::PropertyValue& newValue);

    editor::CircuitDocument* document_;
    QUndoStack* undoStack_;
    std::vector<uint32_t> componentIds_;
    // QMetaObject::Connection::disconnect() (via QObject::disconnect) es
    // seguro de llamar incluso si el documento del que colgaban ya se
    // destruyo (a diferencia de disconnect(document_, ...), que en ese caso
    // es un use-after-free sobre un puntero colgante - el crash/freeze
    // reportado al recargar/reabrir un proyecto).
    QMetaObject::Connection propertyChangedConnection_;
    QMetaObject::Connection componentAboutToBeRemovedConnection_;

    QLabel* titleLabel_;
    QWidget* formContainer_ = nullptr;
};

} // namespace digitalforge::ui
