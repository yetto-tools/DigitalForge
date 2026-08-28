#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <vector>

#include "FlipFlopExcitation.hpp"
#include "KarnaughMap.hpp"

namespace digitalforge::editor {

// Modelo en memoria de una tabla de excitacion de flip-flops (2 a 4 bits de
// estado, mismo limite que KarnaughDocument/TruthTableDocument por el tamano
// de mapa soportado): registra, para cada combinacion de estado actual (un
// minterm, igual que en las otras dos), tanto el estado ACTUAL de cada bit
// (editable -- arranca en su valor natural segun el minterm, pero se puede
// marcar DontCare para anotar una fila inalcanzable) como el estado
// SIGUIENTE deseado, y que tipo de flip-flop (SR/JK/T/D) lo implementa -- a
// diferencia de TruthTableDocument, la cantidad de columnas de "salida"
// (nextState) es siempre igual a la cantidad de bits de estado, uno a uno, y
// no se puede agregar/quitar sueltas. ui::ExcitationTableView deriva de esto
// las columnas de excitacion reales (ver computeExcitation()) para alimentar
// el mismo formats::synthesizeMultiOutputToCircuit() que ya usan
// Karnaugh/TruthTable.
class ExcitationTableDocument : public QObject {
    Q_OBJECT

public:
    explicit ExcitationTableDocument(QObject* parent = nullptr);

    // Reinicia a `stateBitCount` bits de estado (nombres por defecto
    // "Q0".."Q3", tipo D por defecto en todos), estado actual en su valor
    // natural (bit i del minterm) y estado siguiente en Zero para toda
    // combinacion. Lanza std::invalid_argument si stateBitCount no esta en
    // [2,4].
    void reset(int stateBitCount);

    [[nodiscard]] int stateBitCount() const noexcept { return stateBitCount_; }
    // Redimensiona preservando nombres/tipo/estado actual/estado siguiente
    // existentes en la medida de lo posible -- mismo criterio que
    // TruthTableDocument::setVariableCount; los minterms NUEVOS de estado
    // actual arrancan en su valor natural (no en Zero, a diferencia de
    // estado siguiente). Lanza std::invalid_argument si count no esta en
    // [2,4].
    void setStateBitCount(int count);

    [[nodiscard]] QString stateBitName(int index) const;
    // Lanza std::invalid_argument si index esta fuera de [0, stateBitCount()).
    void setStateBitName(int index, const QString& name);

    [[nodiscard]] FlipFlopType flipFlopType(int index) const;
    void setFlipFlopType(int index, FlipFlopType type);

    // `minterm` es la combinacion de estado actual "de fabrica" (bit i del
    // minterm = valor natural de presentState(i, minterm)); el usuario puede
    // editarlo igual que nextState -- tipicamente para marcar toda la fila
    // como DontCare (estado inalcanzable) sin tener que tocar cada bit de
    // estado siguiente por separado (ver FlipFlopExcitation.hpp).
    [[nodiscard]] KarnaughCellValue presentState(int bitIndex, int minterm) const;
    void setPresentState(int bitIndex, int minterm, KarnaughCellValue value);

    // El estado SIGUIENTE deseado de `bitIndex` para la combinacion de
    // estado actual `minterm` (DontCare = no importa, p. ej. un estado del
    // que no importa a donde va).
    [[nodiscard]] KarnaughCellValue nextState(int bitIndex, int minterm) const;
    void setNextState(int bitIndex, int minterm, KarnaughCellValue value);

    // true si hubo algun cambio (bits, nombres, tipos, estado actual o
    // estado siguiente) desde el ultimo markClean() -- mismo criterio que
    // KarnaughDocument::dirty()/TruthTableDocument::dirty().
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void markClean() noexcept { dirty_ = false; }

signals:
    // Cambio estructural: stateBitCount, un nombre de bit, o el tipo de
    // flip-flop asignado -- cualquier cosa que invalide una vista ya armada
    // y obligue a reconstruirla entera.
    void structureChanged();
    void presentStateChanged(int bitIndex, int minterm);
    void nextStateChanged(int bitIndex, int minterm);

private:
    struct StateBit {
        QString name;
        FlipFlopType flipFlopType = FlipFlopType::D;
        std::vector<KarnaughCellValue> presentState;
        std::vector<KarnaughCellValue> nextState;
    };

    [[nodiscard]] const StateBit& bitAt(int index) const;
    [[nodiscard]] StateBit& bitAt(int index);

    int stateBitCount_ = 2;
    std::vector<StateBit> bits_;
    bool dirty_ = false;
};

} // namespace digitalforge::editor
