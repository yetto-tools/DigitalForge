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

nlohmann::json serializeEndpoint(const WireEndpoint& endpoint, const CircuitDocument& document) {
    if (endpoint.isJunction) {
        return nlohmann::json{{"kind", "junction"}, {"junctionId", endpoint.id}};
    }
    nlohmann::json e{{"kind", "pin"}, {"componentId", endpoint.id}, {"pinIndex", endpoint.pinIndex}};
    // La CLAVE del pin es lo que identifica la conexion; el indice se sigue
    // guardando solo para poder abrir el archivo en versiones anteriores. Al
    // cargar manda la clave (ver resolveEndpoint): si un componente reordena
    // sus pines, reconectar por indice cablearia el circuito a la entrada
    // equivocada en silencio.
    if (const components::ComponentInstance* instance = document.component(endpoint.id)) {
        if (endpoint.pinIndex < instance->pins().size()) {
            e["pinKey"] = instance->pins()[endpoint.pinIndex].name;
        }
    }
    return e;
}

// Extremo de cable ya resuelto contra las definiciones instaladas.
struct ResolvedEndpoint {
    WireEndpoint endpoint;
    // Clave guardada que no existe hoy en el componente. Vacia si resolvio
    // bien; si no, el cable NO debe conectarse por indice.
    std::string missingPinKey;
};

// "kind" ausente == "pin", para no romper archivos guardados antes de que
// existiera esta distincion (cuando todo cable era necesariamente pin-a-pin).
ResolvedEndpoint resolveEndpoint(const nlohmann::json& e, const CircuitDocument& document) {
    const std::string kind = e.contains("kind") ? e.at("kind").get<std::string>() : std::string("pin");
    if (kind == "junction") {
        return ResolvedEndpoint{WireEndpoint::junction(e.at("junctionId").get<uint32_t>()), {}};
    }

    const auto componentId = e.at("componentId").get<uint32_t>();
    const auto storedIndex = static_cast<uint16_t>(e.at("pinIndex").get<unsigned>());

    // Sin clave guardada (archivo anterior a este campo) no queda otra que
    // confiar en el indice: es lo unico que ese archivo llego a registrar.
    if (!e.contains("pinKey") || !e.at("pinKey").is_string()) {
        return ResolvedEndpoint{WireEndpoint(PinRef{componentId, storedIndex}), {}};
    }

    const auto pinKey = e.at("pinKey").get<std::string>();
    const components::ComponentInstance* instance = document.component(componentId);
    if (instance == nullptr) {
        return ResolvedEndpoint{WireEndpoint(PinRef{componentId, storedIndex}), {}};
    }
    const std::vector<components::PinTemplate>& pins = instance->pins();
    for (std::size_t i = 0; i < pins.size(); ++i) {
        if (pins[i].name == pinKey) {
            // Puede no coincidir con storedIndex: eso es exactamente lo que
            // repara reconectar por clave cuando los pines se reordenaron.
            return ResolvedEndpoint{WireEndpoint(PinRef{componentId, static_cast<uint16_t>(i)}), {}};
        }
    }
    // La clave ya no existe. Se devuelve el extremo tal cual para poder
    // informarlo, pero el cable no se restaura.
    return ResolvedEndpoint{WireEndpoint(PinRef{componentId, storedIndex}), pinKey};
}

// Identidad y huellas de compatibilidad de una instancia, tal como se guardan
// dentro de su entrada de componente. El proyecto no guarda la definicion
// completa: solo lo necesario para detectar, al reabrirlo, que la definicion
// instalada ya no es la que se uso (ver components/ComponentFingerprints.hpp).
nlohmann::json serializeCompatibility(const components::ComponentInstance& instance) {
    const components::ComponentFingerprints fingerprints = components::computeFingerprints(instance);
    return nlohmann::json{
        {"publicInterfaceHash", fingerprints.publicInterface.toHex()},
        {"pinInterfaceHash", fingerprints.pinInterface.toHex()},
        {"propertyInterfaceHash", fingerprints.propertyInterface.toHex()},
        {"simulationHash", fingerprints.simulation.toHex()},
        // Fuera del ejemplo minimo de la especificacion, pero necesarias para
        // poder clasificar un cambio como puramente visual o de encapsulado
        // en vez de agruparlo con el resto.
        {"appearanceHash", fingerprints.appearance.toHex()},
        {"packageHash", fingerprints.package.toHex()},
    };
}

// Lee una huella hexadecimal del JSON. Una ausente o corrupta queda nula, que
// se interpreta como "no comparable" y no como "distinta".
core::Hash256 parseHash(const nlohmann::json& compatibility, const char* key) {
    core::Hash256 hash;
    if (!compatibility.contains(key) || !compatibility.at(key).is_string()) {
        return hash;
    }
    // El fallo se ignora a proposito: fromHex() deja `hash` intacto (nulo) y
    // un hash nulo ya significa "no comparable", que es justo lo que debe
    // pasar con un valor corrupto.
    static_cast<void>(core::Hash256::fromHex(compatibility.at(key).get<std::string>(), hash));
    return hash;
}

} // namespace

nlohmann::json serializeProject(const CircuitDocument& document) {
    nlohmann::json json;
    json["schemaVersion"] = 1;

    nlohmann::json components = nlohmann::json::array();
    for (const uint32_t id : document.componentIds()) {
        const components::ComponentInstance* instance = document.component(id);
        nlohmann::json c = instance->toJson();
        c["definitionVersion"] = instance->definition().definitionVersion;
        c["compatibility"] = serializeCompatibility(*instance);
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
            {"a", serializeEndpoint(w->a, document)},
            {"b", serializeEndpoint(w->b, document)},
            {"waypoints", std::move(waypoints)},
        });
    }
    json["wires"] = std::move(wires);

    return json;
}

void loadProject(CircuitDocument& document, const nlohmann::json& json, ProjectCompatibilityReport* report) {
    if (!json.contains("schemaVersion") || json.at("schemaVersion").get<int>() != 1) {
        throw std::invalid_argument("loadProject: unsupported or missing schemaVersion");
    }

    if (report != nullptr) {
        *report = ProjectCompatibilityReport{};
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

        // Contraste contra la definicion instalada. Se hace despues de
        // colocarla porque la instancia del documento es la que tiene los
        // pines ya derivados (incluidos los de un subcircuito, que dependen
        // de un documento externo).
        if (report == nullptr || !c.contains("compatibility")) {
            continue;
        }
        report->hasStoredMetadata = true;

        const nlohmann::json& compatibility = c.at("compatibility");
        components::ComponentFingerprints stored;
        stored.publicInterface = parseHash(compatibility, "publicInterfaceHash");
        stored.pinInterface = parseHash(compatibility, "pinInterfaceHash");
        stored.propertyInterface = parseHash(compatibility, "propertyInterfaceHash");
        stored.simulation = parseHash(compatibility, "simulationHash");
        stored.appearance = parseHash(compatibility, "appearanceHash");
        stored.package = parseHash(compatibility, "packageHash");

        const components::ComponentInstance* placedInstance = document.component(parsed.instanceId());
        if (placedInstance == nullptr) {
            continue;
        }
        components::ComponentFingerprints current = components::computeFingerprints(*placedInstance);
        // Las huellas que el archivo no traiga (proyecto viejo, campo nuevo)
        // se neutralizan copiando la actual: comparar contra un hash nulo
        // reportaria una incompatibilidad inexistente.
        if (stored.publicInterface.isNull()) {
            stored.publicInterface = current.publicInterface;
        }
        if (stored.pinInterface.isNull()) {
            stored.pinInterface = current.pinInterface;
        }
        if (stored.propertyInterface.isNull()) {
            stored.propertyInterface = current.propertyInterface;
        }
        if (stored.simulation.isNull()) {
            stored.simulation = current.simulation;
        }
        if (stored.appearance.isNull()) {
            stored.appearance = current.appearance;
        }
        if (stored.package.isNull()) {
            stored.package = current.package;
        }

        const components::CompatibilityVerdict verdict = components::compareFingerprints(stored, current);
        if (verdict == components::CompatibilityVerdict::Identical) {
            continue;
        }
        ComponentCompatibilityIssue issue;
        issue.componentId = parsed.instanceId();
        issue.typeId = typeId;
        issue.storedDefinitionVersion =
            c.contains("definitionVersion") ? c.at("definitionVersion").get<uint32_t>() : 0;
        issue.currentDefinitionVersion = definition.definitionVersion;
        issue.verdict = verdict;
        report->issues.push_back(std::move(issue));
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
        const ResolvedEndpoint a = resolveEndpoint(w.at("a"), document);
        const ResolvedEndpoint b = resolveEndpoint(w.at("b"), document);

        // Una clave de pin que ya no existe NO se reconecta por indice: se
        // deja el cable sin restaurar y se informa, para que el usuario vea
        // que se perdio en vez de terminar con el circuito mal cableado.
        if (!a.missingPinKey.empty() || !b.missingPinKey.empty()) {
            if (report != nullptr) {
                const ResolvedEndpoint& broken = a.missingPinKey.empty() ? b : a;
                UnresolvedWire unresolved;
                unresolved.wireId = wire.id;
                unresolved.componentId = broken.endpoint.id;
                unresolved.pinKey = broken.missingPinKey;
                report->unresolvedWires.push_back(std::move(unresolved));
            }
            continue;
        }

        wire.a = a.endpoint;
        wire.b = b.endpoint;
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
