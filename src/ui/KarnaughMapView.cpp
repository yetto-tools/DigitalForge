#include "KarnaughMapView.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

#include "editor/CircuitDocument.hpp"
#include "editor/KarnaughDocument.hpp"
#include "editor/Project.hpp"
#include "formats/KarnaughSynthesizer.hpp"

namespace digitalforge::ui {

using editor::gridDimensions;
using editor::KarnaughCellValue;
using editor::KarnaughDocument;
using editor::KarnaughResult;
using editor::mintermAt;
using editor::Project;

namespace {

constexpr int kCellSize = 56;
constexpr int kHeaderSize = 26;

// Mismo criterio que ui::IconFactory/WaveformPanel: nunca se cachea, cada
// pintado vuelve a leer el palette activo para que el canvas se actualice
// solo al cambiar de tema claro/oscuro.
bool isDarkPalette() { return QApplication::palette().color(QPalette::Base).lightness() < 128; }

QString bitsToString(int value, int bitCount) {
    QString result;
    for (int bit = bitCount - 1; bit >= 0; --bit) {
        result += ((value >> bit) & 1) != 0 ? QLatin1Char('1') : QLatin1Char('0');
    }
    return result;
}

QChar charForCellValue(KarnaughCellValue value) {
    switch (value) {
        case KarnaughCellValue::Zero:
            return QLatin1Char('0');
        case KarnaughCellValue::One:
            return QLatin1Char('1');
        case KarnaughCellValue::DontCare:
            return QLatin1Char('X');
    }
    return QLatin1Char('0');
}

KarnaughCellValue nextCellValue(KarnaughCellValue value) {
    switch (value) {
        case KarnaughCellValue::Zero:
            return KarnaughCellValue::One;
        case KarnaughCellValue::One:
            return KarnaughCellValue::DontCare;
        case KarnaughCellValue::DontCare:
            return KarnaughCellValue::Zero;
    }
    return KarnaughCellValue::Zero;
}

} // namespace

KarnaughGridCanvas::KarnaughGridCanvas(KarnaughDocument* document, int outputIndex, QWidget* parent)
    : QWidget(parent), document_(document), outputIndex_(outputIndex) {
    rebuildGeometry();
}

void KarnaughGridCanvas::setDocument(KarnaughDocument* document) {
    document_ = document;
    highlighted_.clear();
    rebuildGeometry();
    update();
}

void KarnaughGridCanvas::setHighlightedMinterms(std::vector<int> minterms) {
    highlighted_ = std::move(minterms);
    update();
}

QSize KarnaughGridCanvas::sizeHint() const { return minimumSize(); }

void KarnaughGridCanvas::rebuildGeometry() {
    const int variableCount = document_->variableCount();
    const auto [rows, cols] = gridDimensions(variableCount);
    const int rowVarCount = variableCount / 2;

    cellRects_.assign(std::size_t{1} << variableCount, QRectF());
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int minterm = mintermAt(row, col, variableCount);
            cellRects_[static_cast<std::size_t>(minterm)] =
                QRectF(kHeaderSize + col * kCellSize, kHeaderSize + row * kCellSize, kCellSize, kCellSize);
        }
    }
    (void)rowVarCount;
    setMinimumSize(kHeaderSize + cols * kCellSize + 1, kHeaderSize + rows * kCellSize + 1);
}

void KarnaughGridCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const int variableCount = document_->variableCount();
    const auto [rows, cols] = gridDimensions(variableCount);
    const int rowVarCount = variableCount / 2;
    const int colVarCount = variableCount - rowVarCount;

    const bool dark = isDarkPalette();
    const QColor textColor = dark ? QColor(225, 225, 225) : QColor(30, 30, 30);
    const QColor gridColor = dark ? QColor(90, 90, 90) : QColor(180, 180, 180);
    const QColor cellFill = dark ? QColor(45, 45, 48) : QColor(250, 250, 250);
    const QColor highlightFill = dark ? QColor(60, 100, 150) : QColor(180, 215, 250);

    QFont headerFont = painter.font();
    headerFont.setPointSizeF(headerFont.pointSizeF() * 0.85);

    // Encabezados Gray-code de fila/columna: en la fila/columna 0, el otro
    // eje contribuye 0 a esos bits (ver mintermAt()), asi que enmascarar/
    // correr directamente el minterm de esa fila/columna alcanza sin
    // necesitar el codigo Gray por separado aca.
    painter.setFont(headerFont);
    painter.setPen(textColor);
    for (int col = 0; col < cols; ++col) {
        const int colBits = mintermAt(0, col, variableCount) >> rowVarCount;
        painter.drawText(QRectF(kHeaderSize + col * kCellSize, 0, kCellSize, kHeaderSize), Qt::AlignCenter,
                          bitsToString(colBits, colVarCount));
    }
    for (int row = 0; row < rows; ++row) {
        const int rowBits = mintermAt(row, 0, variableCount) & ((1 << rowVarCount) - 1);
        painter.drawText(QRectF(0, kHeaderSize + row * kCellSize, kHeaderSize, kCellSize), Qt::AlignCenter,
                          bitsToString(rowBits, rowVarCount));
    }

    QFont cellFont = painter.font();
    cellFont.setPointSizeF(cellFont.pointSizeF() * 1.3);
    painter.setFont(cellFont);

    for (std::size_t minterm = 0; minterm < cellRects_.size(); ++minterm) {
        const QRectF& rect = cellRects_[minterm];
        const bool isHighlighted =
            std::find(highlighted_.begin(), highlighted_.end(), static_cast<int>(minterm)) != highlighted_.end();
        painter.setPen(Qt::NoPen);
        painter.setBrush(isHighlighted ? highlightFill : cellFill);
        painter.drawRect(rect);
        painter.setPen(QPen(gridColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);

        painter.setPen(textColor);
        painter.drawText(rect, Qt::AlignCenter,
                          QString(charForCellValue(document_->cellValue(outputIndex_, static_cast<int>(minterm)))));
    }
}

void KarnaughGridCanvas::mousePressEvent(QMouseEvent* event) {
    for (std::size_t minterm = 0; minterm < cellRects_.size(); ++minterm) {
        if (cellRects_[minterm].contains(event->position())) {
            emit cellClicked(static_cast<int>(minterm));
            return;
        }
    }
}

KarnaughMapView::KarnaughMapView(Project* project, uint32_t documentId, KarnaughDocument* document, QWidget* parent)
    : QWidget(parent), project_(project), documentId_(documentId), document_(document) {
    auto* layout = new QVBoxLayout(this);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel(tr("Variables:"), this));
    variableCountCombo_ = new QComboBox(this);
    variableCountCombo_->addItems({"2", "3", "4"});
    variableCountCombo_->setCurrentIndex(document_->variableCount() - 2);
    connect(variableCountCombo_, &QComboBox::currentIndexChanged, this, &KarnaughMapView::onVariableCountChanged);
    topRow->addWidget(variableCountCombo_);
    topRow->addStretch();
    layout->addLayout(topRow);

    variableNamesLayout_ = new QHBoxLayout();
    layout->addLayout(variableNamesLayout_);
    rebuildVariableNameEditors();

    auto* outputRow = new QHBoxLayout();
    addOutputButton_ = new QPushButton(tr("+ Agregar salida"), this);
    connect(addOutputButton_, &QPushButton::clicked, this, &KarnaughMapView::onAddOutputClicked);
    outputRow->addWidget(addOutputButton_);
    removeOutputButton_ = new QPushButton(tr("- Quitar ultima salida"), this);
    connect(removeOutputButton_, &QPushButton::clicked, this, &KarnaughMapView::onRemoveOutputClicked);
    outputRow->addWidget(removeOutputButton_);
    outputRow->addStretch();
    layout->addLayout(outputRow);

    // Un panel autocontenido por funcion (grilla + nombre + analisis
    // propio), apilados verticalmente dentro de un area con scroll -- asi
    // las N funciones se ven TODAS juntas en la misma pestana, en vez de
    // una sola a la vez.
    panelsScroll_ = new QScrollArea(this);
    panelsScroll_->setWidgetResizable(true);
    auto* panelsContainer = new QWidget(panelsScroll_);
    panelsLayout_ = new QVBoxLayout(panelsContainer);
    panelsLayout_->addStretch(); // placeholder, rebuildPanels() lo saca antes de agregar panels
    panelsScroll_->setWidget(panelsContainer);
    layout->addWidget(panelsScroll_, /*stretch=*/1);
    rebuildPanels();

    generateCircuitButton_ = new QPushButton(tr("Generar circuito"), this);
    generateCircuitButton_->setToolTip(
        tr("Sintetiza las N funciones juntas en un unico circuito nuevo (entradas compartidas, una salida por "
           "funcion)."));
    connect(generateCircuitButton_, &QPushButton::clicked, this, &KarnaughMapView::onGenerateCircuitClicked);
    layout->addWidget(generateCircuitButton_);

    connect(document_, &KarnaughDocument::variablesChanged, this, &KarnaughMapView::onDocumentVariablesChanged);
    connect(document_, &KarnaughDocument::outputsChanged, this, &KarnaughMapView::onDocumentOutputsChanged);
    connect(document_, &KarnaughDocument::cellChanged, this, &KarnaughMapView::onDocumentCellChanged);
}

std::vector<QString> KarnaughMapView::currentVariableNames() const {
    std::vector<QString> names;
    names.reserve(variableNameEdits_.size());
    for (QLineEdit* edit : variableNameEdits_) {
        names.push_back(edit->text());
    }
    return names;
}

void KarnaughMapView::rebuildVariableNameEditors() {
    for (QLineEdit* edit : variableNameEdits_) {
        edit->deleteLater();
    }
    variableNameEdits_.clear();

    for (int i = 0; i < document_->variableCount(); ++i) {
        auto* edit = new QLineEdit(document_->variableName(i), this);
        edit->setMaximumWidth(60);
        connect(edit, &QLineEdit::editingFinished, this, [this, i, edit] { document_->setVariableName(i, edit->text()); });
        variableNamesLayout_->addWidget(edit);
        variableNameEdits_.push_back(edit);
    }
}

void KarnaughMapView::rebuildPanels() {
    // Destruye los panels existentes (y el stretch placeholder inicial) por
    // completo -- mas simple que reconciliar cual sobrevive, y con la
    // cantidad de funciones esperada (unas pocas) el costo es
    // insignificante, mismo criterio que ui::ProjectTree::repopulate().
    QLayoutItem* item = nullptr;
    while ((item = panelsLayout_->takeAt(0)) != nullptr) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    panels_.clear();

    for (int o = 0; o < document_->outputCount(); ++o) {
        FunctionPanel panel;
        panel.outputIndex = o;

        panel.container = new QFrame(panelsScroll_->widget());
        static_cast<QFrame*>(panel.container)->setFrameShape(QFrame::StyledPanel);
        auto* panelLayout = new QVBoxLayout(panel.container);

        auto* nameRow = new QHBoxLayout();
        nameRow->addWidget(new QLabel(tr("Funcion:"), panel.container));
        panel.nameEdit = new QLineEdit(document_->outputName(o), panel.container);
        panel.nameEdit->setMaximumWidth(160);
        connect(panel.nameEdit, &QLineEdit::editingFinished, this,
                [this, o, edit = panel.nameEdit] { document_->setOutputName(o, edit->text()); });
        nameRow->addWidget(panel.nameEdit);
        nameRow->addStretch();
        panelLayout->addLayout(nameRow);

        panel.canvas = new KarnaughGridCanvas(document_, o, panel.container);
        connect(panel.canvas, &KarnaughGridCanvas::cellClicked, this, [this, o](int minterm) {
            document_->setCellValue(o, minterm, nextCellValue(document_->cellValue(o, minterm)));
        });
        panelLayout->addWidget(panel.canvas);

        auto* stepRow = new QHBoxLayout();
        panel.analyzeButton = new QPushButton(tr("Analizar"), panel.container);
        stepRow->addWidget(panel.analyzeButton);
        panel.prevStepButton = new QPushButton(tr("< Paso anterior"), panel.container);
        stepRow->addWidget(panel.prevStepButton);
        panel.nextStepButton = new QPushButton(tr("Siguiente paso >"), panel.container);
        stepRow->addWidget(panel.nextStepButton);
        stepRow->addStretch();
        panelLayout->addLayout(stepRow);

        panel.stepDescriptionLabel = new QLabel(tr("Presione \"Analizar\" para minimizar esta funcion."), panel.container);
        panel.stepDescriptionLabel->setWordWrap(true);
        panelLayout->addWidget(panel.stepDescriptionLabel);

        auto* sopRow = new QHBoxLayout();
        sopRow->addWidget(new QLabel(tr("Expresion minimizada:"), panel.container));
        panel.sopExpressionLabel = new QLabel(panel.container);
        QFont sopFont = panel.sopExpressionLabel->font();
        sopFont.setBold(true);
        panel.sopExpressionLabel->setFont(sopFont);
        sopRow->addWidget(panel.sopExpressionLabel);
        sopRow->addStretch();
        panelLayout->addLayout(sopRow);

        panelsLayout_->addWidget(panel.container);
        panels_.push_back(panel);
    }
    panelsLayout_->addStretch();

    // Los botones de analisis se conectan recien aca (necesitan una
    // referencia estable a panels_[index], que solo existe una vez que el
    // vector completo esta armado -- push_back() de arriba podria haber
    // reubicado elementos anteriores en memoria).
    for (std::size_t i = 0; i < panels_.size(); ++i) {
        FunctionPanel& panel = panels_[i];
        const int outputIndex = panel.outputIndex;
        connect(panel.analyzeButton, &QPushButton::clicked, this, [this, outputIndex] {
            FunctionPanel& p = panels_[static_cast<std::size_t>(outputIndex)];
            p.result = editor::minimize(document_->variableCount(), currentVariableNames(),
                                         document_->outputCells(outputIndex));
            p.stepIndex = -1;
            p.canvas->setHighlightedMinterms({});
            p.sopExpressionLabel->setText(p.result.sopExpression);
            p.stepDescriptionLabel->setText(p.result.steps.empty()
                                                 ? tr("Sin pasos: la funcion ya es constante.")
                                                 : tr("Minimizado. Use \"Siguiente paso\" para ver el procedimiento."));
            updateStepControls(p);
        });
        connect(panel.nextStepButton, &QPushButton::clicked, this, [this, outputIndex] {
            FunctionPanel& p = panels_[static_cast<std::size_t>(outputIndex)];
            if (p.stepIndex + 1 >= static_cast<int>(p.result.steps.size())) {
                return;
            }
            ++p.stepIndex;
            p.canvas->setHighlightedMinterms(p.result.steps[static_cast<std::size_t>(p.stepIndex)].involvedMinterms);
            p.stepDescriptionLabel->setText(p.result.steps[static_cast<std::size_t>(p.stepIndex)].description);
            updateStepControls(p);
        });
        connect(panel.prevStepButton, &QPushButton::clicked, this, [this, outputIndex] {
            FunctionPanel& p = panels_[static_cast<std::size_t>(outputIndex)];
            if (p.stepIndex <= 0) {
                p.stepIndex = -1;
                p.canvas->setHighlightedMinterms({});
                p.stepDescriptionLabel->setText(tr("Presione \"Siguiente paso\" para empezar el procedimiento."));
                updateStepControls(p);
                return;
            }
            --p.stepIndex;
            p.canvas->setHighlightedMinterms(p.result.steps[static_cast<std::size_t>(p.stepIndex)].involvedMinterms);
            p.stepDescriptionLabel->setText(p.result.steps[static_cast<std::size_t>(p.stepIndex)].description);
            updateStepControls(p);
        });
        updateStepControls(panel);
    }

    removeOutputButton_->setEnabled(document_->outputCount() > 1);
}

void KarnaughMapView::updateStepControls(FunctionPanel& panel) {
    const bool hasSteps = !panel.result.steps.empty();
    panel.prevStepButton->setEnabled(hasSteps && panel.stepIndex >= 0);
    panel.nextStepButton->setEnabled(hasSteps && panel.stepIndex + 1 < static_cast<int>(panel.result.steps.size()));
}

void KarnaughMapView::onVariableCountChanged(int index) {
    const int newCount = index + 2;
    if (newCount == document_->variableCount()) {
        return;
    }
    document_->setVariableCount(newCount);
}

void KarnaughMapView::onAddOutputClicked() { document_->addOutput(); }

void KarnaughMapView::onRemoveOutputClicked() {
    try {
        document_->removeOutput(document_->outputCount() - 1);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se pudo quitar la salida"), QString::fromUtf8(error.what()));
    }
}

void KarnaughMapView::onDocumentVariablesChanged() {
    if (variableCountCombo_->currentIndex() != document_->variableCount() - 2) {
        const QSignalBlocker blocker(variableCountCombo_);
        variableCountCombo_->setCurrentIndex(document_->variableCount() - 2);
    }
    rebuildVariableNameEditors();
    // La geometria de cada grilla depende de variableCount, y cualquier
    // resultado de "Analizar" ya calculado quedo obsoleto -- mas simple
    // reconstruir todos los panels de una que actualizar cada grilla y
    // descartar cada resultado por separado.
    rebuildPanels();
}

void KarnaughMapView::onDocumentOutputsChanged() { rebuildPanels(); }

void KarnaughMapView::onDocumentCellChanged(int outputIndex, int minterm) {
    (void)minterm;
    if (outputIndex >= 0 && outputIndex < static_cast<int>(panels_.size())) {
        panels_[static_cast<std::size_t>(outputIndex)].canvas->update();
    }
}

void KarnaughMapView::onGenerateCircuitClicked() {
    try {
        // Se recalcula siempre de una, sin depender de ningun resultado de
        // "Analizar" ya cacheado: si el usuario edito celdas despues del
        // ultimo analisis, el circuito generado debe reflejar el mapa TAL
        // COMO esta ahora. Las N funciones se sintetizan JUNTAS en un solo
        // circuito (entradas compartidas, una salida por funcion) -- mismo
        // mecanismo que ui::TruthTableView::onGenerateCircuitClicked().
        const std::vector<QString> names = currentVariableNames();
        std::vector<formats::OutputSpec> outputs;
        outputs.reserve(static_cast<std::size_t>(document_->outputCount()));
        for (int o = 0; o < document_->outputCount(); ++o) {
            formats::OutputSpec spec;
            spec.name = document_->outputName(o);
            spec.result = editor::minimize(document_->variableCount(), names, document_->outputCells(o));
            outputs.push_back(std::move(spec));
        }
        const QString suggested =
            project_->suggestUniqueDocumentName(project_->karnaughDocumentName(documentId_) + tr(" (circuito)"));
        const uint32_t newId = project_->addDocument(suggested);
        formats::synthesizeMultiOutputToCircuit(*project_->document(newId), document_->variableCount(), names,
                                                 outputs);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se pudo generar el circuito"), QString::fromUtf8(error.what()));
    }
}

} // namespace digitalforge::ui
