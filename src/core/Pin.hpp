#pragma once

#include <cstdint>

namespace digitalforge::core {

// Dirección de un pin físico con nombre en un componente (usado por las
// definiciones de componentes y los encapsulados de IC en fases posteriores).
// El núcleo de simulación en sí solo maneja índices de net de entrada/salida
// de gates; este enum existe para que las capas superiores (cargador de
// JSON, renderizador de encapsulados DIP) compartan un mismo vocabulario.
enum class PinDirection : uint8_t {
    Input,
    Output,
    Bidirectional,
    Power,
    NotConnected,
};

[[nodiscard]] constexpr const char* toString(PinDirection direction) noexcept {
    switch (direction) {
        case PinDirection::Input:         return "input";
        case PinDirection::Output:        return "output";
        case PinDirection::Bidirectional: return "bidirectional";
        case PinDirection::Power:         return "power";
        case PinDirection::NotConnected:  return "nc";
    }
    return "unknown";
}

} // namespace digitalforge::core
