#pragma once

#include <QMetaObject>
#include <QWidget>

#include <vector>

#include "editor/CircuitDocument.hpp"

class QListWidget;
class QListWidgetItem;

namespace digitalforge::ui {

// Panel de solo lectura que lista los diagnosticos de
// editor::CircuitDocument::runDiagnostics() (pines obligatorios sin
// conectar, conflictos de manejadores, oscilacion detectada). Se recalcula
// solo, reconectando a las mismas senales que ya usa SimulationToolbar
// (simulationRebuilt/simulationStepped/liveSimulationChanged) - sin agregar
// ninguna senal nueva a CircuitDocument, y sin boton "Generar" (a diferencia
// de TruthTablePanel, correr runDiagnostics() es tan barato como recorrer
// componentIds() una vez, no un barrido 2^N).
//
// Activar una fila (doble clic / Enter) emite diagnosticActivated() con los
// WireEndpoint involucrados, para que quien instancie el panel (MainWindow)
// los seleccione en la escena activa - eso ya dispara el resaltado de nodo
// completo (ver CircuitScene::updateNetHighlight()) gratis, al quedar
// seleccionados.
class DiagnosticsPanel : public QWidget {
    Q_OBJECT

public:
    explicit DiagnosticsPanel(editor::CircuitDocument* document, QWidget* parent = nullptr);

    // Igual patron que TruthTablePanel::setDocument() - usado por
    // MainWindow al cambiar de documento activo.
    void setDocument(editor::CircuitDocument* document);

signals:
    void diagnosticActivated(std::vector<editor::WireEndpoint> endpoints);

private slots:
    void refresh();
    void onItemActivated(QListWidgetItem* item);

private:
    editor::CircuitDocument* document_;
    QListWidget* list_;
    // Extremos relacionados de cada fila de `list_`, en el mismo orden -
    // onItemActivated() los busca por indice de fila en vez de intentar
    // guardar un std::vector<WireEndpoint> dentro del QListWidgetItem.
    std::vector<std::vector<editor::WireEndpoint>> rowEndpoints_;

    // Ver el comentario sobre QMetaObject::Connection::disconnect() en
    // PropertyInspector.hpp: seguro incluso si el documento ya se destruyo.
    QMetaObject::Connection simulationRebuiltConnection_;
    QMetaObject::Connection simulationSteppedConnection_;
    QMetaObject::Connection liveSimulationChangedConnection_;
};

} // namespace digitalforge::ui
