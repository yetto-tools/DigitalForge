#include "JsonComponentLoader.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CommonProperties.hpp"
#include "ComponentRegistry.hpp"
#include "core/Circuit.hpp"
#include "core/GateType.hpp"
#include "core/Net.hpp"
#include "core/Pin.hpp"

namespace digitalforge::components {

namespace {

using core::GateType;
using core::NetId;

// Una compuerta primitiva del netlist, ya validada: su tipo, las nets que lee
// (por nombre) y la net que maneja (por nombre). Los nombres son o bien de un
// pin del componente, o bien de una net interna (cualquier nombre que no sea
// un pin).
struct NetlistGate {
    GateType type = GateType::Buffer;
    std::vector<std::string> inputs;
    std::string output;
};

[[nodiscard]] ComponentCategory categoryFromString(const std::string& name) {
    // Los nombres coinciden con components::toString(ComponentCategory), para
    // que el JSON use la misma ortografia que el resto del proyecto.
    if (name == "Wiring") return ComponentCategory::Wiring;
    if (name == "Gates") return ComponentCategory::Gates;
    if (name == "Plexers") return ComponentCategory::Plexers;
    if (name == "Arithmetic") return ComponentCategory::Arithmetic;
    if (name == "Memory") return ComponentCategory::Memory;
    if (name == "Subcircuits") return ComponentCategory::Subcircuits;
    if (name == "IO") return ComponentCategory::IO;
    if (name == "Base") return ComponentCategory::Base;
    if (name == "Analysis") return ComponentCategory::Analysis;
    if (name == "74LSxx") return ComponentCategory::Ic74LS;
    throw std::invalid_argument("categoria desconocida: '" + name +
                                 "' (usar Wiring/Gates/Plexers/Arithmetic/Memory/Subcircuits/IO/Base/Analysis/74LSxx)");
}

[[nodiscard]] core::PinDirection directionFromString(const std::string& dir) {
    if (dir == "in" || dir == "input") return core::PinDirection::Input;
    if (dir == "out" || dir == "output") return core::PinDirection::Output;
    throw std::invalid_argument("direccion de pin desconocida: '" + dir + "' (usar 'in' u 'out')");
}

[[nodiscard]] const nlohmann::json& requireField(const nlohmann::json& json, const char* key) {
    if (!json.contains(key)) {
        throw std::invalid_argument(std::string("falta el campo obligatorio '") + key + "'");
    }
    return json.at(key);
}

[[nodiscard]] std::string requireString(const nlohmann::json& json, const char* key) {
    const nlohmann::json& value = requireField(json, key);
    if (!value.is_string()) {
        throw std::invalid_argument(std::string("el campo '") + key + "' debe ser una cadena");
    }
    return value.get<std::string>();
}

[[nodiscard]] uint32_t optionalVersion(const nlohmann::json& json, const char* key) {
    if (!json.contains(key)) {
        return 1;
    }
    const nlohmann::json& value = json.at(key);
    if (!value.is_number_unsigned() || value.get<uint64_t>() == 0) {
        throw std::invalid_argument(std::string("el campo '") + key + "' debe ser un entero positivo");
    }
    return static_cast<uint32_t>(value.get<uint64_t>());
}

// Propiedades de presentacion que recibe todo componente cargado de JSON, para
// que se comporte como uno integrado (etiquetable, coloreable, redimensionable
// en la interfaz). El comportamiento en si no depende de ninguna propiedad en
// esta primera version: los pines y el netlist son fijos.
[[nodiscard]] std::vector<PropertyDescriptor> presentationProperties() {
    return {
        makeLabelProperty(),      makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"),
        makeCustomWidthProperty(), makeCustomHeightProperty(),  makeNotesProperty(),
    };
}

} // namespace

ComponentDefinition componentDefinitionFromJson(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::invalid_argument("la definicion de componente debe ser un objeto JSON");
    }

    ComponentDefinition definition;
    definition.typeId = requireString(json, "typeId");
    if (definition.typeId.empty()) {
        throw std::invalid_argument("'typeId' no puede estar vacio");
    }
    const std::string context = "componente '" + definition.typeId + "': ";

    try {
        definition.displayName =
            json.contains("displayName") ? requireString(json, "displayName") : definition.typeId;
        definition.description = json.contains("description") ? requireString(json, "description") : std::string();
        definition.category =
            json.contains("category") ? categoryFromString(requireString(json, "category")) : ComponentCategory::Base;
        definition.definitionVersion = optionalVersion(json, "definitionVersion");
        definition.behaviorVersion = optionalVersion(json, "behaviorVersion");
        definition.appearanceVersion = optionalVersion(json, "appearanceVersion");

        // --- Pines (fijos) ---
        const nlohmann::json& pinsJson = requireField(json, "pins");
        if (!pinsJson.is_array() || pinsJson.empty()) {
            throw std::invalid_argument("'pins' debe ser un arreglo no vacio");
        }
        std::vector<PinTemplate> pins;
        std::set<std::string> inputPinNames;
        std::set<std::string> outputPinNames;
        std::set<std::string> allPinNames;
        for (const nlohmann::json& pinJson : pinsJson) {
            PinTemplate pin;
            pin.name = requireString(pinJson, "name");
            if (pin.name.empty()) {
                throw std::invalid_argument("un pin tiene 'name' vacio");
            }
            if (!allPinNames.insert(pin.name).second) {
                throw std::invalid_argument("nombre de pin duplicado: '" + pin.name + "'");
            }
            pin.direction = directionFromString(requireString(pinJson, "dir"));
            if (pin.direction == core::PinDirection::Input) {
                inputPinNames.insert(pin.name);
            } else {
                outputPinNames.insert(pin.name);
            }
            pins.push_back(std::move(pin));
        }

        // --- Netlist de primitivas ---
        const nlohmann::json& netlistJson = requireField(json, "netlist");
        if (!netlistJson.is_array() || netlistJson.empty()) {
            throw std::invalid_argument("'netlist' debe ser un arreglo no vacio");
        }
        std::vector<NetlistGate> netlist;
        std::set<std::string> drivenNames; // toda net que alguna compuerta maneja
        for (const nlohmann::json& gateJson : netlistJson) {
            NetlistGate gate;
            const std::string gateName = requireString(gateJson, "gate");
            if (!core::gateTypeFromString(gateName, gate.type)) {
                throw std::invalid_argument("tipo de compuerta desconocido: '" + gateName + "'");
            }
            if (gate.type == GateType::InputPin) {
                throw std::invalid_argument("'InputPin' no es valido dentro de un netlist: las entradas del "
                                             "componente son sus propios pines de entrada");
            }
            if (gateJson.contains("in")) {
                const nlohmann::json& inJson = gateJson.at("in");
                if (!inJson.is_array()) {
                    throw std::invalid_argument("el campo 'in' de una compuerta debe ser un arreglo");
                }
                for (const nlohmann::json& n : inJson) {
                    if (!n.is_string() || n.get<std::string>().empty()) {
                        throw std::invalid_argument("cada entrada de 'in' debe ser un nombre de net no vacio");
                    }
                    gate.inputs.push_back(n.get<std::string>());
                }
            }
            gate.output = requireString(gateJson, "out");
            if (gate.output.empty()) {
                throw std::invalid_argument("el campo 'out' de una compuerta no puede estar vacio");
            }
            if (!core::isValidInputCount(gate.type, gate.inputs.size())) {
                throw std::invalid_argument("la compuerta '" + gateName + "' tiene una cantidad de entradas (" +
                                             std::to_string(gate.inputs.size()) + ") invalida para su tipo");
            }
            // Una compuerta no puede manejar un pin de ENTRADA del componente:
            // esos los controla lo que se cablee por fuera, no la logica interna.
            if (inputPinNames.count(gate.output) != 0) {
                throw std::invalid_argument("la compuerta maneja '" + gate.output +
                                             "', que es un pin de entrada del componente");
            }
            drivenNames.insert(gate.output);
            netlist.push_back(std::move(gate));
        }

        // Toda net que se pueda leer con seguridad: un pin de entrada (lo maneja
        // el exterior) o algo que alguna compuerta maneja.
        std::set<std::string> availableNames = drivenNames;
        availableNames.insert(inputPinNames.begin(), inputPinNames.end());
        for (const NetlistGate& gate : netlist) {
            for (const std::string& input : gate.inputs) {
                if (availableNames.count(input) == 0) {
                    throw std::invalid_argument("la net '" + input +
                                                 "' se lee pero nada la controla (no es un pin de entrada ni la "
                                                 "salida de ninguna compuerta)");
                }
            }
        }
        // Todo pin de SALIDA debe quedar manejado por alguna compuerta, si no
        // flotaria (Z) siempre.
        for (const std::string& outputPin : outputPinNames) {
            if (drivenNames.count(outputPin) == 0) {
                throw std::invalid_argument("el pin de salida '" + outputPin + "' no lo maneja ninguna compuerta");
            }
        }

        definition.properties = presentationProperties();

        definition.derivePins = [pins](const PropertyMap&) { return pins; };

        // El orden de los nombres de pin es paralelo a derivePins(): pinNets[i]
        // es la net enlazada al pin i, asi que reconstruimos el mapa
        // nombre->net desde ese orden y luego reproducimos el netlist.
        std::vector<std::string> pinOrder;
        pinOrder.reserve(pins.size());
        for (const PinTemplate& pin : pins) {
            pinOrder.push_back(pin.name);
        }
        definition.buildSimulation = [pinOrder, netlist](core::Circuit& circuit, const PropertyMap&,
                                                          const std::vector<NetId>& pinNets) {
            std::map<std::string, NetId> netOf;
            for (std::size_t i = 0; i < pinOrder.size(); ++i) {
                netOf[pinOrder[i]] = pinNets[i];
            }
            const auto resolve = [&](const std::string& name) -> NetId {
                const auto it = netOf.find(name);
                if (it != netOf.end()) {
                    return it->second;
                }
                const NetId fresh = circuit.addNet(); // net interna, creada al primer uso
                netOf[name] = fresh;
                return fresh;
            };

            ComponentSimBinding binding;
            binding.kind = ComponentSimBinding::Kind::Driver;
            bool first = true;
            for (const NetlistGate& gate : netlist) {
                std::vector<NetId> inputs;
                inputs.reserve(gate.inputs.size());
                for (const std::string& input : gate.inputs) {
                    inputs.push_back(resolve(input));
                }
                const NetId output = resolve(gate.output);
                const uint32_t gateIndex = circuit.addGate(gate.type, inputs, output);
                if (first) {
                    binding.gateIndex = gateIndex;
                    first = false;
                }
            }
            return binding;
        };
    } catch (const std::exception& e) {
        // Reetiqueta cualquier error de validacion con el typeId, para que el
        // reporte de carga diga de que componente se trata.
        throw std::invalid_argument(context + e.what());
    }

    return definition;
}

ComponentLoadReport loadComponentLibraryFromDirectory(ComponentRegistry& registry,
                                                       const std::filesystem::path& directory) {
    ComponentLoadReport report;

    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return report; // un directorio inexistente no es un error
    }

    // Orden alfabetico por ruta para que la carga sea determinista (y para que
    // un choque de typeId siempre lo reporte el mismo archivo de los dos).
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        const std::string source = file.string();
        try {
            std::ifstream stream(file);
            if (!stream.is_open()) {
                throw std::runtime_error("no se pudo abrir el archivo");
            }
            const nlohmann::json json = nlohmann::json::parse(stream);
            ComponentDefinition definition = componentDefinitionFromJson(json);
            const std::string typeId = definition.typeId;
            registry.registerDefinition(std::move(definition)); // lanza si el typeId ya existe
            report.loadedTypeIds.push_back(typeId);
        } catch (const std::exception& e) {
            report.errors.push_back(ComponentLoadError{source, e.what()});
        }
    }

    return report;
}

} // namespace digitalforge::components
