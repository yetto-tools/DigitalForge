#pragma once

#include <cstdint>
#include <limits>

namespace digitalforge::core {

using NetId = uint32_t;

inline constexpr NetId kInvalidNet = std::numeric_limits<NetId>::max();

// Un consumidor de una net: el gate `gateIndex` lee esta net en su pin de
// entrada `inputPin`. Se almacena por net para que, ante un cambio de valor
// en la net, el simulador pueda encolar exactamente los pares (gate, pin)
// afectados sin recorrer todos los gates del circuito.
struct FanoutEntry {
    uint32_t gateIndex = 0;
    uint16_t inputPin = 0;
};

static_assert(sizeof(FanoutEntry) <= 8, "FanoutEntry must stay compact");

} // namespace digitalforge::core
