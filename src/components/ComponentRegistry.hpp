#pragma once

#include <map>
#include <string>
#include <vector>

#include "ComponentDefinition.hpp"
#include "ComponentInstance.hpp"

namespace digitalforge::components {

// Catalogo central de *tipos* de componente, indexado por typeId estable
// (p. ej. "gates.and"), nunca por nombre visible. ComponentInstance es un
// tipo de valor concreto en lugar de un ComponentModel polimorfico, por lo
// que create() devuelve uno directamente en vez de un unique_ptr a una
// clase base - no hay ninguna subclase por tipo de componente que asignar,
// lo que mantiene economica y libre de despacho virtual la colocacion de
// millones de instancias.
class ComponentRegistry {
public:
    // Lanza std::invalid_argument si ya hay una definicion registrada con
    // el mismo typeId.
    void registerDefinition(ComponentDefinition definition);

    [[nodiscard]] bool contains(const std::string& typeId) const noexcept;

    // Lanza std::invalid_argument si `typeId` no esta registrado.
    [[nodiscard]] const ComponentDefinition& definition(const std::string& typeId) const;

    // Lanza std::invalid_argument si `typeId` no esta registrado o si
    // alguna propiedad de override no es valida para ese tipo (ver
    // ComponentInstance).
    [[nodiscard]] ComponentInstance create(const std::string& typeId, uint32_t instanceId,
                                            PropertyMap overrides = {}) const;

    [[nodiscard]] std::vector<std::string> registeredTypeIds() const;

private:
    std::map<std::string, ComponentDefinition> definitions_;
};

} // namespace digitalforge::components
