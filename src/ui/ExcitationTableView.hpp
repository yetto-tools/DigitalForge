#pragma once

#include <QWidget>

#include <cstdint>
#include <vector>

class QComboBox;
class QHBoxLayout;
class QPushButton;
class QTableWidget;

namespace digitalforge::editor {
class Project;
class ExcitationTableDocument;
} // namespace digitalforge::editor

namespace digitalforge::ui {

// Pestana dedicada para un editor::ExcitationTableDocument: misma grilla tipo
// planilla que ui::TruthTableView (fila por combinacion de ESTADO ACTUAL, en
// orden binario natural), pero sin su gadget de reordenar filas -- el bit i
// del minterm de cada fila arranca en su valor natural, pero a diferencia de
// TruthTableView el usuario puede editarlo directo (cicla 0/1/X con un clic,
// igual que estado siguiente) para marcar una fila entera como inalcanzable
// sin tener que tocar cada bit de estado siguiente por separado. Una fila de
// combos aparte deja elegir el tipo de flip-flop (SR/JK/T/D) de cada bit.
//
// "Generar mapas de excitacion"/"Generar circuito" derivan, con
// editor::computeExcitation(), las columnas de excitacion reales (J/K, S/R, T
// o D segun el tipo elegido por bit) a partir del estado siguiente ya
// cargado, y las alimentan al mismo editor::KarnaughDocument/
// formats::synthesizeMultiOutputToCircuit() que ya usan
// ui::TruthTableView/ui::KarnaughMapView -- ninguno de los dos coloca ni
// cablea flip-flops, solo la logica de excitacion ya minimizada.
// "Generar circuito con flip-flops" es la version completa: coloca un
// flip-flop de verdad por bit (formats::synthesizeSequentialCircuit()) con
// esa misma logica ya cableada a su entrada y un reloj compartido, listo
// para simular sin tener que cablear nada a mano -- salvo que algun bit use
// SR, tipo sin componente con reloj en la biblioteca (ver el mensaje de
// error si eso pasa).
class ExcitationTableView : public QWidget {
    Q_OBJECT

public:
    ExcitationTableView(editor::Project* project, uint32_t documentId, editor::ExcitationTableDocument* document,
                         QWidget* parent = nullptr);

public slots:
    // Publicos para que MainWindow los pueda disparar desde el menu
    // "Karnaugh" sin pasar por el boton propio de la vista.
    void onGenerateMapsClicked();
    void onGenerateCircuitClicked();
    void onGenerateCircuitWithFlipFlopsClicked();

private slots:
    void onCellClicked(int row, int column);
    void onHeaderDoubleClicked(int section);
    void onStateBitCountChanged(int index);
    void onPresentStateChanged(int bitIndex, int minterm);
    void onNextStateChanged(int bitIndex, int minterm);
    // stateBitCount, un nombre de bit, o un tipo de flip-flop: cualquiera
    // obliga a reconstruir la tabla y/o la fila de combos de tipo.
    void onStructureChanged();

private:
    void rebuildTable();
    // Destruye y reconstruye flipFlopTypeCombos_ (uno por bit, con su
    // etiqueta) -- separado de rebuildTable() porque tambien hace falta
    // llamarlo cuando cambia un NOMBRE de bit (la etiqueta lo muestra), no
    // solo cuando cambia la cantidad.
    void rebuildFlipFlopTypeCombos();
    void populateRow(int row);
    [[nodiscard]] std::vector<QString> currentStateBitNames() const;

    editor::Project* project_;
    uint32_t documentId_;
    editor::ExcitationTableDocument* document_;

    QComboBox* stateBitCountCombo_ = nullptr;
    QHBoxLayout* flipFlopTypeLayout_ = nullptr;
    std::vector<QComboBox*> flipFlopTypeCombos_;
    QTableWidget* table_ = nullptr;
    QPushButton* generateMapsButton_ = nullptr;
    QPushButton* generateCircuitButton_ = nullptr;
    QPushButton* generateCircuitWithFlipFlopsButton_ = nullptr;
};

} // namespace digitalforge::ui
