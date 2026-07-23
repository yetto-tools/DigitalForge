#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "ComponentInstance.hpp"
#include "ComponentRegistry.hpp"
#include "core/LogicValue.hpp"

namespace digitalforge::components {

// Registra la biblioteca de componentes combinacionales basicos de la FASE
// B1: Entrada (wiring.input), Salida (wiring.output), Constante
// (wiring.constant), las siete puertas basicas
// (gates.and/or/not/nand/nor/xor/xnor), LED (io.led), Sonda logica
// (debug.probe) y el display de siete segmentos (io.seven_segment). "Cable"
// no se registra aqui intencionalmente: un cable de 1 bit no tiene logica ni
// propiedades propias en esta capa, es simplemente dos pines que comparten
// el mismo core::NetId; obtendra una representacion visual cuando la fase
// de GUI introduzca WireItem.
//
// Lanza std::invalid_argument si alguno de estos typeIds ya esta registrado
// en `registry`.
void registerBasicComponentLibrary(ComponentRegistry& registry);

// Convierte uno de "0", "1", "Z", "X" en el LogicValue correspondiente.
// Lanza std::invalid_argument para cualquier otra cadena.
[[nodiscard]] core::LogicValue parseLogicValueEnum(const std::string& text);

// Lee la propiedad "initialValue" de una instancia wiring.input como
// LogicValue, para que el llamador la aplique mediante Simulator::setInput()
// una vez que exista un Simulator (ComponentDefinition::buildSimulation solo
// tiene acceso a la topologia del Circuit, no a un Simulator en ejecucion).
// Lanza std::invalid_argument si `input` no es una instancia wiring.input.
[[nodiscard]] core::LogicValue inputInitialValue(const ComponentInstance& input);

// Indica si una instancia io.led debe representarse encendida, dado el
// valor actual del net que observa su unico pin. Respeta la propiedad
// "activeHigh": Zero enciende un LED activo en bajo, One enciende uno
// activo en alto. Unknown/HighImpedance/Error nunca cuentan como encendido.
// Lanza std::invalid_argument si `led` no es una instancia io.led.
[[nodiscard]] bool ledIsLit(const ComponentInstance& led, core::LogicValue netValue);

// Indica si un segmento de una instancia io.seven_segment debe
// representarse encendido, dado el valor actual del net que lo controla.
// Respeta la propiedad "commonAnode": Zero enciende un segmento en un
// display de anodo comun, One lo enciende en uno de catodo comun.
// Unknown/HighImpedance/Error nunca cuentan como encendido. Lanza
// std::invalid_argument si `display` no es una instancia io.seven_segment.
[[nodiscard]] bool segmentIsLit(const ComponentInstance& display, core::LogicValue netValue);

// Los 7 segmentos (a..g, en ese orden) que representan el digito hexadecimal
// `value` (0-15) en un display de 7 segmentos estandar - tabla pura, sin
// dependencia de ninguna instancia. Lanza std::invalid_argument si value > 15.
[[nodiscard]] std::array<bool, 7> hexDigitSegments(uint8_t value);

// Resultado de decodificar las 4 entradas bit0..bit3 de una instancia
// io.hexDisplay: `valid` es false si alguna entrada no es un 0/1 limpio (no
// se puede decodificar una direccion ambigua, igual que un decodificador BCD
// real), en cuyo caso `segments` queda en su valor por defecto (todo false).
// `hasConflict` solo distingue, dentro de ese caso invalido, si la causa fue
// un genuino LogicValue::Error (dos drivers en pugna) o simplemente una
// entrada sin conectar (HighImpedance/Unknown, p. ej. mientras el
// componente todavia se esta arrastrando en el lienzo) -- quien lo consuma
// (editor::ComponentItem::paintHexDisplay) dibuja apagado en el segundo
// caso y en rojo de conflicto solo en el primero, igual que ya distingue
// io.seven_segment segmento por segmento.
struct HexDisplayState {
    bool valid = false;
    bool hasConflict = false;
    std::array<bool, 7> segments{};
};
// Lanza std::invalid_argument si `display` no es una instancia io.hexDisplay.
[[nodiscard]] HexDisplayState hexDisplayState(const ComponentInstance& display, core::LogicValue bit0,
                                               core::LogicValue bit1, core::LogicValue bit2, core::LogicValue bit3);

// Lado maximo de una io.ledMatrix con conexion directa (un pin por celda):
// mas alla de 8x8 la cantidad de pines deja de ser manejable a mano. La
// variante multiplexada no tiene este limite porque usa filas+columnas pines.
inline constexpr uint64_t kLedMatrixMaxDirectSide = 8;

// True si la io.ledMatrix descrita por `properties` usa pines de fila/columna
// en vez de un pin por celda. Tolera mapas sin la propiedad "wiring"
// (proyectos guardados antes de que existiera), en cuyo caso es directa.
[[nodiscard]] bool ledMatrixIsMultiplexed(const PropertyMap& properties);
[[nodiscard]] bool ledMatrixIsMultiplexed(const ComponentInstance& matrix);

// Igual que ledIsLit(), pero para una celda de una instancia io.ledMatrix con
// conexion directa (respeta la misma propiedad "activeHigh"). Lanza
// std::invalid_argument si `matrix` no es una instancia io.ledMatrix.
[[nodiscard]] bool ledMatrixCellIsLit(const ComponentInstance& matrix, core::LogicValue netValue);

// Estado de una celda de una io.ledMatrix multiplexada: encendida solo si su
// fila esta en el nivel activo (propiedad "activeHigh") y su columna en el
// nivel contrario, que es como conduce un panel real (la fila alimenta y la
// columna drena). Cualquier valor que no sea un 0/1 limpio deja la celda
// apagada. Lanza std::invalid_argument si `matrix` no es una io.ledMatrix.
[[nodiscard]] bool ledMatrixMultiplexedCellIsLit(const ComponentInstance& matrix, core::LogicValue rowValue,
                                                  core::LogicValue colValue);

// Decodifica las 8 entradas bit0..bit7 (bit0 = LSB) de una instancia
// io.terminal como un caracter ASCII. Devuelve std::nullopt si alguna
// entrada no es un 0/1 limpio (byte ambiguo, no se puede mostrar un
// caracter). Lanza std::invalid_argument si `terminal` no es una instancia
// io.terminal.
[[nodiscard]] std::optional<char> terminalCharacter(const ComponentInstance& terminal,
                                                     std::span<const core::LogicValue> bits);

} // namespace digitalforge::components
