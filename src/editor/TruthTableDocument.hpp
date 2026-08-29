#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <vector>

#include "KarnaughMap.hpp"

namespace digitalforge::editor {

// Modelo en memoria de una tabla de verdad de VARIAS salidas (2 a 4
// variables de entrada, compartidas por todas las columnas de salida) --
// analogo a KarnaughDocument, del que toma el mismo esquema de celdas
// (KarnaughCellValue, indexadas por NUMERO de minterm) y las mismas
// restricciones de variableCount, pero con N columnas de salida en vez de
// una sola. Pensado para alimentar formats::synthesizeMultiOutputToCircuit()
// -- un circuito combinado con una entrada por variable y una salida por
// columna, en vez de tener que sintetizar cada columna por separado y
// unirlas a mano (que es como se armaba hasta ahora un decodificador BCD a 7
// segmentos, por ejemplo).
class TruthTableDocument : public QObject {
    Q_OBJECT

public:
    explicit TruthTableDocument(QObject* parent = nullptr);

    // Reinicia a `variableCount` variables (nombres por defecto "A".."D") y
    // UNA sola columna de salida ("F1"), todas las celdas en Zero. Lanza
    // std::invalid_argument si variableCount no esta en [2,4].
    void reset(int variableCount);

    [[nodiscard]] int variableCount() const noexcept { return variableCount_; }
    // Redimensiona a `count` variables preservando los valores de celda
    // existentes en la medida de lo posible (mismo criterio que
    // KarnaughDocument::setVariableCount, aplicado a cada columna de
    // salida). Lanza std::invalid_argument si count no esta en [2,4].
    void setVariableCount(int count);

    [[nodiscard]] QString variableName(int index) const;
    // Lanza std::invalid_argument si index esta fuera de [0, variableCount()).
    void setVariableName(int index, const QString& name);

    [[nodiscard]] int outputCount() const noexcept { return static_cast<int>(outputs_.size()); }
    // Agrega una columna de salida al final (celdas en Zero) y devuelve su
    // indice. `name` vacio sugiere "F" + (outputCount()+1).
    int addOutput(const QString& name = QString());
    // Lanza std::invalid_argument si index esta fuera de rango, o si es la
    // unica columna de salida que queda (una tabla de verdad siempre tiene
    // que tener al menos una salida que sintetizar).
    void removeOutput(int index);
    [[nodiscard]] QString outputName(int index) const;
    // Lanza std::invalid_argument si index esta fuera de [0, outputCount()).
    void setOutputName(int index, const QString& name);

    [[nodiscard]] KarnaughCellValue cellValue(int outputIndex, int minterm) const;
    // Lanza std::invalid_argument si outputIndex o minterm estan fuera de
    // rango.
    void setCellValue(int outputIndex, int minterm, KarnaughCellValue value);
    [[nodiscard]] const std::vector<KarnaughCellValue>& outputCells(int outputIndex) const;

    // true si hubo algun cambio (variables, salidas o celdas) desde el
    // ultimo markClean() -- mismo criterio que KarnaughDocument::dirty()
    // (sin QUndoStack propio, esto es lo unico que Project::hasUnsavedChanges()
    // puede consultar).
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void markClean() noexcept { dirty_ = false; }

signals:
    void variablesChanged();
    void outputsChanged(); // se agrego, quito o renombro una columna de salida
    void cellChanged(int outputIndex, int minterm);

private:
    struct Output {
        QString name;
        std::vector<KarnaughCellValue> cells;
    };

    [[nodiscard]] const Output& outputAt(int index) const;
    [[nodiscard]] Output& outputAt(int index);

    int variableCount_ = 4;
    std::vector<QString> variableNames_;
    std::vector<Output> outputs_;
    bool dirty_ = false;
};

} // namespace digitalforge::editor
