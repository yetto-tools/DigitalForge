#pragma once

#include <cstdint>

#include "LogicValue.hpp"

namespace digitalforge::core {

// Un estímulo pendiente: el gate `componentId` debe observar el valor `value`
// en su pin de entrada `inputPin` en el instante lógico `timestamp`.
struct SimulationEvent {
    uint64_t timestamp = 0;
    uint32_t componentId = 0;
    uint32_t inputPin = 0;
    LogicValue value = LogicValue::Unknown;
};

} // namespace digitalforge::core
