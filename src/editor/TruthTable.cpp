#include "TruthTable.hpp"

#include <stdexcept>

#include "CircuitDocument.hpp"
#include "components/ComponentInstance.hpp"
#include "core/LogicValue.hpp"

namespace digitalforge::editor {

namespace {

QString columnLabel(const components::ComponentInstance& instance, const QString& fallbackPrefix) {
    const std::string& label = std::get<std::string>(instance.property("label"));
    if (!label.empty()) {
        return QString::fromStdString(label);
    }
    return fallbackPrefix + QString::number(instance.instanceId());
}

// Un termino (minterm) de la suma de productos: cada entrada aparece
// literal si valia '1' en esa fila, negada (con ') si valia '0'. Las
// entradas siempre son '0'/'1' exactas (nunca X/Z/E): a diferencia de una
// salida, cada wiring.input se fuerza explicitamente a un valor concreto
// antes de leer la fila (ver el barrido de arriba).
QString formatMinterm(const std::vector<QString>& inputHeaders, const std::vector<char>& rowValues) {
    QStringList literals;
    for (std::size_t i = 0; i < inputHeaders.size(); ++i) {
        literals << (rowValues[i] == '1' ? inputHeaders[i] : inputHeaders[i] + QStringLiteral("'"));
    }
    return literals.join(QStringLiteral("·"));
}

// Suma de productos canonica de una columna de salida: un termino por cada
// fila donde esa salida vale '1'. Las filas con un valor indefinido (Z/X/E,
// ver el comentario de TruthTableFormula) se excluyen de la suma en vez de
// arriesgar un termino incorrecto -- `complete` queda en false si eso paso
// alguna vez, para que ui::TruthTablePanel pueda avisarlo.
TruthTableFormula computeOutputFormula(const TruthTable& table, std::size_t outputIndex) {
    const std::size_t inputCount = table.inputHeaders.size();
    const std::size_t column = inputCount + outputIndex;

    TruthTableFormula formula;
    QStringList terms;
    for (const TruthTableRow& row : table.rows) {
        const char value = row.values[column];
        if (value == '1') {
            terms << formatMinterm(table.inputHeaders, row.values);
        } else if (value != '0') {
            formula.complete = false;
        }
    }

    if (terms.isEmpty()) {
        formula.expression = QStringLiteral("0");
    } else if (static_cast<std::size_t>(terms.size()) == table.rows.size()) {
        formula.expression = QStringLiteral("1");
    } else {
        formula.expression = terms.join(QStringLiteral(" + "));
    }
    return formula;
}

} // namespace

TruthTable computeTruthTable(CircuitDocument& document, std::size_t maxInputs) {
    std::vector<uint32_t> inputIds;
    std::vector<uint32_t> outputIds;
    for (const uint32_t id : document.componentIds()) {
        const components::ComponentInstance* instance = document.component(id);
        if (instance == nullptr) {
            continue;
        }
        if (instance->typeId() == "wiring.input") {
            inputIds.push_back(id);
        } else if (instance->typeId() == "wiring.output") {
            outputIds.push_back(id);
        }
    }

    if (inputIds.empty() || outputIds.empty()) {
        throw std::invalid_argument(
            "computeTruthTable: el circuito necesita al menos una entrada (wiring.input) y una salida "
            "(wiring.output)");
    }
    if (inputIds.size() > maxInputs) {
        throw std::invalid_argument("computeTruthTable: demasiadas entradas (" + std::to_string(inputIds.size()) +
                                     ") para una tabla de verdad exhaustiva - el limite es " +
                                     std::to_string(maxInputs));
    }

    // Snapshot de los valores actuales para restaurarlos al final.
    std::vector<core::LogicValue> originalValues;
    originalValues.reserve(inputIds.size());
    for (const uint32_t id : inputIds) {
        originalValues.push_back(document.pinValue(id, 0));
    }

    TruthTable table;
    for (const uint32_t id : inputIds) {
        table.inputHeaders.push_back(columnLabel(*document.component(id), QStringLiteral("IN")));
    }
    for (const uint32_t id : outputIds) {
        table.outputHeaders.push_back(columnLabel(*document.component(id), QStringLiteral("OUT")));
    }

    const std::size_t combinationCount = std::size_t{1} << inputIds.size();
    table.rows.reserve(combinationCount);
    for (std::size_t combination = 0; combination < combinationCount; ++combination) {
        for (std::size_t bit = 0; bit < inputIds.size(); ++bit) {
            const core::LogicValue value =
                ((combination >> bit) & 1U) != 0 ? core::LogicValue::One : core::LogicValue::Zero;
            document.setInputValue(inputIds[bit], value);
        }
        document.runUntilStable();

        TruthTableRow row;
        row.values.reserve(inputIds.size() + outputIds.size());
        for (const uint32_t id : inputIds) {
            row.values.push_back(core::toChar(document.pinValue(id, 0)));
        }
        for (const uint32_t id : outputIds) {
            row.values.push_back(core::toChar(document.pinValue(id, 0)));
        }
        table.rows.push_back(std::move(row));
    }

    for (std::size_t i = 0; i < inputIds.size(); ++i) {
        document.setInputValue(inputIds[i], originalValues[i]);
    }
    document.runUntilStable();

    table.outputFormulas.reserve(outputIds.size());
    for (std::size_t outputIndex = 0; outputIndex < outputIds.size(); ++outputIndex) {
        table.outputFormulas.push_back(computeOutputFormula(table, outputIndex));
    }

    return table;
}

} // namespace digitalforge::editor
