#pragma once

#include <QMetaObject>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace digitalforge::editor {
class CircuitDocument;
}

namespace digitalforge::ui {

// Panel de solo lectura que barre exhaustivamente todas las combinaciones de
// las wiring.input del documento activo y arma la tabla de verdad
// resultante (columnas = entradas + wiring.output, filas = cada
// combinacion). No se recalcula sola: el usuario debe presionar "Generar"
// explicitamente (un barrido de 2^N reconstruye todo el estado de
// simulacion mientras corre, no es algo para hacer en cada tecla). Cualquier
// edicion estructural del documento despues de generar invalida la tabla
// (se muestra un aviso en vez de datos potencialmente desactualizados).
class TruthTablePanel : public QWidget {
    Q_OBJECT

public:
    explicit TruthTablePanel(editor::CircuitDocument* document, QWidget* parent = nullptr);

    // Igual patron que PropertyInspector::setDocument() - usado por
    // MainWindow al cambiar de documento activo.
    void setDocument(editor::CircuitDocument* document);

private slots:
    void onGenerateClicked();
    void onExportClicked();
    void invalidate();

private:
    void generate();
    // Serializan el contenido actual de `table_` (encabezados + celdas) en
    // cada formato - onExportClicked() elige cual segun la extension que el
    // usuario haya puesto/elegido en el dialogo de guardado.
    [[nodiscard]] QString toCsv() const;
    [[nodiscard]] QString toPlainText() const;
    [[nodiscard]] QString toMarkdown() const;

    editor::CircuitDocument* document_;
    QLabel* statusLabel_;
    // Una linea por salida ("Y = A·B + A'·B'", ver editor::TruthTableFormula)
    // debajo de la tabla -- vacio/oculto hasta que generate() la puebla.
    QLabel* formulaLabel_;
    QPushButton* generateButton_;
    // Habilitado unicamente cuando table_ tiene una tabla generada (ver
    // generate()/invalidate()) - exportar sin datos no tiene sentido.
    QPushButton* exportButton_;
    QTableWidget* table_;

    // Ver el comentario sobre QMetaObject::Connection::disconnect() en
    // PropertyInspector.hpp: seguro incluso si el documento ya se destruyo.
    QMetaObject::Connection componentAddedConnection_;
    QMetaObject::Connection componentAboutToBeRemovedConnection_;
    QMetaObject::Connection propertyChangedConnection_;
};

} // namespace digitalforge::ui
