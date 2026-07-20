#include "TruthTablePanel.hpp"

#include <QAbstractItemView>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

#include "editor/CircuitDocument.hpp"
#include "editor/TruthTable.hpp"

namespace digitalforge::ui {

using editor::CircuitDocument;

TruthTablePanel::TruthTablePanel(editor::CircuitDocument* document, QWidget* parent)
    : QWidget(parent), document_(nullptr) {
    auto* layout = new QVBoxLayout(this);

    statusLabel_ = new QLabel(tr("Presione Generar para calcular la tabla de verdad."), this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    generateButton_ = new QPushButton(tr("Generar"), this);
    connect(generateButton_, &QPushButton::clicked, this, &TruthTablePanel::onGenerateClicked);

    exportButton_ = new QPushButton(tr("Exportar..."), this);
    exportButton_->setEnabled(false);
    connect(exportButton_, &QPushButton::clicked, this, &TruthTablePanel::onExportClicked);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(generateButton_);
    buttonRow->addWidget(exportButton_);
    layout->addLayout(buttonRow);

    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table_);

    formulaLabel_ = new QLabel(this);
    formulaLabel_->setWordWrap(true);
    formulaLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formulaLabel_->setVisible(false);
    layout->addWidget(formulaLabel_);

    setDocument(document);
}

void TruthTablePanel::setDocument(editor::CircuitDocument* document) {
    disconnect(componentAddedConnection_);
    disconnect(componentAboutToBeRemovedConnection_);
    disconnect(propertyChangedConnection_);
    document_ = document;
    componentAddedConnection_ =
        connect(document_, &CircuitDocument::componentAdded, this, &TruthTablePanel::invalidate);
    componentAboutToBeRemovedConnection_ =
        connect(document_, &CircuitDocument::componentAboutToBeRemoved, this, &TruthTablePanel::invalidate);
    propertyChangedConnection_ =
        connect(document_, &CircuitDocument::propertyChanged, this, &TruthTablePanel::invalidate);
    invalidate();
}

void TruthTablePanel::invalidate() {
    table_->clear();
    table_->setRowCount(0);
    table_->setColumnCount(0);
    formulaLabel_->clear();
    formulaLabel_->setVisible(false);
    exportButton_->setEnabled(false);
    statusLabel_->setText(tr("El circuito cambio - presione Generar para actualizar la tabla."));
}

void TruthTablePanel::onGenerateClicked() { generate(); }

void TruthTablePanel::generate() {
    editor::TruthTable result;
    try {
        result = editor::computeTruthTable(*document_);
    } catch (const std::invalid_argument& error) {
        exportButton_->setEnabled(false);
        statusLabel_->setText(QString::fromUtf8(error.what()));
        return;
    }

    table_->clear();
    const int inputCount = static_cast<int>(result.inputHeaders.size());
    const int outputCount = static_cast<int>(result.outputHeaders.size());
    table_->setColumnCount(inputCount + outputCount);
    table_->setRowCount(static_cast<int>(result.rows.size()));

    QStringList headers;
    for (const QString& header : result.inputHeaders) {
        headers << header;
    }
    for (const QString& header : result.outputHeaders) {
        headers << header;
    }
    table_->setHorizontalHeaderLabels(headers);

    for (int row = 0; row < static_cast<int>(result.rows.size()); ++row) {
        const editor::TruthTableRow& tableRow = result.rows[static_cast<std::size_t>(row)];
        for (int column = 0; column < static_cast<int>(tableRow.values.size()); ++column) {
            table_->setItem(row, column, new QTableWidgetItem(QString(QChar(tableRow.values[static_cast<std::size_t>(column)]))));
        }
    }

    table_->resizeColumnsToContents();

    QStringList formulaLines;
    for (int i = 0; i < outputCount; ++i) {
        const editor::TruthTableFormula& formula = result.outputFormulas[static_cast<std::size_t>(i)];
        QString line = QStringLiteral("%1 = %2").arg(result.outputHeaders[static_cast<std::size_t>(i)], formula.expression);
        if (!formula.complete) {
            line += tr(" (incompleta: hay combinaciones con valor indefinido)");
        }
        formulaLines << line;
    }
    formulaLabel_->setText(formulaLines.join('\n'));
    formulaLabel_->setVisible(true);

    exportButton_->setEnabled(true);
    statusLabel_->setText(tr("Tabla generada: %1 entradas, %2 salidas, %3 combinaciones.")
                               .arg(inputCount)
                               .arg(outputCount)
                               .arg(result.rows.size()));
}

void TruthTablePanel::onExportClicked() {
    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar tabla de verdad"), QString(),
        tr("CSV (*.csv);;Texto plano (*.txt);;Markdown (*.md)"), &selectedFilter);
    if (path.isEmpty()) {
        return;
    }

    QString extension = QFileInfo(path).suffix().toLower();
    if (extension.isEmpty()) {
        // El usuario no puso extension - se infiere del filtro elegido en
        // el dialogo (CSV por defecto si tampoco eso ayuda).
        if (selectedFilter.contains(QStringLiteral("*.txt"))) {
            extension = QStringLiteral("txt");
        } else if (selectedFilter.contains(QStringLiteral("*.md"))) {
            extension = QStringLiteral("md");
        } else {
            extension = QStringLiteral("csv");
        }
    }
    const QString finalPath = QFileInfo(path).suffix().isEmpty() ? path + "." + extension : path;

    QString content;
    if (extension == QStringLiteral("txt")) {
        content = toPlainText();
    } else if (extension == QStringLiteral("md")) {
        content = toMarkdown();
    } else {
        content = toCsv();
    }

    QFile file(finalPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error al exportar"), tr("No se pudo escribir el archivo."));
        return;
    }
    file.write(content.toUtf8());
}

namespace {

// Cabeceras + filas de `table_`, tal como estan mostradas ahora mismo (no
// vuelve a calcular la tabla) - separado a una funcion libre para que los
// tres formatos de exportacion compartan la misma lectura.
struct TableContents {
    QStringList headers;
    std::vector<QStringList> rows;
};

TableContents readTable(const QTableWidget& table) {
    TableContents contents;
    for (int column = 0; column < table.columnCount(); ++column) {
        const QTableWidgetItem* headerItem = table.horizontalHeaderItem(column);
        contents.headers << (headerItem != nullptr ? headerItem->text() : QString());
    }
    for (int row = 0; row < table.rowCount(); ++row) {
        QStringList rowValues;
        for (int column = 0; column < table.columnCount(); ++column) {
            const QTableWidgetItem* item = table.item(row, column);
            rowValues << (item != nullptr ? item->text() : QString());
        }
        contents.rows.push_back(rowValues);
    }
    return contents;
}

// Escapeo minimo de CSV (RFC 4180): entre comillas si el campo contiene la
// propia coma, comillas o un salto de linea, duplicando cualquier comilla
// interna.
QString csvField(const QString& field) {
    if (!field.contains(',') && !field.contains('"') && !field.contains('\n')) {
        return field;
    }
    QString escaped = field;
    escaped.replace("\"", "\"\"");
    return "\"" + escaped + "\"";
}

} // namespace

QString TruthTablePanel::toCsv() const {
    const TableContents contents = readTable(*table_);
    QStringList lines;
    QStringList headerFields;
    for (const QString& header : contents.headers) {
        headerFields << csvField(header);
    }
    lines << headerFields.join(',');
    for (const QStringList& row : contents.rows) {
        QStringList rowFields;
        for (const QString& value : row) {
            rowFields << csvField(value);
        }
        lines << rowFields.join(',');
    }
    return lines.join('\n') + '\n';
}

QString TruthTablePanel::toPlainText() const {
    const TableContents contents = readTable(*table_);
    // Ancho de columna = el mas largo entre el encabezado y todas sus
    // filas, para que las columnas queden alineadas en una fuente
    // monoespaciada.
    std::vector<int> widths;
    for (const QString& header : contents.headers) {
        widths.push_back(static_cast<int>(header.size()));
    }
    for (const QStringList& row : contents.rows) {
        for (int column = 0; column < row.size(); ++column) {
            widths[static_cast<std::size_t>(column)] =
                std::max(widths[static_cast<std::size_t>(column)], static_cast<int>(row[column].size()));
        }
    }

    auto formatRow = [&](const QStringList& fields) {
        QStringList padded;
        for (int column = 0; column < fields.size(); ++column) {
            padded << fields[column].leftJustified(widths[static_cast<std::size_t>(column)]);
        }
        return padded.join("  ");
    };

    QStringList lines;
    lines << formatRow(contents.headers);
    for (const QStringList& row : contents.rows) {
        lines << formatRow(row);
    }
    return lines.join('\n') + '\n';
}

QString TruthTablePanel::toMarkdown() const {
    const TableContents contents = readTable(*table_);
    QStringList lines;
    lines << "| " + contents.headers.join(" | ") + " |";
    QStringList separators;
    for (int column = 0; column < contents.headers.size(); ++column) {
        separators << "---";
    }
    lines << "| " + separators.join(" | ") + " |";
    for (const QStringList& row : contents.rows) {
        lines << "| " + row.join(" | ") + " |";
    }
    return lines.join('\n') + '\n';
}

} // namespace digitalforge::ui
