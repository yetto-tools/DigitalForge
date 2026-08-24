#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace digitalforge::editor {

// Valor de una celda del mapa de Karnaugh (o de un minterm dentro de la
// tabla de verdad que lo alimenta). DontCare participa de la formacion de
// grupos en minimize() pero nunca justifica por si solo un termino final
// (ver seleccion de implicantes esenciales/Petrick mas abajo) - refleja
// que "no importa" no es lo mismo que "esta encendido".
enum class KarnaughCellValue : uint8_t { Zero, One, DontCare };

// Un literal de una expresion booleana: la variable `variableIndex` (0 =
// primera de KarnaughResult::variableNames), negada o no.
struct KarnaughLiteral {
    int variableIndex = 0;
    bool negated = false;
};

// Un implicante de Quine-McCluskey: un conjunto de minterms adyacentes que
// difieren solo en las posiciones marcadas por `dashMask` (bit i en 1 =
// esa variable ya se elimino, aparece como "-" en la notacion clasica).
// `bits` guarda el valor compartido de las variables NO eliminadas (los
// bits en dashMask son irrelevantes en `bits`, siempre 0 ahi).
struct Implicant {
    uint32_t bits = 0;
    uint32_t dashMask = 0;
    std::vector<int> minterms; // ordenados, sin repetidos

    // Los literales de este implicante (una entrada por cada variable NO
    // eliminada), en orden ascendente de variableIndex - listo para
    // formatear o para sintetizar un cable por literal (ver
    // formats::synthesizeToCircuit). Vacio si dashMask cubre las
    // `variableCount` variables enteras (el implicante "siempre 1").
    [[nodiscard]] std::vector<KarnaughLiteral> literals(int variableCount) const;

    [[nodiscard]] bool operator==(const Implicant& other) const noexcept {
        return bits == other.bits && dashMask == other.dashMask;
    }
};

// Que clase de paso del algoritmo de Quine-McCluskey + Petrick representa
// un KarnaughStep - ver KarnaughResult::steps, pensado para que
// ui::KarnaughMapView anime uno a la vez.
enum class KarnaughStepKind {
    CombinePair,     // dos implicantes adyacentes se combinaron en uno mas grande
    MarkPrime,       // un implicante nunca se combino con otro: es primo
    SelectEssential, // un implicante primo es el UNICO que cubre algun minterm en '1'
    SelectViaPetrick,// cobertura de los minterms restantes via el metodo de Petrick
    FinalTerm,       // un implicante entra a la cobertura minima final (la suma de productos)
};

// Un paso individual del procedimiento, en el orden en que ocurrio -
// pensado para animarse uno a la vez sobre la grilla (ver
// ui::KarnaughMapView): `involvedMinterms` son las celdas a resaltar,
// `description` ya viene en espaniol lista para mostrar.
struct KarnaughStep {
    KarnaughStepKind kind = KarnaughStepKind::CombinePair;
    QString description;
    std::vector<int> involvedMinterms;
    std::optional<Implicant> implicant;
    int groupIndex = -1; // indice en KarnaughResult::selectedGroups, cuando ya se conoce
};

// Resultado completo de minimizar un mapa de Karnaugh de `variableCount`
// variables (2 a 4) - ver minimize().
struct KarnaughResult {
    int variableCount = 0;
    std::vector<QString> variableNames;
    std::vector<Implicant> primeImplicants;
    std::vector<Implicant> essentialPrimeImplicants;
    std::vector<Implicant> selectedGroups; // cobertura minima final
    std::vector<KarnaughStep> steps;         // en orden, listo para animar
    QString sopExpression;                    // "A·B' + C'·D" (mismo formato que TruthTable.cpp::formatMinterm)
};

// Minimiza la funcion booleana descrita por `cellsByMinterm` (tamano
// 2^variableCount, indexado por NUMERO de minterm, no por posicion en la
// grilla Gray-code - ver gridDimensions()/mintermAt() para esa conversion,
// que es puramente de presentacion) usando Quine-McCluskey para hallar los
// implicantes primos y el metodo de Petrick para cubrir cualquier minterm
// que ningun primo esencial cubra por si solo.
//
// Lanza std::invalid_argument si variableCount no esta en [2,4],
// `variableNames.size() != variableCount`, o `cellsByMinterm.size() !=
// 2^variableCount`.
[[nodiscard]] KarnaughResult minimize(int variableCount, const std::vector<QString>& variableNames,
                                       const std::vector<KarnaughCellValue>& cellsByMinterm);

// Dimensiones {filas, columnas} de la grilla 2D convencional para
// `variableCount` variables: 2->{2,2}, 3->{2,4}, 4->{4,4} (las primeras
// ceil(variableCount/2) variables parten las filas, el resto las
// columnas). Lanza std::invalid_argument si variableCount no esta en
// [2,4].
[[nodiscard]] std::pair<int, int> gridDimensions(int variableCount);

// Numero de minterm que le corresponde a la celda (row, col) de la grilla
// Gray-code de `variableCount` variables (ver gridDimensions()). Lanza
// std::invalid_argument si variableCount no esta en [2,4] o si row/col
// caen fuera de gridDimensions().
[[nodiscard]] int mintermAt(int row, int col, int variableCount);

// Inversa de mintermAt(): posicion {row, col} en la grilla Gray-code que
// le corresponde al minterm `minterm`. Lanza std::invalid_argument si
// variableCount no esta en [2,4] o minterm >= 2^variableCount.
[[nodiscard]] std::pair<int, int> gridPositionOf(int minterm, int variableCount);

} // namespace digitalforge::editor
