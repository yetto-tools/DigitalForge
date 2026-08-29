#include "KarnaughDocument.hpp"

#include <stdexcept>

namespace digitalforge::editor {

namespace {

void validateVariableCount(int variableCount) {
    if (variableCount < 2 || variableCount > 4) {
        throw std::invalid_argument("KarnaughDocument: variableCount debe estar entre 2 y 4");
    }
}

QString defaultVariableName(int index) {
    static const QString names[] = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")};
    return names[index];
}

} // namespace

KarnaughDocument::KarnaughDocument(QObject* parent) : QObject(parent) {
    reset(variableCount_);
    // reset() marca dirty_ (es una edicion real cuando se llama despues de
    // construido) -- pero el estado inicial de un documento recien creado
    // no es "sin guardar" (mismo criterio que CircuitDocument/QUndoStack,
    // que arrancan limpios), asi que se revierte aca.
    dirty_ = false;
}

void KarnaughDocument::reset(int variableCount) {
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

void KarnaughDocument::setVariableCount(int count) {
    validateVariableCount(count);
    if (count == variableCount_) {
        return;
    }
    // Los bits bajos de un minterm siguen significando lo mismo (bit i =
    // variable i) sin importar cuantas variables mas se agreguen o quiten
    // arriba - por eso alcanza con truncar/extender cada Output::cells
    // directamente: ningun minterm que sobrevive cambia de significado.
    while (static_cast<int>(variableNames_.size()) < count) {
        variableNames_.push_back(defaultVariableName(static_cast<int>(variableNames_.size())));
    }
    variableNames_.resize(static_cast<std::size_t>(count));
    for (Output& output : outputs_) {
        output.cells.resize(std::size_t{1} << count, KarnaughCellValue::Zero);
    }
    variableCount_ = count;
    dirty_ = true;
    emit variablesChanged();
}

QString KarnaughDocument::variableName(int index) const {
    if (index < 0 || index >= variableCount_) {
        throw std::invalid_argument("KarnaughDocument::variableName: index fuera de rango");
    }
    return variableNames_[static_cast<std::size_t>(index)];
}

void KarnaughDocument::setVariableName(int index, const QString& name) {
    if (index < 0 || index >= variableCount_) {
        throw std::invalid_argument("KarnaughDocument::setVariableName: index fuera de rango");
    }
    variableNames_[static_cast<std::size_t>(index)] = name;
    dirty_ = true;
    emit variablesChanged();
}

const KarnaughDocument::Output& KarnaughDocument::outputAt(int index) const {
    if (index < 0 || index >= outputCount()) {
        throw std::invalid_argument("KarnaughDocument: output index fuera de rango");
    }
    return outputs_[static_cast<std::size_t>(index)];
}

KarnaughDocument::Output& KarnaughDocument::outputAt(int index) {
    if (index < 0 || index >= outputCount()) {
        throw std::invalid_argument("KarnaughDocument: output index fuera de rango");
    }
    return outputs_[static_cast<std::size_t>(index)];
}

int KarnaughDocument::addOutput(const QString& name) {
    const QString finalName = name.isEmpty() ? QStringLiteral("F%1").arg(outputCount() + 1) : name;
    outputs_.push_back(
        Output{finalName, std::vector<KarnaughCellValue>(std::size_t{1} << variableCount_, KarnaughCellValue::Zero)});
    dirty_ = true;
    emit outputsChanged();
    return outputCount() - 1;
}

void KarnaughDocument::removeOutput(int index) {
    if (index < 0 || index >= outputCount()) {
        throw std::invalid_argument("KarnaughDocument::removeOutput: index fuera de rango");
    }
    if (outputCount() <= 1) {
        throw std::invalid_argument("KarnaughDocument::removeOutput: debe quedar al menos una funcion");
    }
    outputs_.erase(outputs_.begin() + index);
    dirty_ = true;
    emit outputsChanged();
}

QString KarnaughDocument::outputName(int index) const { return outputAt(index).name; }

void KarnaughDocument::setOutputName(int index, const QString& name) {
    outputAt(index).name = name;
    dirty_ = true;
    emit outputsChanged();
}

KarnaughCellValue KarnaughDocument::cellValue(int outputIndex, int minterm) const {
    const Output& output = outputAt(outputIndex);
    if (minterm < 0 || minterm >= static_cast<int>(output.cells.size())) {
        throw std::invalid_argument("KarnaughDocument::cellValue: minterm fuera de rango");
    }
    return output.cells[static_cast<std::size_t>(minterm)];
}

void KarnaughDocument::setCellValue(int outputIndex, int minterm, KarnaughCellValue value) {
    Output& output = outputAt(outputIndex);
    if (minterm < 0 || minterm >= static_cast<int>(output.cells.size())) {
        throw std::invalid_argument("KarnaughDocument::setCellValue: minterm fuera de rango");
    }
    output.cells[static_cast<std::size_t>(minterm)] = value;
    dirty_ = true;
    emit cellChanged(outputIndex, minterm);
}

const std::vector<KarnaughCellValue>& KarnaughDocument::outputCells(int outputIndex) const {
    return outputAt(outputIndex).cells;
}

} // namespace digitalforge::editor
