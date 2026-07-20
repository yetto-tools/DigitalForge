#include "ProjectSerializer.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "components/ComponentInstance.hpp"
#include "editor/CircuitDocument.hpp"

namespace digitalforge::formats {

using editor::CircuitDocument;
using editor::ComponentPlacement;
using editor::PinRef;
using editor::WireConnection;
using editor::WireEndpoint;

namespace {

nlohmann::json serializeEndpoint(const WireEndpoint& endpoint) {
    if (endpoint.isJunction) {
        return nlohmann::json{{"kind", "junction"}, {"junctionId", endpoint.id}};
    }
    return nlohmann::json{{"kind", "pin"}, {"componentId", endpoint.id}, {"pinIndex", endpoint.pinIndex}};
}

// "kind" ausente == "pin", para no romper archivos guardados antes de que
// existiera esta distincion (cuando todo cable era necesariamente pin-a-pin).
WireEndpoint parseEndpoint(const nlohmann::json& e) {
    const std::string kind = e.contains("kind") ? e.at("kind").get<std::string>() : std::string("pin");
    if (kind == "junction") {
        return WireEndpoint::junction(e.at("junctionId").get<uint32_t>());
    }
    return WireEndpoint(
        PinRef{e.at("componentId").get<uint32_t>(), static_cast<uint16_t>(e.at("pinIndex").get<unsigned>())});
}

} // namespace

nlohmann::json serializeProject(const CircuitDocument& document) {
    nlohmann::json json;
    json["schemaVersion"] = 1;

    nlohmann::json components = nlohmann::json::array();
    for (const uint32_t id : document.componentIds()) {
        const components::ComponentInstance* instance = document.component(id);
        nlohmann::json c = instance->toJson();
        const ComponentPlacement placement = document.componentPlacement(id);
        c["position"] = {{"x", placement.position.x()}, {"y", placement.position.y()}};
        c["rotation"] = placement.rotationDegrees;
        c["zOrder"] = placement.zOrder;
        components.push_back(std::move(c));
    }
    json["components"] = std::move(components);

    nlohmann::json junctions = nlohmann::json::array();
    for (const uint32_t junctionId : document.junctionIds()) {
        const QPointF position = document.junctionPosition(junctionId);
        junctions.push_back({{"id", junctionId}, {"x", position.x()}, {"y", position.y()}});
    }
    json["junctions"] = std::move(junctions);

    nlohmann::json wires = nlohmann::json::array();
    for (const uint32_t wireId : document.wireIds()) {
        const WireConnection* w = document.wire(wireId);
        nlohmann::json waypoints = nlohmann::json::array();
        for (const QPointF& p : w->waypoints) {
            waypoints.push_back({{"x", p.x()}, {"y", p.y()}});
        }
        wires.push_back({
            {"id", w->id},
            {"a", serializeEndpoint(w->a)},
            {"b", serializeEndpoint(w->b)},
            {"waypoints", std::move(waypoints)},
        });
    }
    json["wires"] = std::move(wires);

    return json;
}

void loadProject(CircuitDocument& document, const nlohmann::json& json) {
    if (!json.contains("schemaVersion") || json.at("schemaVersion").get<int>() != 1) {
        throw std::invalid_argument("loadProject: unsupported or missing schemaVersion");
    }

    document.clear();

    for (const nlohmann::json& c : json.at("components")) {
        const std::string typeId = c.at("typeId").get<std::string>();
        if (!document.registry().contains(typeId)) {
            throw std::invalid_argument("loadProject: unknown component typeId '" + typeId + "'");
        }
        const components::ComponentDefinition& definition = document.registry().definition(typeId);
        const components::ComponentInstance parsed = components::ComponentInstance::fromJson(definition, c);

        components::PropertyMap overrides;
        for (const components::PropertyDescriptor& descriptor : definition.properties) {
            overrides[descriptor.id] = parsed.property(descriptor.id);
        }

        ComponentPlacement placement;
        placement.position = QPointF(c.at("position").at("x").get<double>(), c.at("position").at("y").get<double>());
        placement.rotationDegrees = c.at("rotation").get<int>();
        // "zOrder" es un campo nuevo, aditivo -- ausente en archivos
        // guardados antes de que existiera (todos los componentes se
        // apilaban por orden de insercion, equivalente a que todos
        // compartan 0).
        placement.zOrder = c.contains("zOrder") ? c.at("zOrder").get<int>() : 0;

        document.addComponentWithId(parsed.instanceId(), typeId, overrides, placement);
    }

    // "junctions" es un campo nuevo, aditivo -- ausente en archivos guardados
    // antes de que existiera este concepto (todo cable era pin-a-pin).
    if (json.contains("junctions")) {
        for (const nlohmann::json& j : json.at("junctions")) {
            const uint32_t id = j.at("id").get<uint32_t>();
            const QPointF position(j.at("x").get<double>(), j.at("y").get<double>());
            document.addJunctionWithId(id, position);
        }
    }

    for (const nlohmann::json& w : json.at("wires")) {
        WireConnection wire;
        wire.id = w.at("id").get<uint32_t>();
        wire.a = parseEndpoint(w.at("a"));
        wire.b = parseEndpoint(w.at("b"));
        if (w.contains("waypoints")) {
            for (const nlohmann::json& p : w.at("waypoints")) {
                wire.waypoints.emplace_back(p.at("x").get<double>(), p.at("y").get<double>());
            }
        }
        document.restoreWire(wire);
    }
}

void saveProjectToFile(const CircuitDocument& document, const std::string& path) {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("saveProjectToFile: cannot open '" + path + "' for writing");
    }
    file << serializeProject(document).dump(2);
    if (!file.good()) {
        throw std::runtime_error("saveProjectToFile: write failed for '" + path + "'");
    }
}

void loadProjectFromFile(CircuitDocument& document, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("loadProjectFromFile: cannot open '" + path + "' for reading");
    }
    nlohmann::json json;
    file >> json;
    loadProject(document, json);
}

} // namespace digitalforge::formats
