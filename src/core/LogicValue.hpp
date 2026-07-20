#pragma once

#include <cstdint>
#include <span>

namespace digitalforge::core {

// Lógica de cinco valores utilizada en todo el núcleo de simulación.
enum class LogicValue : uint8_t {
    Zero = 0,
    One = 1,
    HighImpedance = 2,
    Unknown = 3,
    Error = 4,
};

[[nodiscard]] constexpr char toChar(LogicValue value) noexcept {
    switch (value) {
        case LogicValue::Zero:          return '0';
        case LogicValue::One:           return '1';
        case LogicValue::HighImpedance: return 'Z';
        case LogicValue::Unknown:       return 'X';
        case LogicValue::Error:         return 'E';
    }
    return '?';
}

[[nodiscard]] constexpr bool isDriving(LogicValue value) noexcept {
    return value != LogicValue::HighImpedance;
}

// Una entrada de gate flotante (Z) o contradictoria (Error) no puede tratarse
// como un nivel digital limpio, por lo que la lógica combinacional la degrada
// a Unknown a menos que un valor dominante (0 para la familia AND, 1 para la
// familia OR) la anule primero.
[[nodiscard]] constexpr LogicValue degradeToUnknownIfIndeterminate(LogicValue value) noexcept {
    if (value == LogicValue::Error) {
        return LogicValue::Error;
    }
    if (value == LogicValue::Unknown || value == LogicValue::HighImpedance) {
        return LogicValue::Unknown;
    }
    return value;
}

[[nodiscard]] constexpr LogicValue logicNot(LogicValue value) noexcept {
    switch (value) {
        case LogicValue::Zero: return LogicValue::One;
        case LogicValue::One:  return LogicValue::Zero;
        case LogicValue::Error: return LogicValue::Error;
        default: return LogicValue::Unknown;
    }
}

[[nodiscard]] constexpr LogicValue logicAnd2(LogicValue a, LogicValue b) noexcept {
    if (a == LogicValue::Zero || b == LogicValue::Zero) {
        return LogicValue::Zero;
    }
    if (a == LogicValue::Error || b == LogicValue::Error) {
        return LogicValue::Error;
    }
    if (a == LogicValue::Unknown || a == LogicValue::HighImpedance ||
        b == LogicValue::Unknown || b == LogicValue::HighImpedance) {
        return LogicValue::Unknown;
    }
    return LogicValue::One;
}

[[nodiscard]] constexpr LogicValue logicOr2(LogicValue a, LogicValue b) noexcept {
    if (a == LogicValue::One || b == LogicValue::One) {
        return LogicValue::One;
    }
    if (a == LogicValue::Error || b == LogicValue::Error) {
        return LogicValue::Error;
    }
    if (a == LogicValue::Unknown || a == LogicValue::HighImpedance ||
        b == LogicValue::Unknown || b == LogicValue::HighImpedance) {
        return LogicValue::Unknown;
    }
    return LogicValue::Zero;
}

[[nodiscard]] constexpr LogicValue logicNand2(LogicValue a, LogicValue b) noexcept {
    return logicNot(logicAnd2(a, b));
}

[[nodiscard]] constexpr LogicValue logicNor2(LogicValue a, LogicValue b) noexcept {
    return logicNot(logicOr2(a, b));
}

[[nodiscard]] constexpr LogicValue logicXor2(LogicValue a, LogicValue b) noexcept {
    if (a == LogicValue::Error || b == LogicValue::Error) {
        return LogicValue::Error;
    }
    if (a == LogicValue::Unknown || a == LogicValue::HighImpedance ||
        b == LogicValue::Unknown || b == LogicValue::HighImpedance) {
        return LogicValue::Unknown;
    }
    return (a == b) ? LogicValue::Zero : LogicValue::One;
}

[[nodiscard]] constexpr LogicValue logicXnor2(LogicValue a, LogicValue b) noexcept {
    return logicNot(logicXor2(a, b));
}

// Resuelve el valor impulsado en una net a partir del conjunto de valores
// declarados por sus drivers. Los drivers que reportan HighImpedance no están
// impulsando activamente y se ignoran; si ningún driver impulsa activamente,
// la net queda flotante. Si dos o más drivers no coinciden en un valor real,
// la net entra en conflicto (Error).
[[nodiscard]] inline LogicValue resolveNetValue(std::span<const LogicValue> driverValues) noexcept {
    bool any = false;
    LogicValue resolved = LogicValue::HighImpedance;
    for (const LogicValue driven : driverValues) {
        if (!isDriving(driven)) {
            continue;
        }
        if (!any) {
            resolved = driven;
            any = true;
            continue;
        }
        if (resolved != driven) {
            return LogicValue::Error;
        }
    }
    return any ? resolved : LogicValue::HighImpedance;
}

} // namespace digitalforge::core
