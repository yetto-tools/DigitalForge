#include "ComponentRegistry.hpp"

#include <stdexcept>

namespace digitalforge::components {

void ComponentRegistry::registerDefinition(ComponentDefinition definition) {
    const std::string typeId = definition.typeId;
    if (definitions_.contains(typeId)) {
        throw std::invalid_argument("ComponentRegistry: typeId '" + typeId + "' is already registered");
    }
    definitions_.emplace(typeId, std::move(definition));
}

bool ComponentRegistry::contains(const std::string& typeId) const noexcept { return definitions_.contains(typeId); }

const ComponentDefinition& ComponentRegistry::definition(const std::string& typeId) const {
    const auto it = definitions_.find(typeId);
    if (it == definitions_.end()) {
        throw std::invalid_argument("ComponentRegistry: unknown typeId '" + typeId + "'");
    }
    return it->second;
}

ComponentInstance ComponentRegistry::create(const std::string& typeId, uint32_t instanceId,
                                             PropertyMap overrides) const {
    return ComponentInstance(definition(typeId), instanceId, std::move(overrides));
}

std::vector<std::string> ComponentRegistry::registeredTypeIds() const {
    std::vector<std::string> ids;
    ids.reserve(definitions_.size());
    for (const auto& [id, def] : definitions_) {
        ids.push_back(id);
    }
    return ids;
}

} // namespace digitalforge::components
