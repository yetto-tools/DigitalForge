#include "TruthTableDocument.hpp"

#include <stdexcept>

namespace digitalforge::editor {

namespace {

void validateVariableCount(int variableCount) {
    if (variableCount < 2 || variableCount > 4) {
        throw std::invalid_argument("TruthTableDocument: variableCount debe estar entre 2 y 4");
    }
}

QString defaultVariableName(int index) {
    static const QString names[] = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")};
    return names[index];
}

} // namespace

TruthTableDocument::TruthTableDocument(QObject* parent) : QObject(parent) {
    reset(variableCount_);
    // Mismo criterio que KarnaughDocument: el estado inicial de un
    // documento recien creado no es "sin guardar".
    dirty_ = false;
}

void TruthTableDocument::reset(int variableCount) {
    validateVariableCount(variableCount);
    variableCount_ = variableCount;
    variableNames_.clear();
    for (int i = 0; i < variableCount; ++i) {
        variableNames_.push_back(defaultVariableName(i));
    }
    outputs_.clear();
    outputs_.push_back(Output{QStringLiteral("F1"), std::vector<KarnaughCellValue>(std::size_t{1} << variableCount,
                                                                                     KarnaughCellValue::Zero)});
    dirty_ = true;
    emit variablesChanged();
    emit outputsChanged();
}

void TruthTableDocument::setVariableCount(int count) {
    validateVariableCount(count);
    if (count == variableCount_) {
        return;
    }
    while (static_cast<int>(variableNames_.size()) < count) {
        variableNames_.push_back(defaultVariableName(static_cast<int>(variableNames_.size())));
    }
    variableNames_.resize(static_cast<std::size_t>(count));
    // Los bits bajos de un minterm siguen significando lo mismo (bit i =
    // variable i) sin importar cuantas variables mas se agreguen o quiten
    // arriba -- mismo criterio que KarnaughDocument::setVariableCount,
    // aplicado a cada columna de salida por separado.
    for (Output& output : outputs_) {
        output.cells.resize(std::size_t{1} << count, KarnaughCellValue::Zero);
    }
    variableCount_ = count;
    dirty_ = true;
    emit variablesChanged();
}

QString TruthTableDocument::variableName(int index) const {
    if (index < 0 || index >= variableCount_) {
        throw std::invalid_argument("TruthTableDocument::variableName: index fuera de rango");
    }
    return variableNames_[static_cast<std::size_t>(index)];
}

void TruthTableDocument::setVariableName(int index, const QString& name) {
    if (index < 0 || index >= variableCount_) {
        throw std::invalid_argument("TruthTableDocument::setVariableName: index fuera de rango");
    }
    variableNames_[static_cast<std::size_t>(index)] = name;
    dirty_ = true;
    emit variablesChanged();
}

const TruthTableDocument::Output& TruthTableDocument::outputAt(int index) const {
    if (index < 0 || index >= outputCount()) {
        throw std::invalid_argument("TruthTableDocument: output index fuera de rango");
    }
    return outputs_[static_cast<std::size_t>(index)];
}

TruthTableDocument::Output& TruthTableDocument::outputAt(int index) {
    if (index < 0 || index >= outputCount()) {
        throw std::invalid_argument("TruthTableDocument: output index fuera de rango");
    }
    return outputs_[static_cast<std::size_t>(index)];
}

int TruthTableDocument::addOutput(const QString& name) {
    const QString finalName = name.isEmpty() ? QStringLiteral("F%1").arg(outputCount() + 1) : name;
    outputs_.push_back(
        Output{finalName, std::vector<KarnaughCellValue>(std::size_t{1} << variableCount_, KarnaughCellValue::Zero)});
    dirty_ = true;
    emit outputsChanged();
    return outputCount() - 1;
}

void TruthTableDocument::removeOutput(int index) {
    if (index < 0 || index >= outputCount()) {
        throw std::invalid_argument("TruthTableDocument::removeOutput: index fuera de rango");
    }
    if (outputCount() <= 1) {
        throw std::invalid_argument("TruthTableDocument::removeOutput: debe quedar al menos una salida");
    }
    outputs_.erase(outputs_.begin() + index);
    dirty_ = true;
    emit outputsChanged();
}

QString TruthTableDocument::outputName(int index) const { return outputAt(index).name; }

void TruthTableDocument::setOutputName(int index, const QString& name) {
    outputAt(index).name = name;
    dirty_ = true;
    emit outputsChanged();
}

KarnaughCellValue TruthTableDocument::cellValue(int outputIndex, int minterm) const {
    const Output& output = outputAt(outputIndex);
    if (minterm < 0 || minterm >= static_cast<int>(output.cells.size())) {
        throw std::invalid_argument("TruthTableDocument::cellValue: minterm fuera de rango");
    }
    return output.cells[static_cast<std::size_t>(minterm)];
}

void TruthTableDocument::setCellValue(int outputIndex, int minterm, KarnaughCellValue value) {
    Output& output = outputAt(outputIndex);
    if (minterm < 0 || minterm >= static_cast<int>(output.cells.size())) {
        throw std::invalid_argument("TruthTableDocument::setCellValue: minterm fuera de rango");
    }
    output.cells[static_cast<std::size_t>(minterm)] = value;
    dirty_ = true;
    emit cellChanged(outputIndex, minterm);
}

const std::vector<KarnaughCellValue>& TruthTableDocument::outputCells(int outputIndex) const {
    return outputAt(outputIndex).cells;
}

} // namespace digitalforge::editor
