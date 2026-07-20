#include "ComponentInstance.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace digitalforge::components {

namespace {

nlohmann::json propertyValueToJson(const PropertyValue& value) {
    return std::visit([](const auto& v) { return nlohmann::json(v); }, value);
}

PropertyValue propertyValueFromJson(const nlohmann::json& json, PropertyType type) {
    switch (type) {
        case PropertyType::Boolean:
            return json.get<bool>();
        case PropertyType::Integer:
            return json.get<int64_t>();
        case PropertyType::UnsignedInteger:
            return json.get<uint64_t>();
        case PropertyType::String:
        case PropertyType::Enum:
        case PropertyType::Color: // guardado como el mismo string hex "#RRGGBB"
            return json.get<std::string>();
    }
    throw std::invalid_argument("propertyValueFromJson: unrecognized PropertyType");
}

} // namespace

ComponentInstance::ComponentInstance(const ComponentDefinition& definition, uint32_t instanceId,
                                      PropertyMap overrides)
    : definition_(&definition), instanceId_(instanceId) {
    for (const auto& [id, value] : overrides) {
        if (definition_->findProperty(id) == nullptr) {
            throw std::invalid_argument("ComponentInstance: unknown property '" + id + "' for type '" +
                                         definition_->typeId + "'");
        }
    }

    for (const PropertyDescriptor& descriptor : definition_->properties) {
        const auto it = overrides.find(descriptor.id);
        const PropertyValue& candidate = (it != overrides.end()) ? it->second : descriptor.defaultValue;

        std::string error;
        if (!validatePropertyValue(descriptor, candidate, &error)) {
            throw std::invalid_argument("ComponentInstance: " + error);
        }
        properties_[descriptor.id] = candidate;
    }

    derivePins();
}

const PropertyValue& ComponentInstance::property(const std::string& id) const {
    const auto it = properties_.find(id);
    if (it == properties_.end()) {
        throw std::invalid_argument("ComponentInstance: unknown property '" + id + "'");
    }
    return it->second;
}

void ComponentInstance::setProperty(const std::string& id, PropertyValue value) {
    const PropertyDescriptor* descriptor = definition_->findProperty(id);
    if (descriptor == nullptr) {
        throw std::invalid_argument("ComponentInstance: unknown property '" + id + "' for type '" +
                                     definition_->typeId + "'");
    }

    std::string error;
    if (!validatePropertyValue(*descriptor, value, &error)) {
        throw std::invalid_argument("ComponentInstance: " + error);
    }

    properties_[id] = std::move(value);

    // Solo recalcular los pines - y descartar el cableado/binding de
    // simulacion existente - si el cambio de propiedad realmente altero la
    // disposicion de pines (p. ej. el inputCount de AND). Un cambio
    // puramente cosmetico (p. ej. "label") no debe desmontar las
    // conexiones existentes.
    applyRecomputedPins(computePins());
}

void ComponentInstance::refreshDerivedPins() { applyRecomputedPins(computePins()); }

void ComponentInstance::applyRecomputedPins(std::vector<PinTemplate> newPins) {
    if (newPins != pins_) {
        pins_ = std::move(newPins);
        pinNets_.assign(pins_.size(), core::kInvalidNet);
        simBinding_ = ComponentSimBinding{};
    }
}

std::vector<PinTemplate> ComponentInstance::computePins() const {
    if (definition_->deriveExternalPins) {
        return definition_->deriveExternalPins(properties_, externalContext_);
    }
    return definition_->derivePins(properties_);
}

void ComponentInstance::derivePins() {
    pins_ = computePins();
    pinNets_.assign(pins_.size(), core::kInvalidNet);
}

void ComponentInstance::bindPin(std::size_t pinIndex, core::NetId net) { pinNets_.at(pinIndex) = net; }

void ComponentInstance::unbindPin(std::size_t pinIndex) { pinNets_.at(pinIndex) = core::kInvalidNet; }

bool ComponentInstance::allPinsBound() const noexcept {
    for (const core::NetId net : pinNets_) {
        if (net == core::kInvalidNet) {
            return false;
        }
    }
    return true;
}

void ComponentInstance::buildSimulation(core::Circuit& circuit) {
    if (!allPinsBound()) {
        throw std::logic_error("ComponentInstance: cannot build simulation, not every pin is connected");
    }
    simBinding_ = definition_->buildExternalSimulation
                      ? definition_->buildExternalSimulation(circuit, properties_, pinNets_, externalContext_)
                      : definition_->buildSimulation(circuit, properties_, pinNets_);
}

nlohmann::json ComponentInstance::toJson() const {
    nlohmann::json properties = nlohmann::json::object();
    for (const auto& [id, value] : properties_) {
        properties[id] = propertyValueToJson(value);
    }

    nlohmann::json json;
    json["typeId"] = definition_->typeId;
    json["instanceId"] = instanceId_;
    json["properties"] = std::move(properties);
    return json;
}

ComponentInstance ComponentInstance::fromJson(const ComponentDefinition& definition, const nlohmann::json& json) {
    if (!json.contains("typeId") || json.at("typeId").get<std::string>() != definition.typeId) {
        throw std::invalid_argument("ComponentInstance::fromJson: typeId mismatch");
    }
    if (!json.contains("instanceId") || !json.contains("properties")) {
        throw std::invalid_argument("ComponentInstance::fromJson: missing instanceId or properties");
    }

    const auto instanceId = json.at("instanceId").get<uint32_t>();
    const nlohmann::json& propertiesJson = json.at("properties");

    PropertyMap overrides;
    for (const PropertyDescriptor& descriptor : definition.properties) {
        if (propertiesJson.contains(descriptor.id)) {
            overrides[descriptor.id] = propertyValueFromJson(propertiesJson.at(descriptor.id), descriptor.type);
        }
    }

    return ComponentInstance(definition, instanceId, std::move(overrides));
}

} // namespace digitalforge::components
