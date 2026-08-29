#include "TruthTableView.hpp"

#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

#include <QApplication>
#include <QStyleHints>

#include "Theme.hpp"
#include "editor/KarnaughDocument.hpp"
#include "editor/KarnaughMap.hpp"
#include "editor/Project.hpp"
#include "editor/TruthTableDocument.hpp"
#include "formats/KarnaughSynthesizer.hpp"

namespace digitalforge::ui {

using editor::KarnaughCellValue;
using editor::KarnaughResult;
using editor::Project;
using editor::TruthTableDocument;
using formats::OutputSpec;

namespace {

QString charForCellValue(KarnaughCellValue value) {
    switch (value) {
        case KarnaughCellValue::Zero:
            return QStringLiteral("0");
        case KarnaughCellValue::One:
            return QStringLiteral("1");
        case KarnaughCellValue::DontCare:
            return QStringLiteral("X");
    }
    return QStringLiteral("0");
}

// La fila visual `row` cuenta en orden ascendente natural (columna de la
// PRIMERA variable = bit mas significativo, ultima columna = menos
// significativo -- lectura izquierda a derecha como un numero binario
// comun). El minterm interno que usa TruthTableDocument/KarnaughMap tiene
// la convencion opuesta (variableIndex i <-> bit i, primera variable =
// bit menos significativo -- ver Implicant::literals()), asi que hace
// falta invertir el orden de los bits al leer o escribir una celda. Como
// reverseBits() es su propia inversa para un mismo bitCount, sirve para
// las dos direcciones (row->minterm y minterm->row).
int reverseBits(int value, int bitCount) {
    int result = 0;
    for (int i = 0; i < bitCount; ++i) {
        result |= ((value >> i) & 1) << (bitCount - 1 - i);
    }
    return result;
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

// Mismo criterio que ui::KarnaughMapView/ui::WaveformPanel: nunca se
// cachea, se vuelve a leer el palette activo cada vez que hace falta
// repintar para que la tabla se actualice sola al cambiar de tema
// claro/oscuro (ver las conexiones a colorSchemeChanged/ThemeManager::changed
// en el constructor de TruthTableView).
bool isDarkPalette() { return QApplication::palette().color(QPalette::Base).lightness() < 128; }

// Fondo de las celdas de ENTRADA: distingue a simple vista la columna donde
// el gesto es "intercambiar de fila" (clic) de la de SALIDA (fondo por
// defecto), donde el gesto es "ciclar 0/1/X" -- un tono apenas mas claro que
// el fondo base en tema oscuro, apenas mas oscuro en tema claro, para que se
// note la diferencia sin chocar con ninguno de los dos temas.
QColor inputCellBackground() { return isDarkPalette() ? QColor(55, 55, 58) : QColor(235, 235, 235); }

// Item no editable (nada de doble-clic-para-escribir-texto): tanto las
// celdas de bit de entrada como las de salida se cambian con gestos propios
// (clic para las de salida; clic para intercambiar de fila en las de
// entrada -- ver onCellClicked()).
QTableWidgetItem* makeItem(const QString& text, bool isInputColumn) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (isInputColumn) {
        item->setBackground(inputCellBackground());
    }
    return item;
}

} // namespace

TruthTableView::TruthTableView(Project* project, uint32_t documentId, TruthTableDocument* document, QWidget* parent)
    : QWidget(parent), project_(project), documentId_(documentId), document_(document) {
    auto* layout = new QVBoxLayout(this);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel(tr("Variables:"), this));
    variableCountCombo_ = new QComboBox(this);
    variableCountCombo_->addItems({"2", "3", "4"});
    variableCountCombo_->setCurrentIndex(document_->variableCount() - 2);
    connect(variableCountCombo_, &QComboBox::currentIndexChanged, this, &TruthTableView::onVariableCountChanged);
    topRow->addWidget(variableCountCombo_);
    topRow->addSpacing(16);

    addOutputButton_ = new QPushButton(tr("+ Agregar salida"), this);
    connect(addOutputButton_, &QPushButton::clicked, this, &TruthTableView::onAddOutputClicked);
    topRow->addWidget(addOutputButton_);

    removeOutputButton_ = new QPushButton(tr("- Quitar ultima salida"), this);
    connect(removeOutputButton_, &QPushButton::clicked, this, &TruthTableView::onRemoveOutputClicked);
    topRow->addWidget(removeOutputButton_);
    topRow->addStretch();
    layout->addLayout(topRow);

    table_ = new QTableWidget(this);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionsClickable(true);
    connect(table_, &QTableWidget::cellClicked, this, &TruthTableView::onCellClicked);
    connect(table_->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this,
            &TruthTableView::onHeaderDoubleClicked);
    layout->addWidget(table_);

    // El fondo de las celdas de entrada (inputCellBackground()) se calcula a
    // partir del palette activo y se hornea en cada QTableWidgetItem al
    // crearlo -- no se actualiza solo, asi que un cambio de tema (manual o
    // del SO) tiene que forzar un rebuildTable() para recalcularlo. Mismo
    // patron que ui::SimulationToolbar/ui::ZoomControl con refreshIcons().
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
        rebuildTable();
    });
    // Forzar un tema cambia la paleta a mano, sin emitir colorSchemeChanged.
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, [this] { rebuildTable(); });

    auto* helpLabel = new QLabel(
        tr("Doble clic en un encabezado para renombrarlo. Clic en una celda de salida para ciclar 0/1/X. Clic en "
           "una celda de entrada para intercambiar esa fila con la que ya tenia esa combinacion."),
        this);
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    auto* actionsRow = new QHBoxLayout();
    generateMapsButton_ = new QPushButton(tr("Generar mapas"), this);
    generateMapsButton_->setToolTip(
        tr("Crea un mapa de Karnaugh con una funcion por columna de salida (todas en la misma pestana, con un "
           "selector para elegir cual se muestra) -- para inspeccionar o animar el procedimiento de cada una antes "
           "de generar el circuito."));
    connect(generateMapsButton_, &QPushButton::clicked, this, &TruthTableView::onGenerateMapsClicked);
    actionsRow->addWidget(generateMapsButton_);

    generateCircuitButton_ = new QPushButton(tr("Generar circuito"), this);
    connect(generateCircuitButton_, &QPushButton::clicked, this, &TruthTableView::onGenerateCircuitClicked);
    actionsRow->addWidget(generateCircuitButton_);
    layout->addLayout(actionsRow);

    connect(document_, &TruthTableDocument::variablesChanged, this, &TruthTableView::onStructureChanged);
    connect(document_, &TruthTableDocument::outputsChanged, this, &TruthTableView::onStructureChanged);
    connect(document_, &TruthTableDocument::cellChanged, this, &TruthTableView::onCellValueChanged);

    rebuildTable();
}

std::vector<QString> TruthTableView::currentVariableNames() const {
    std::vector<QString> names;
    names.reserve(static_cast<std::size_t>(document_->variableCount()));
    for (int i = 0; i < document_->variableCount(); ++i) {
        names.push_back(document_->variableName(i));
    }
    return names;
}

void TruthTableView::rebuildTable() {
    const int variableCount = document_->variableCount();
    const int outputCount = document_->outputCount();
    const int rowCount = 1 << variableCount;

    // Solo reinicia el orden si cambio la cantidad de filas (variableCount) --
    // agregar/quitar una columna de salida no debe descartar un
    // reordenamiento manual que el usuario ya haya hecho a mano.
    if (static_cast<int>(rowToMinterm_.size()) != rowCount) {
        rowToMinterm_.resize(rowCount);
        for (int row = 0; row < rowCount; ++row) {
            rowToMinterm_[row] = reverseBits(row, variableCount);
        }
    }

    const QSignalBlocker blocker(table_);
    table_->clear();
    table_->setRowCount(rowCount);
    table_->setColumnCount(variableCount + outputCount);

    QStringList headers;
    for (int i = 0; i < variableCount; ++i) {
        headers << document_->variableName(i);
    }
    for (int o = 0; o < outputCount; ++o) {
        headers << document_->outputName(o);
    }
    table_->setHorizontalHeaderLabels(headers);

    for (int row = 0; row < rowCount; ++row) {
        populateRow(row);
    }
    table_->resizeColumnsToContents();

    removeOutputButton_->setEnabled(outputCount > 1);
}

void TruthTableView::populateRow(int row) {
    const int variableCount = document_->variableCount();
    const int outputCount = document_->outputCount();
    const int minterm = rowToMinterm_[static_cast<std::size_t>(row)];
    for (int i = 0; i < variableCount; ++i) {
        const int bit = (minterm >> i) & 1;
        table_->setItem(row, i, makeItem(QString::number(bit), /*isInputColumn=*/true));
    }
    for (int o = 0; o < outputCount; ++o) {
        table_->setItem(row, variableCount + o,
                         makeItem(charForCellValue(document_->cellValue(o, minterm)), /*isInputColumn=*/false));
    }
}

void TruthTableView::onCellClicked(int row, int column) {
    const int variableCount = document_->variableCount();
    if (column < variableCount) {
        // Celda de entrada: togglear el bit de esa variable e intercambiar
        // esta fila con la que ya tenia la combinacion resultante -- asi las
        // 2^N combinaciones se siguen cubriendo exactamente una vez cada
        // una, solo se reacomoda cual fila muestra cual.
        const int newMinterm = rowToMinterm_[static_cast<std::size_t>(row)] ^ (1 << column);
        const auto row2It = std::find(rowToMinterm_.begin(), rowToMinterm_.end(), newMinterm);
        const int row2 = static_cast<int>(std::distance(rowToMinterm_.begin(), row2It));
        std::swap(rowToMinterm_[static_cast<std::size_t>(row)], rowToMinterm_[static_cast<std::size_t>(row2)]);
        populateRow(row);
        populateRow(row2);
        return;
    }
    const int outputIndex = column - variableCount;
    const int minterm = rowToMinterm_[static_cast<std::size_t>(row)];
    document_->setCellValue(outputIndex, minterm, nextCellValue(document_->cellValue(outputIndex, minterm)));
}

void TruthTableView::onHeaderDoubleClicked(int section) {
    const int variableCount = document_->variableCount();
    bool ok = false;
    if (section < variableCount) {
        const QString name = QInputDialog::getText(this, tr("Renombrar variable"), tr("Nombre:"), QLineEdit::Normal,
                                                     document_->variableName(section), &ok);
        if (ok && !name.isEmpty()) {
            document_->setVariableName(section, name);
        }
        return;
    }
    const int outputIndex = section - variableCount;
    const QString name = QInputDialog::getText(this, tr("Renombrar salida"), tr("Nombre:"), QLineEdit::Normal,
                                                 document_->outputName(outputIndex), &ok);
    if (ok && !name.isEmpty()) {
        document_->setOutputName(outputIndex, name);
    }
}

void TruthTableView::onVariableCountChanged(int index) {
    const int newCount = index + 2;
    if (newCount == document_->variableCount()) {
        return;
    }
    document_->setVariableCount(newCount);
}

void TruthTableView::onAddOutputClicked() { document_->addOutput(); }

void TruthTableView::onRemoveOutputClicked() {
    try {
        document_->removeOutput(document_->outputCount() - 1);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se pudo quitar la salida"), QString::fromUtf8(error.what()));
    }
}

void TruthTableView::onCellValueChanged(int outputIndex, int minterm) {
    const auto rowIt = std::find(rowToMinterm_.begin(), rowToMinterm_.end(), minterm);
    if (rowIt == rowToMinterm_.end()) {
        return; // documento en medio de un cambio de variableCount -- rebuildTable() ya viene en camino
    }
    const int row = static_cast<int>(std::distance(rowToMinterm_.begin(), rowIt));
    QTableWidgetItem* item = table_->item(row, document_->variableCount() + outputIndex);
    if (item != nullptr) {
        item->setText(charForCellValue(document_->cellValue(outputIndex, minterm)));
    }
}

void TruthTableView::onStructureChanged() {
    if (variableCountCombo_->currentIndex() != document_->variableCount() - 2) {
        const QSignalBlocker blocker(variableCountCombo_);
        variableCountCombo_->setCurrentIndex(document_->variableCount() - 2);
    }
    rebuildTable();
}

void TruthTableView::onGenerateMapsClicked() {
    try {
        const std::vector<QString> names = currentVariableNames();
        // Un solo KarnaughDocument con una salida por columna de la tabla --
        // asi las N funciones quedan juntas en una sola pestana, en vez de
        // abrir un mapa de Karnaugh separado por cada una. Mismo patron
        // "reusar la salida 0 de reset(), addOutput() para el resto" que
        // usa formats::loadTruthTableDocument.
        const QString suggested = project_->suggestUniqueDocumentName(
            project_->truthTableDocumentName(documentId_) + tr(" (mapas)"));
        const uint32_t newId = project_->addKarnaughDocument(suggested, document_->variableCount());
        editor::KarnaughDocument* karnaughDoc = project_->karnaughDocument(newId);
        for (int i = 0; i < document_->variableCount(); ++i) {
            karnaughDoc->setVariableName(i, names[static_cast<std::size_t>(i)]);
        }
        for (int o = 0; o < document_->outputCount(); ++o) {
            const int outputIndex = o == 0 ? 0 : karnaughDoc->addOutput();
            karnaughDoc->setOutputName(outputIndex, document_->outputName(o));
            const std::vector<KarnaughCellValue>& cells = document_->outputCells(o);
            for (std::size_t m = 0; m < cells.size(); ++m) {
                karnaughDoc->setCellValue(outputIndex, static_cast<int>(m), cells[m]);
            }
        }
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se pudieron generar los mapas"), QString::fromUtf8(error.what()));
    }
}

void TruthTableView::onGenerateCircuitClicked() {
    try {
        const std::vector<QString> names = currentVariableNames();
        std::vector<OutputSpec> outputs;
        outputs.reserve(static_cast<std::size_t>(document_->outputCount()));
        for (int o = 0; o < document_->outputCount(); ++o) {
            OutputSpec spec;
            spec.name = document_->outputName(o);
            spec.result = editor::minimize(document_->variableCount(), names, document_->outputCells(o));
            outputs.push_back(std::move(spec));
        }

        const QString suggested = project_->suggestUniqueDocumentName(
            project_->truthTableDocumentName(documentId_) + tr(" (circuito)"));
        const uint32_t newId = project_->addDocument(suggested);
        formats::synthesizeMultiOutputToCircuit(*project_->document(newId), document_->variableCount(), names,
                                                 outputs);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se pudo generar el circuito"), QString::fromUtf8(error.what()));
    }
}

} // namespace digitalforge::ui
