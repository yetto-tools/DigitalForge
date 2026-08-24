#pragma once

#include <QRectF>
#include <QSize>
#include <QWidget>

#include <cstdint>
#include <vector>

#include "editor/KarnaughMap.hpp"

class QLabel;
class QPushButton;
class QComboBox;
class QLineEdit;
class QHBoxLayout;
class QVBoxLayout;
class QScrollArea;
class QMouseEvent;
class QPaintEvent;

namespace digitalforge::editor {
class Project;
class KarnaughDocument;
} // namespace digitalforge::editor

namespace digitalforge::ui {

// Grilla clickeable del mapa de Karnaugh (2 a 4 variables), en orden
// Gray-code (ver editor::gridDimensions()/mintermAt()) -- mismo patron que
// ui::WaveformCanvas (QWidget de dibujo puro, sin Q_OBJECT en ese caso;
// este si lo necesita para emitir cellClicked()). Un clic sobre una celda
// hace ciclar su valor 0 -> 1 -> X -> 0; setHighlightedMinterms() resalta
// el grupo del paso actual del procedimiento. Cada instancia muestra UNA
// funcion fija (outputIndex, fijado al crearla) -- ver
// ui::KarnaughMapView::FunctionPanel, que arma una de estas por funcion.
class KarnaughGridCanvas : public QWidget {
    Q_OBJECT

public:
    KarnaughGridCanvas(editor::KarnaughDocument* document, int outputIndex, QWidget* parent = nullptr);

    void setDocument(editor::KarnaughDocument* document);
    void setHighlightedMinterms(std::vector<int> minterms);

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void cellClicked(int minterm);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void rebuildGeometry();

    editor::KarnaughDocument* document_;
    int outputIndex_;
    std::vector<QRectF> cellRects_; // indexado por minterm
    std::vector<int> highlighted_;
};

// Pestana dedicada (un lienzo especializado, distinto del lienzo normal de
// edicion de circuitos) para un editor::KarnaughDocument de VARIAS
// funciones: entrada del mapa (cantidad/nombres de variables, compartidos)
// y, apiladas una debajo de la otra dentro de un area con scroll, UNA
// grilla independiente POR FUNCION -- cada una con su propio nombre
// editable, su propio procedimiento paso a paso de Quine-McCluskey (ver
// editor::minimize()) y su propia expresion minimizada -- para poder ver
// las N funciones juntas en la misma pestana, en vez de una sola a la vez.
// "Generar circuito" (formats::synthesizeMultiOutputToCircuit) sintetiza
// las N funciones JUNTAS en un unico CircuitDocument nuevo del mismo
// proyecto (entradas compartidas, una salida por funcion -- mismo mecanismo
// que ui::TruthTableView::onGenerateCircuitClicked()). Una instancia por
// documento abierto (ver MainWindow::karnaughViews_).
class KarnaughMapView : public QWidget {
    Q_OBJECT

public:
    KarnaughMapView(editor::Project* project, uint32_t documentId, editor::KarnaughDocument* document,
                    QWidget* parent = nullptr);

public slots:
    // Publico para que MainWindow lo pueda disparar desde el menu "Karnaugh"
    // (accion "Generar circuito") sin pasar por el boton propio de la vista
    // -- misma operacion, dos formas de invocarla.
    void onGenerateCircuitClicked();

private slots:
    void onVariableCountChanged(int index);
    void onAddOutputClicked();
    void onRemoveOutputClicked();
    void onDocumentVariablesChanged();
    void onDocumentOutputsChanged();
    void onDocumentCellChanged(int outputIndex, int minterm);

private:
    // Un bloque autocontenido por funcion: grilla + nombre editable +
    // controles de analisis/pasos + expresion SOP, todos apilados dentro de
    // panelsLayout_. outputIndex es fijo para la vida del panel (rebuildPanels()
    // destruye y recrea TODOS los panels ante cualquier cambio estructural,
    // asi que nunca hace falta reasignarle el indice a uno existente).
    struct FunctionPanel {
        int outputIndex = 0;
        QWidget* container = nullptr;
        QLineEdit* nameEdit = nullptr;
        KarnaughGridCanvas* canvas = nullptr;
        QLabel* stepDescriptionLabel = nullptr;
        QLabel* sopExpressionLabel = nullptr;
        QPushButton* analyzeButton = nullptr;
        QPushButton* prevStepButton = nullptr;
        QPushButton* nextStepButton = nullptr;
        editor::KarnaughResult result;
        int stepIndex = -1;
    };

    void rebuildVariableNameEditors();
    // Destruye y reconstruye un FunctionPanel por cada document_->outputCount(),
    // en orden -- usado en el constructor y cada vez que cambia la cantidad
    // de variables o de funciones (ambas invalidan cualquier grilla/analisis
    // ya armado).
    void rebuildPanels();
    void updateStepControls(FunctionPanel& panel);
    [[nodiscard]] std::vector<QString> currentVariableNames() const;

    editor::Project* project_;
    uint32_t documentId_;
    editor::KarnaughDocument* document_;
    std::vector<FunctionPanel> panels_;

    QComboBox* variableCountCombo_ = nullptr;
    QHBoxLayout* variableNamesLayout_ = nullptr;
    std::vector<QLineEdit*> variableNameEdits_;
    QPushButton* addOutputButton_ = nullptr;
    QPushButton* removeOutputButton_ = nullptr;
    QScrollArea* panelsScroll_ = nullptr;
    QVBoxLayout* panelsLayout_ = nullptr;
    QPushButton* generateCircuitButton_ = nullptr;
};

} // namespace digitalforge::ui
