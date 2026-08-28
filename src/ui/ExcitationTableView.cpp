#include "ExcitationTableView.hpp"

#include <QAbstractItemView>
#include <QApplication>
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
#include <QStringList>
#include <QStyleHints>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.hpp"
#include "editor/ExcitationTableDocument.hpp"
#include "editor/FlipFlopExcitation.hpp"
#include "editor/KarnaughDocument.hpp"
#include "editor/KarnaughMap.hpp"
#include "editor/Project.hpp"
#include "formats/KarnaughSynthesizer.hpp"

namespace digitalforge::ui {

using editor::ExcitationColumn;
using editor::ExcitationTableDocument;
using editor::FlipFlopType;
using editor::KarnaughCellValue;
using editor::Project;
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

// Misma convencion que ui::TruthTableView: la fila visual `row` cuenta en
// orden ascendente natural (primer bit de estado = mas significativo,
// lectura izquierda a derecha como un numero binario comun), mientras que el
// minterm interno usa la convencion opuesta (bit i <-> bit de estado i,
// LSB-first). reverseBits() es su propia inversa para un mismo bitCount, asi
// que sirve para las dos direcciones.
int reverseBits(int value, int bitCount) {
    int result = 0;
    for (int i = 0; i < bitCount; ++i) {
        result |= ((value >> i) & 1) << (bitCount - 1 - i);
    }
    return result;
}

bool isDarkPalette() { return QApplication::palette().color(QPalette::Base).lightness() < 128; }

// Las columnas de estado actual son tan editables (clic cicla 0/1/X) como
// las de estado siguiente -- este tinte es solo para separarlas a simple
// vista en dos grupos, no implica "de solo lectura".
QColor presentStateBackground() { return isDarkPalette() ? QColor(55, 55, 58) : QColor(235, 235, 235); }

QTableWidgetItem* makeItem(const QString& text, bool isPresentStateColumn) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (isPresentStateColumn) {
        item->setBackground(presentStateBackground());
    }
    return item;
}

} // namespace

ExcitationTableView::ExcitationTableView(Project* project, uint32_t documentId, ExcitationTableDocument* document,
                                          QWidget* parent)
    : QWidget(parent), project_(project), documentId_(documentId), document_(document) {
    auto* layout = new QVBoxLayout(this);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel(tr("Bits de estado:"), this));
    stateBitCountCombo_ = new QComboBox(this);
    stateBitCountCombo_->addItems({"2", "3", "4"});
    stateBitCountCombo_->setCurrentIndex(document_->stateBitCount() - 2);
    connect(stateBitCountCombo_, &QComboBox::currentIndexChanged, this, &ExcitationTableView::onStateBitCountChanged);
    topRow->addWidget(stateBitCountCombo_);
    topRow->addStretch();
    layout->addLayout(topRow);

    flipFlopTypeLayout_ = new QHBoxLayout();
    layout->addLayout(flipFlopTypeLayout_);
    rebuildFlipFlopTypeCombos();

    table_ = new QTableWidget(this);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionsClickable(true);
    connect(table_, &QTableWidget::cellClicked, this, &ExcitationTableView::onCellClicked);
    connect(table_->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this,
            &ExcitationTableView::onHeaderDoubleClicked);
    layout->addWidget(table_);

    // Mismo criterio que ui::TruthTableView/ui::KarnaughMapView: el tinte de
    // las columnas de estado actual se hornea en cada QTableWidgetItem al
    // crearlo, asi que un cambio de tema tiene que forzar un rebuildTable().
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { rebuildTable(); });
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, [this] { rebuildTable(); });

    auto* helpLabel = new QLabel(
        tr("Doble clic en un encabezado renombra el bit. Clic en cualquier celda (estado actual o siguiente) cicla "
           "0/1/X -- marcar el estado actual como X anota toda la fila como inalcanzable. Elegi el tipo de "
           "flip-flop de cada bit arriba antes de generar mapas o circuito."),
        this);
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    auto* actionsRow = new QHBoxLayout();
    generateMapsButton_ = new QPushButton(tr("Generar mapas de excitacion"), this);
    generateMapsButton_->setToolTip(
        tr("Crea un mapa de Karnaugh con las columnas de excitacion (J/K, S/R, T o D segun el tipo elegido por "
           "bit) -- para inspeccionar o animar el procedimiento de cada una antes de generar el circuito."));
    connect(generateMapsButton_, &QPushButton::clicked, this, &ExcitationTableView::onGenerateMapsClicked);
    actionsRow->addWidget(generateMapsButton_);

    generateCircuitButton_ = new QPushButton(tr("Generar circuito"), this);
    generateCircuitButton_->setToolTip(
        tr("Sintetiza solo la logica de excitacion ya minimizada (entradas de estado actual, salidas J/K, S/R, T o "
           "D) -- los flip-flops los colocas y cableas vos."));
    connect(generateCircuitButton_, &QPushButton::clicked, this, &ExcitationTableView::onGenerateCircuitClicked);
    actionsRow->addWidget(generateCircuitButton_);

    generateCircuitWithFlipFlopsButton_ = new QPushButton(tr("Generar circuito con flip-flops"), this);
    generateCircuitWithFlipFlopsButton_->setToolTip(
        tr("Coloca un flip-flop de verdad por bit (del tipo elegido arriba) con la logica de excitacion ya "
           "cableada a su entrada y un reloj compartido -- listo para simular. SR no soportado: no hay componente "
           "de flip-flop SR con reloj en la biblioteca."));
    connect(generateCircuitWithFlipFlopsButton_, &QPushButton::clicked, this,
            &ExcitationTableView::onGenerateCircuitWithFlipFlopsClicked);
    actionsRow->addWidget(generateCircuitWithFlipFlopsButton_);
    layout->addLayout(actionsRow);

    connect(document_, &ExcitationTableDocument::structureChanged, this, &ExcitationTableView::onStructureChanged);
    connect(document_, &ExcitationTableDocument::presentStateChanged, this,
            &ExcitationTableView::onPresentStateChanged);
    connect(document_, &ExcitationTableDocument::nextStateChanged, this, &ExcitationTableView::onNextStateChanged);

    rebuildTable();
}

std::vector<QString> ExcitationTableView::currentStateBitNames() const {
    std::vector<QString> names;
    names.reserve(static_cast<std::size_t>(document_->stateBitCount()));
    for (int i = 0; i < document_->stateBitCount(); ++i) {
        names.push_back(document_->stateBitName(i));
    }
    return names;
}

void ExcitationTableView::rebuildFlipFlopTypeCombos() {
    QLayoutItem* item = nullptr;
    while ((item = flipFlopTypeLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    flipFlopTypeCombos_.clear();

    const int stateBitCount = document_->stateBitCount();
    for (int i = 0; i < stateBitCount; ++i) {
        flipFlopTypeLayout_->addWidget(new QLabel(document_->stateBitName(i) + tr(":"), this));
        auto* combo = new QComboBox(this);
        combo->addItems({"D", "T", "JK", "SR"});
        combo->setCurrentIndex(static_cast<int>(document_->flipFlopType(i)));
        // `i` se captura por valor: es el indice de ESTE combo dentro del
        // documento, no cambia aunque mas tarde se reconstruyan todos.
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this, i](int index) { document_->setFlipFlopType(i, static_cast<FlipFlopType>(index)); });
        flipFlopTypeLayout_->addWidget(combo);
        flipFlopTypeCombos_.push_back(combo);
    }
    flipFlopTypeLayout_->addStretch();
}

void ExcitationTableView::rebuildTable() {
    const int stateBitCount = document_->stateBitCount();
    const int rowCount = 1 << stateBitCount;

    const QSignalBlocker blocker(table_);
    table_->clear();
    table_->setRowCount(rowCount);
    table_->setColumnCount(stateBitCount * 2);

    QStringList headers;
    for (int i = 0; i < stateBitCount; ++i) {
        headers << document_->stateBitName(i);
    }
    for (int i = 0; i < stateBitCount; ++i) {
        headers << document_->stateBitName(i) + QStringLiteral("+");
    }
    table_->setHorizontalHeaderLabels(headers);

    for (int row = 0; row < rowCount; ++row) {
        populateRow(row);
    }
    table_->resizeColumnsToContents();
}

void ExcitationTableView::populateRow(int row) {
    const int stateBitCount = document_->stateBitCount();
    const int minterm = reverseBits(row, stateBitCount);
    for (int i = 0; i < stateBitCount; ++i) {
        table_->setItem(row, i,
                         makeItem(charForCellValue(document_->presentState(i, minterm)), /*isPresentStateColumn=*/true));
    }
    for (int i = 0; i < stateBitCount; ++i) {
        table_->setItem(
            row, stateBitCount + i,
            makeItem(charForCellValue(document_->nextState(i, minterm)), /*isPresentStateColumn=*/false));
    }
}

void ExcitationTableView::onCellClicked(int row, int column) {
    const int stateBitCount = document_->stateBitCount();
    const int minterm = reverseBits(row, stateBitCount);
    if (column < stateBitCount) {
        const int bitIndex = column;
        document_->setPresentState(bitIndex, minterm, nextCellValue(document_->presentState(bitIndex, minterm)));
        return;
    }
    const int bitIndex = column - stateBitCount;
    document_->setNextState(bitIndex, minterm, nextCellValue(document_->nextState(bitIndex, minterm)));
}

void ExcitationTableView::onHeaderDoubleClicked(int section) {
    const int stateBitCount = document_->stateBitCount();
    const int bitIndex = section < stateBitCount ? section : section - stateBitCount;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Renombrar bit de estado"), tr("Nombre:"), QLineEdit::Normal,
                                                 document_->stateBitName(bitIndex), &ok);
    if (ok && !name.isEmpty()) {
        document_->setStateBitName(bitIndex, name);
    }
}

void ExcitationTableView::onStateBitCountChanged(int index) {
    const int newCount = index + 2;
    if (newCount == document_->stateBitCount()) {
        return;
    }
    document_->setStateBitCount(newCount);
}

void ExcitationTableView::onPresentStateChanged(int bitIndex, int minterm) {
    const int stateBitCount = document_->stateBitCount();
    const int row = reverseBits(minterm, stateBitCount); // reverseBits es su propia inversa
    QTableWidgetItem* item = table_->item(row, bitIndex);
    if (item != nullptr) {
        item->setText(charForCellValue(document_->presentState(bitIndex, minterm)));
    }
}

void ExcitationTableView::onNextStateChanged(int bitIndex, int minterm) {
    const int stateBitCount = document_->stateBitCount();
    const int row = reverseBits(minterm, stateBitCount); // reverseBits es su propia inversa
    QTableWidgetItem* item = table_->item(row, stateBitCount + bitIndex);
    if (item != nullptr) {
        item->setText(charForCellValue(document_->nextState(bitIndex, minterm)));
    }
}

void ExcitationTableView::onStructureChanged() {
    // Diferido: puede dispararse de forma sincrona desde dentro del propio
    // manejador de senal de uno de flipFlopTypeCombos_ (currentIndexChanged
    // -> document_->setFlipFlopType() -> structureChanged()) --
    // rebuildFlipFlopTypeCombos() destruye ese mismo combo mientras aun esta
    // desenrollando su propia emision de senal. Reconstruir en la siguiente
    // iteracion del bucle de eventos evita el use-after-free (mismo patron
    // que ui::PropertyInspector::setSelectedComponents/onPropertyChanged).
    QTimer::singleShot(0, this, [this] {
        if (stateBitCountCombo_->currentIndex() != document_->stateBitCount() - 2) {
            const QSignalBlocker blocker(stateBitCountCombo_);
            stateBitCountCombo_->setCurrentIndex(document_->stateBitCount() - 2);
        }
        rebuildFlipFlopTypeCombos();
        rebuildTable();
    });
}

void ExcitationTableView::onGenerateMapsClicked() {
    try {
        const std::vector<QString> names = currentStateBitNames();
        const int stateBitCount = document_->stateBitCount();
        const std::size_t cellCount = std::size_t{1} << stateBitCount;

        // Un solo KarnaughDocument con una salida por columna de excitacion
        // (1 por bit D/T, 2 por bit JK/SR) -- mismo mecanismo exacto que
        // ui::TruthTableView::onGenerateMapsClicked.
        const QString suggested = project_->suggestUniqueDocumentName(
            project_->excitationTableDocumentName(documentId_) + tr(" (mapas)"));
        const uint32_t newId = project_->addKarnaughDocument(suggested, stateBitCount);
        editor::KarnaughDocument* karnaughDoc = project_->karnaughDocument(newId);
        for (int i = 0; i < stateBitCount; ++i) {
            karnaughDoc->setVariableName(i, names[static_cast<std::size_t>(i)]);
        }

        bool firstOutput = true;
        for (int bit = 0; bit < stateBitCount; ++bit) {
            std::vector<KarnaughCellValue> presentState(cellCount);
            std::vector<KarnaughCellValue> nextState(cellCount);
            for (std::size_t m = 0; m < cellCount; ++m) {
                presentState[m] = document_->presentState(bit, static_cast<int>(m));
                nextState[m] = document_->nextState(bit, static_cast<int>(m));
            }
            const std::vector<ExcitationColumn> columns = editor::computeExcitation(
                document_->flipFlopType(bit), names[static_cast<std::size_t>(bit)], presentState, nextState);
            for (const ExcitationColumn& column : columns) {
                const int outputIndex = firstOutput ? 0 : karnaughDoc->addOutput();
                firstOutput = false;
                karnaughDoc->setOutputName(outputIndex, column.name);
                for (std::size_t m = 0; m < column.values.size(); ++m) {
                    karnaughDoc->setCellValue(outputIndex, static_cast<int>(m), column.values[m]);
                }
            }
        }
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se pudieron generar los mapas"), QString::fromUtf8(error.what()));
    }
}

void ExcitationTableView::onGenerateCircuitClicked() {
    try {
        const std::vector<QString> names = currentStateBitNames();
        const int stateBitCount = document_->stateBitCount();
        const std::size_t cellCount = std::size_t{1} << stateBitCount;

        std::vector<OutputSpec> outputs;
        for (int bit = 0; bit < stateBitCount; ++bit) {
            std::vector<KarnaughCellValue> presentState(cellCount);
            std::vector<KarnaughCellValue> nextState(cellCount);
            for (std::size_t m = 0; m < cellCount; ++m) {
                presentState[m] = document_->presentState(bit, static_cast<int>(m));
                nextState[m] = document_->nextState(bit, static_cast<int>(m));
            }
            const std::vector<ExcitationColumn> columns = editor::computeExcitation(
                document_->flipFlopType(bit), names[static_cast<std::size_t>(bit)], presentState, nextState);
            for (const ExcitationColumn& column : columns) {
                OutputSpec spec;
                spec.name = column.name;
                spec.result = editor::minimize(stateBitCount, names, column.values);
                outputs.push_back(std::move(spec));
            }
        }

        const QString suggested = project_->suggestUniqueDocumentName(
            project_->excitationTableDocumentName(documentId_) + tr(" (circuito)"));
        const uint32_t newId = project_->addDocument(suggested);
        formats::synthesizeMultiOutputToCircuit(*project_->document(newId), stateBitCount, names, outputs);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se pudo generar el circuito"), QString::fromUtf8(error.what()));
    }
}

void ExcitationTableView::onGenerateCircuitWithFlipFlopsClicked() {
    try {
        const std::vector<QString> names = currentStateBitNames();
        const int stateBitCount = document_->stateBitCount();
        const std::size_t cellCount = std::size_t{1} << stateBitCount;

        std::vector<formats::FlipFlopSpec> specs;
        specs.reserve(static_cast<std::size_t>(stateBitCount));
        for (int bit = 0; bit < stateBitCount; ++bit) {
            std::vector<KarnaughCellValue> presentState(cellCount);
            std::vector<KarnaughCellValue> nextState(cellCount);
            for (std::size_t m = 0; m < cellCount; ++m) {
                presentState[m] = document_->presentState(bit, static_cast<int>(m));
                nextState[m] = document_->nextState(bit, static_cast<int>(m));
            }
            const FlipFlopType type = document_->flipFlopType(bit);
            const std::vector<ExcitationColumn> columns =
                editor::computeExcitation(type, names[static_cast<std::size_t>(bit)], presentState, nextState);

            formats::FlipFlopSpec spec;
            spec.bitName = names[static_cast<std::size_t>(bit)];
            spec.type = type;
            spec.excitationResults.reserve(columns.size());
            for (const ExcitationColumn& column : columns) {
                spec.excitationResults.push_back(editor::minimize(stateBitCount, names, column.values));
            }
            specs.push_back(std::move(spec));
        }

        const QString suggested = project_->suggestUniqueDocumentName(
            project_->excitationTableDocumentName(documentId_) + tr(" (circuito con flip-flops)"));
        const uint32_t newId = project_->addDocument(suggested);
        formats::synthesizeSequentialCircuit(*project_->document(newId), specs);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se pudo generar el circuito"), QString::fromUtf8(error.what()));
    }
}

} // namespace digitalforge::ui
