#include "ExcitationTableDocument.hpp"

#include <stdexcept>

namespace digitalforge::editor {

namespace {

void validateStateBitCount(int stateBitCount) {
    if (stateBitCount < 2 || stateBitCount > 4) {
        throw std::invalid_argument("ExcitationTableDocument: stateBitCount debe estar entre 2 y 4");
    }
}

QString defaultStateBitName(int index) { return QStringLiteral("Q%1").arg(index); }

KarnaughCellValue naturalPresentStateBit(int bitIndex, int minterm) {
    return ((minterm >> bitIndex) & 1) != 0 ? KarnaughCellValue::One : KarnaughCellValue::Zero;
}

std::vector<KarnaughCellValue> naturalPresentStateColumn(int bitIndex, int cellCount) {
    std::vector<KarnaughCellValue> result(static_cast<std::size_t>(cellCount));
    for (int m = 0; m < cellCount; ++m) {
        result[static_cast<std::size_t>(m)] = naturalPresentStateBit(bitIndex, m);
    }
    return result;
}

} // namespace

ExcitationTableDocument::ExcitationTableDocument(QObject* parent) : QObject(parent) {
    reset(stateBitCount_);
    // Mismo criterio que KarnaughDocument/TruthTableDocument: el estado
    // inicial de un documento recien creado no es "sin guardar".
    dirty_ = false;
}

void ExcitationTableDocument::reset(int stateBitCount) {
    validateStateBitCount(stateBitCount);
    stateBitCount_ = stateBitCount;
    bits_.clear();
    bits_.reserve(static_cast<std::size_t>(stateBitCount));
    const int cellCount = 1 << stateBitCount;
    for (int i = 0; i < stateBitCount; ++i) {
        StateBit bit;
        bit.name = defaultStateBitName(i);
        bit.flipFlopType = FlipFlopType::D;
        bit.presentState = naturalPresentStateColumn(i, cellCount);
        bit.nextState.assign(static_cast<std::size_t>(cellCount), KarnaughCellValue::Zero);
        bits_.push_back(std::move(bit));
    }
    dirty_ = true;
    emit structureChanged();
}

void ExcitationTableDocument::setStateBitCount(int count) {
    validateStateBitCount(count);
    if (count == stateBitCount_) {
        return;
    }
    const int newCellCount = 1 << count;
    // Bits nuevos (si crece): arrancan iguales que en reset() (estado actual
    // natural, estado siguiente en Zero, tipo D).
    while (static_cast<int>(bits_.size()) < count) {
        const int newBitIndex = static_cast<int>(bits_.size());
        StateBit bit;
        bit.name = defaultStateBitName(newBitIndex);
        bit.flipFlopType = FlipFlopType::D;
        bit.presentState = naturalPresentStateColumn(newBitIndex, newCellCount);
        bit.nextState.assign(static_cast<std::size_t>(newCellCount), KarnaughCellValue::Zero);
        bits_.push_back(std::move(bit));
    }
    bits_.resize(static_cast<std::size_t>(count));
    // Los bits que sobreviven conservan lo que el usuario ya edito en los
    // minterms que siguen existiendo (tanto estado actual como siguiente);
    // los minterms NUEVOS de estado actual arrancan en su valor natural (no
    // en Zero, a diferencia de estado siguiente) -- son la parte "recien
    // aparecida" que todavia nadie edito.
    for (int i = 0; i < count; ++i) {
        StateBit& bit = bits_[static_cast<std::size_t>(i)];
        const std::size_t oldSize = bit.presentState.size();
        bit.presentState.resize(static_cast<std::size_t>(newCellCount));
        for (std::size_t m = oldSize; m < static_cast<std::size_t>(newCellCount); ++m) {
            bit.presentState[m] = naturalPresentStateBit(i, static_cast<int>(m));
        }
        bit.nextState.resize(static_cast<std::size_t>(newCellCount), KarnaughCellValue::Zero);
    }
    stateBitCount_ = count;
    dirty_ = true;
    emit structureChanged();
}

const ExcitationTableDocument::StateBit& ExcitationTableDocument::bitAt(int index) const {
    if (index < 0 || index >= stateBitCount_) {
        throw std::invalid_argument("ExcitationTableDocument: bitIndex fuera de rango");
    }
    return bits_[static_cast<std::size_t>(index)];
}

ExcitationTableDocument::StateBit& ExcitationTableDocument::bitAt(int index) {
    if (index < 0 || index >= stateBitCount_) {
        throw std::invalid_argument("ExcitationTableDocument: bitIndex fuera de rango");
    }
    return bits_[static_cast<std::size_t>(index)];
}

QString ExcitationTableDocument::stateBitName(int index) const { return bitAt(index).name; }

void ExcitationTableDocument::setStateBitName(int index, const QString& name) {
    bitAt(index).name = name;
    dirty_ = true;
    emit structureChanged();
}

FlipFlopType ExcitationTableDocument::flipFlopType(int index) const { return bitAt(index).flipFlopType; }

void ExcitationTableDocument::setFlipFlopType(int index, FlipFlopType type) {
    bitAt(index).flipFlopType = type;
    dirty_ = true;
    emit structureChanged();
}

KarnaughCellValue ExcitationTableDocument::presentState(int bitIndex, int minterm) const {
    const StateBit& bit = bitAt(bitIndex);
    if (minterm < 0 || minterm >= static_cast<int>(bit.presentState.size())) {
        throw std::invalid_argument("ExcitationTableDocument::presentState: minterm fuera de rango");
    }
    return bit.presentState[static_cast<std::size_t>(minterm)];
}

void ExcitationTableDocument::setPresentState(int bitIndex, int minterm, KarnaughCellValue value) {
    StateBit& bit = bitAt(bitIndex);
    if (minterm < 0 || minterm >= static_cast<int>(bit.presentState.size())) {
        throw std::invalid_argument("ExcitationTableDocument::setPresentState: minterm fuera de rango");
    }
    bit.presentState[static_cast<std::size_t>(minterm)] = value;
    dirty_ = true;
    emit presentStateChanged(bitIndex, minterm);
}

KarnaughCellValue ExcitationTableDocument::nextState(int bitIndex, int minterm) const {
    const StateBit& bit = bitAt(bitIndex);
    if (minterm < 0 || minterm >= static_cast<int>(bit.nextState.size())) {
        throw std::invalid_argument("ExcitationTableDocument::nextState: minterm fuera de rango");
    }
    return bit.nextState[static_cast<std::size_t>(minterm)];
}

void ExcitationTableDocument::setNextState(int bitIndex, int minterm, KarnaughCellValue value) {
    StateBit& bit = bitAt(bitIndex);
    if (minterm < 0 || minterm >= static_cast<int>(bit.nextState.size())) {
        throw std::invalid_argument("ExcitationTableDocument::setNextState: minterm fuera de rango");
    }
    bit.nextState[static_cast<std::size_t>(minterm)] = value;
    dirty_ = true;
    emit nextStateChanged(bitIndex, minterm);
}

} // namespace digitalforge::editor
