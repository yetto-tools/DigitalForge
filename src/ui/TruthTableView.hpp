#pragma once

#include <QWidget>

#include <cstdint>
#include <vector>

class QComboBox;
class QPushButton;
class QTableWidget;

namespace digitalforge::editor {
class Project;
class TruthTableDocument;
} // namespace digitalforge::editor

namespace digitalforge::ui {

// Pestana dedicada para un editor::TruthTableDocument: grilla tipo planilla
// con una fila por combinacion de entradas (en orden binario normal,
// 0..2^N-1 -- a diferencia del Karnaugh-Gray de ui::KarnaughMapView, aca no
// hace falta que las combinaciones adyacentes en la grilla difieran en un
// solo bit) y una columna por variable de entrada (solo lectura, el bit
// correspondiente de ese minterm) mas una columna por salida (cada celda
// cicla 0/1/X con un clic, igual gesto que ui::KarnaughGridCanvas).
// Doble clic sobre un encabezado lo renombra.
//
// El flujo tiene tres pasos, cada uno opcional respecto del anterior (se
// puede ir directo al ultimo): completar la tabla a mano, "Generar mapas"
// (un editor::KarnaughDocument nuevo por columna de salida, con las mismas
// variables y celdas, para inspeccionar/animar el procedimiento de
// Quine-McCluskey de cada una por separado en ui::KarnaughMapView -- lo
// mismo que ya ofrece esa vista para una sola salida) y "Generar circuito"
// (arma un editor::KarnaughResult por columna via editor::minimize() y los
// combina en un unico CircuitDocument con formats::synthesizeMultiOutputToCircuit()
// -- las entradas quedan compartidas entre todas las salidas, en vez de
// tener que sintetizar cada una por separado y unirlas a mano despues, como
// se armaba hasta ahora un decodificador BCD a 7 segmentos por ejemplo).
class TruthTableView : public QWidget {
    Q_OBJECT

public:
    TruthTableView(editor::Project* project, uint32_t documentId, editor::TruthTableDocument* document,
                   QWidget* parent = nullptr);

public slots:
    // Publicos para que MainWindow los pueda disparar desde el menu
    // "Karnaugh" sin pasar por el boton propio de la vista.
    void onGenerateMapsClicked();
    void onGenerateCircuitClicked();

private slots:
    void onCellClicked(int row, int column);
    void onHeaderDoubleClicked(int section);
    void onVariableCountChanged(int index);
    void onAddOutputClicked();
    void onRemoveOutputClicked();
    void onCellValueChanged(int outputIndex, int minterm);
    void onStructureChanged(); // variables u outputs: hace falta reconstruir toda la tabla

private:
    void rebuildTable();
    // Redibuja las celdas (entradas + salidas) de una sola fila visual a
    // partir de rowToMinterm_[row] -- el cuerpo interno del bucle de
    // rebuildTable(), separado para reusarlo al intercambiar dos filas en
    // onCellClicked() sin tener que reconstruir la tabla entera.
    void populateRow(int row);
    [[nodiscard]] std::vector<QString> currentVariableNames() const;

    editor::Project* project_;
    uint32_t documentId_;
    editor::TruthTableDocument* document_;
    // rowToMinterm_[fila visual] = minterm real en document_ -- una
    // permutacion de 0..2^variableCount-1, puramente de la vista (el
    // documento no tiene ni necesita nocion de "orden de fila"). Empieza en
    // el orden ascendente natural (ver reverseBits() en el .cpp) y solo se
    // reordena cuando el usuario hace clic en una celda de entrada (ver
    // onCellClicked()) -- no se persiste entre sesiones.
    std::vector<int> rowToMinterm_;

    QComboBox* variableCountCombo_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* addOutputButton_ = nullptr;
    QPushButton* removeOutputButton_ = nullptr;
    QPushButton* generateMapsButton_ = nullptr;
    QPushButton* generateCircuitButton_ = nullptr;
};

} // namespace digitalforge::ui
