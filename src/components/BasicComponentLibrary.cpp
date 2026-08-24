#include "BasicComponentLibrary.hpp"

#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "CommonProperties.hpp"
#include "ExternalDocumentView.hpp"
#include "core/GateType.hpp"

namespace digitalforge::components {

using core::GateType;
using core::LogicValue;
using core::NetId;

namespace {

// Las propiedades de presentacion comunes (makeLabelProperty,
// makeLabelRotationProperty, makeBodyColorProperty, makeCustomWidthProperty,
// makeCustomHeightProperty, makeNotesProperty) viven ahora en
// CommonProperties.hpp, compartidas con el cargador de componentes desde JSON.

// Compartida por io.led/io.hexDisplay/io.ledMatrix: color de la lente/
// segmentos/celdas, correspondiente a colores reales comunes de LED. Solo
// visual - ver ledBaseColor() en editor::ComponentItem.cpp, que traduce
// cada opcion al QColor real usado al dibujar.
PropertyDescriptor makeColorOptionProperty() {
    return PropertyDescriptor{
        .id = "color",
        .displayName = "Color",
        .description = "Color de la lente/segmentos, correspondiente a colores reales comunes de LED. Solo visual.",
        .type = PropertyType::Enum,
        .defaultValue = std::string("red"),
        .minValue = std::nullopt,
        .maxValue = std::nullopt,
        .enumOptions = {"red", "green", "yellow", "blue", "white"},
        .affectsSimulation = false,
        .affectsAppearance = true,
    };
}

// Solo para gates.* - cuantos ticks del simulador tarda la puerta en
// propagar un cambio a su salida (ver core::Gate::delay,
// Simulator::applyGateOutput). No aplica a sinks/wiring, que no tienen
// logica combinacional propia que retardar.
PropertyDescriptor makePropagationDelayProperty() {
    return PropertyDescriptor{
        .id = "propagationDelay",
        .displayName = "Retardo de propagacion",
        .description = "Cantidad de ticks del simulador que tarda esta puerta en propagar un cambio a su salida.",
        .type = PropertyType::UnsignedInteger,
        .defaultValue = uint64_t{1},
        .minValue = uint64_t{1},
        .maxValue = uint64_t{100},
        .enumOptions = {},
        .affectsSimulation = true,
        .affectsAppearance = false,
    };
}

// Compartida por las 6 puertas variadicas: bit i en 1 invierte la entrada i
// antes de que llegue a la puerta principal (bubble de negacion por entrada,
// como en las puertas 74LSxx reales). Solo los bits bajos hasta inputCount
// son significativos - sin limite maximo dinamico, ya que inputCount es a su
// vez otra propiedad y esta capa no soporta propiedades cuyo rango depende
// de otra propiedad.
PropertyDescriptor makeInvertMaskProperty() {
    return PropertyDescriptor{
        .id = "invertMask",
        .displayName = "Mascara de negacion",
        .description = "Bit i en 1 invierte la entrada i antes de la puerta principal. "
                       "Solo los bits hasta la cantidad de entradas actual son significativos.",
        .type = PropertyType::UnsignedInteger,
        .defaultValue = uint64_t{0},
        .minValue = std::nullopt,
        .maxValue = std::nullopt,
        .enumOptions = {},
        .affectsSimulation = true,
        .affectsAppearance = false,
    };
}

ComponentDefinition makeVariadicGateDefinition(std::string typeId, std::string displayName, GateType gateType) {
    ComponentDefinition definition;
    definition.typeId = std::move(typeId);
    definition.displayName = std::move(displayName);
    definition.description = "Puerta combinacional con una cantidad configurable de entradas de 1 bit.";
    definition.category = ComponentCategory::Gates;
    definition.properties = {
        PropertyDescriptor{
            .id = "inputCount",
            .displayName = "Cantidad de entradas",
            .description = "Cantidad de pines de entrada.",
            .type = PropertyType::UnsignedInteger,
            .defaultValue = uint64_t{2},
            .minValue = uint64_t{2},
            .maxValue = uint64_t{64},
            .enumOptions = {},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        makeInvertMaskProperty(),
        makeLabelProperty(), makeLabelRotationProperty(),
        makeBodyColorProperty("#EBEBEB"),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
        makePropagationDelayProperty(),
    };
    definition.derivePins = [](const PropertyMap& properties) {
        const auto inputCount = std::get<uint64_t>(properties.at("inputCount"));
        std::vector<PinTemplate> pins;
        pins.reserve(inputCount + 1);
        for (uint64_t i = 0; i < inputCount; ++i) {
            pins.push_back(PinTemplate{"In" + std::to_string(i), core::PinDirection::Input});
        }
        pins.push_back(PinTemplate{"Y", core::PinDirection::Output});
        return pins;
    };
    definition.buildSimulation = [gateType](core::Circuit& circuit, const PropertyMap& properties,
                                             const std::vector<NetId>& pinNets) {
        std::vector<NetId> inputs(pinNets.begin(), pinNets.end() - 1);
        const NetId output = pinNets.back();
        const auto delay = static_cast<uint16_t>(std::get<uint64_t>(properties.at("propagationDelay")));
        const auto invertMask = std::get<uint64_t>(properties.at("invertMask"));
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            if (((invertMask >> i) & 1U) == 0U) {
                continue;
            }
            const NetId negated = circuit.addNet();
            (void)circuit.addGate(GateType::Not, std::vector<NetId>{inputs[i]}, negated);
            inputs[i] = negated;
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(gateType, inputs, output, delay);
        return binding;
    };
    return definition;
}

ComponentDefinition makeNotDefinition() {
    ComponentDefinition definition;
    definition.typeId = "gates.not";
    definition.displayName = "NOT";
    definition.description = "Inversor de una sola entrada.";
    definition.category = ComponentCategory::Gates;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty(), makePropagationDelayProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{
            PinTemplate{"A", core::PinDirection::Input},
            PinTemplate{"Y", core::PinDirection::Output},
        };
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto delay = static_cast<uint16_t>(std::get<uint64_t>(properties.at("propagationDelay")));
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(GateType::Not, std::vector<NetId>{pinNets[0]}, pinNets[1], delay);
        return binding;
    };
    return definition;
}

ComponentDefinition makeBufferDefinition() {
    ComponentDefinition definition;
    definition.typeId = "gates.buffer";
    definition.displayName = "BUFFER";
    definition.description = "Buffer no inversor de una sola entrada (degrada una entrada flotante a Unknown).";
    definition.category = ComponentCategory::Gates;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty(), makePropagationDelayProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{
            PinTemplate{"A", core::PinDirection::Input},
            PinTemplate{"Y", core::PinDirection::Output},
        };
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto delay = static_cast<uint16_t>(std::get<uint64_t>(properties.at("propagationDelay")));
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(GateType::Buffer, std::vector<NetId>{pinNets[0]}, pinNets[1], delay);
        return binding;
    };
    return definition;
}

// Pines fijos y con nombre semantico (D/OE/Y), no el patron generico In0/In1
// de las puertas variadicas - misma convencion que CLK/Cin/Cout en
// memoria/aritmetica para pines de aridad fija con roles distintos.
std::vector<PinTemplate> triStatePins() {
    return {
        PinTemplate{"D", core::PinDirection::Input},
        PinTemplate{"OE", core::PinDirection::Input},
        PinTemplate{"Y", core::PinDirection::Output},
    };
}

ComponentDefinition makeTriStateBufferDefinition() {
    ComponentDefinition definition;
    definition.typeId = "gates.tristateBuffer";
    definition.displayName = "Buffer triestado";
    definition.description = "Buffer no inversor con habilitacion (OE): si OE=1 repite D en Y, "
                              "si no deja Y en alta impedancia.";
    definition.category = ComponentCategory::Gates;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty(), makePropagationDelayProperty()};
    definition.derivePins = [](const PropertyMap&) { return triStatePins(); };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto delay = static_cast<uint16_t>(std::get<uint64_t>(properties.at("propagationDelay")));
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(GateType::TriStateBuffer, std::vector<NetId>{pinNets[0], pinNets[1]},
                                             pinNets[2], delay);
        return binding;
    };
    return definition;
}

ComponentDefinition makeTriStateInverterDefinition() {
    ComponentDefinition definition;
    definition.typeId = "gates.tristateInverter";
    definition.displayName = "Inversor triestado";
    definition.description = "Inversor con habilitacion (OE): si OE=1 repite NOT(D) en Y, "
                              "si no deja Y en alta impedancia.";
    definition.category = ComponentCategory::Gates;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty(), makePropagationDelayProperty()};
    definition.derivePins = [](const PropertyMap&) { return triStatePins(); };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto delay = static_cast<uint16_t>(std::get<uint64_t>(properties.at("propagationDelay")));
        const NetId negatedD = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{pinNets[0]}, negatedD);
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex =
            circuit.addGate(GateType::TriStateBuffer, std::vector<NetId>{negatedD, pinNets[1]}, pinNets[2], delay);
        return binding;
    };
    return definition;
}

// Compartida por los 4 tipos de Plexers: cuantos bits de seleccion tiene la
// instancia (determina la cantidad de lineas de datos/salidas como 2^N).
PropertyDescriptor makeSelectBitsProperty() {
    return PropertyDescriptor{
        .id = "selectBits",
        .displayName = "Bits de seleccion",
        .description = "Cantidad de bits de seleccion; determina la cantidad de lineas de datos/salidas (2^N).",
        .type = PropertyType::UnsignedInteger,
        .defaultValue = uint64_t{2},
        .minValue = uint64_t{1},
        .maxValue = uint64_t{4},
        .enumOptions = {},
        .affectsSimulation = true,
        .affectsAppearance = true,
    };
}

// Conecta `outputNet` a partir de `terms`: si hay un unico termino no hace
// falta ninguna puerta variadica (que exige aridad >= 2) - un Buffer alcanza
// para copiarlo tal cual. Usado por los 4 tipos de Plexers de abajo, cuya
// cantidad de terminos por salida varia segun selectBits (puede ser 1
// cuando selectBits=1, o al armar el codificador de prioridad).
uint32_t driveNetFromTerms(core::Circuit& circuit, GateType variadicType, const std::vector<NetId>& terms,
                            NetId outputNet) {
    if (terms.size() == 1) {
        return circuit.addGate(GateType::Buffer, terms, outputNet);
    }
    return circuit.addGate(variadicType, terms, outputNet);
}

// Un net por cada linea de seleccion con su valor negado (para poder armar
// el termino AND de cada combinacion sin importar si ese bit debe estar en
// 0 o en 1) - compartido por decoder/multiplexer/demultiplexer.
std::vector<NetId> negateEach(core::Circuit& circuit, const std::vector<NetId>& nets) {
    std::vector<NetId> negated(nets.size());
    for (std::size_t i = 0; i < nets.size(); ++i) {
        negated[i] = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{nets[i]}, negated[i]);
    }
    return negated;
}

// Los `selectBits` terminos (cada uno el net de seleccion o su negado, segun
// el bit correspondiente de `combination`) que identifican una combinacion
// puntual de las lineas de seleccion - el bloque de construccion comun a
// decoder/multiplexer/demultiplexer.
std::vector<NetId> selectTermsForCombination(const std::vector<NetId>& selectNets,
                                              const std::vector<NetId>& notSelectNets, std::size_t combination) {
    std::vector<NetId> terms(selectNets.size());
    for (std::size_t bit = 0; bit < selectNets.size(); ++bit) {
        terms[bit] = ((combination >> bit) & 1) != 0 ? selectNets[bit] : notSelectNets[bit];
    }
    return terms;
}

ComponentDefinition makeDecoderDefinition() {
    ComponentDefinition definition;
    definition.typeId = "plexers.decoder";
    definition.displayName = "Decodificador";
    definition.description = "N bits de seleccion -> 2^N salidas; una sola salida activa por combinacion.";
    definition.category = ComponentCategory::Plexers;
    definition.properties = {makeSelectBitsProperty(), makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(), makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        const auto selectBits = std::get<uint64_t>(properties.at("selectBits"));
        std::vector<PinTemplate> pins;
        for (uint64_t i = 0; i < selectBits; ++i) {
            pins.push_back(PinTemplate{"S" + std::to_string(i), core::PinDirection::Input});
        }
        for (uint64_t i = 0; i < (uint64_t{1} << selectBits); ++i) {
            pins.push_back(PinTemplate{"Y" + std::to_string(i), core::PinDirection::Output});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto selectBits = static_cast<std::size_t>(std::get<uint64_t>(properties.at("selectBits")));
        const std::vector<NetId> selectNets(pinNets.begin(), pinNets.begin() + static_cast<std::ptrdiff_t>(selectBits));
        const std::vector<NetId> outputNets(pinNets.begin() + static_cast<std::ptrdiff_t>(selectBits), pinNets.end());
        const std::vector<NetId> notSelectNets = negateEach(circuit, selectNets);

        uint32_t firstGateIndex = 0;
        for (std::size_t out = 0; out < outputNets.size(); ++out) {
            const std::vector<NetId> terms = selectTermsForCombination(selectNets, notSelectNets, out);
            const uint32_t gateIndex = driveNetFromTerms(circuit, GateType::And, terms, outputNets[out]);
            if (out == 0) {
                firstGateIndex = gateIndex;
            }
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeMultiplexerDefinition() {
    ComponentDefinition definition;
    definition.typeId = "plexers.multiplexer";
    definition.displayName = "Multiplexor";
    definition.description = "2^N lineas de datos + N bits de seleccion -> 1 salida (la linea de datos elegida).";
    definition.category = ComponentCategory::Plexers;
    definition.properties = {makeSelectBitsProperty(), makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(), makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        const auto selectBits = std::get<uint64_t>(properties.at("selectBits"));
        std::vector<PinTemplate> pins;
        for (uint64_t i = 0; i < (uint64_t{1} << selectBits); ++i) {
            pins.push_back(PinTemplate{"D" + std::to_string(i), core::PinDirection::Input});
        }
        for (uint64_t i = 0; i < selectBits; ++i) {
            pins.push_back(PinTemplate{"S" + std::to_string(i), core::PinDirection::Input});
        }
        pins.push_back(PinTemplate{"Y", core::PinDirection::Output});
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto selectBits = static_cast<std::size_t>(std::get<uint64_t>(properties.at("selectBits")));
        const std::size_t dataCount = std::size_t{1} << selectBits;
        const std::vector<NetId> dataNets(pinNets.begin(), pinNets.begin() + static_cast<std::ptrdiff_t>(dataCount));
        const std::vector<NetId> selectNets(pinNets.begin() + static_cast<std::ptrdiff_t>(dataCount),
                                             pinNets.begin() + static_cast<std::ptrdiff_t>(dataCount + selectBits));
        const NetId output = pinNets.back();
        const std::vector<NetId> notSelectNets = negateEach(circuit, selectNets);

        std::vector<NetId> terms(dataCount);
        for (std::size_t i = 0; i < dataCount; ++i) {
            std::vector<NetId> andInputs = selectTermsForCombination(selectNets, notSelectNets, i);
            andInputs.push_back(dataNets[i]);
            terms[i] = circuit.addNet();
            (void)circuit.addGate(GateType::And, andInputs, terms[i]);
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = driveNetFromTerms(circuit, GateType::Or, terms, output);
        return binding;
    };
    return definition;
}

ComponentDefinition makeDemultiplexerDefinition() {
    ComponentDefinition definition;
    definition.typeId = "plexers.demultiplexer";
    definition.displayName = "Demultiplexor";
    definition.description = "1 entrada de datos + N bits de seleccion -> 2^N salidas (la elegida repite el dato, "
                              "el resto queda en 0).";
    definition.category = ComponentCategory::Plexers;
    definition.properties = {makeSelectBitsProperty(), makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(), makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        const auto selectBits = std::get<uint64_t>(properties.at("selectBits"));
        std::vector<PinTemplate> pins{PinTemplate{"D", core::PinDirection::Input}};
        for (uint64_t i = 0; i < selectBits; ++i) {
            pins.push_back(PinTemplate{"S" + std::to_string(i), core::PinDirection::Input});
        }
        for (uint64_t i = 0; i < (uint64_t{1} << selectBits); ++i) {
            pins.push_back(PinTemplate{"Y" + std::to_string(i), core::PinDirection::Output});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto selectBits = static_cast<std::size_t>(std::get<uint64_t>(properties.at("selectBits")));
        const NetId data = pinNets[0];
        const std::vector<NetId> selectNets(pinNets.begin() + 1, pinNets.begin() + 1 + static_cast<std::ptrdiff_t>(selectBits));
        const std::vector<NetId> outputNets(pinNets.begin() + 1 + static_cast<std::ptrdiff_t>(selectBits), pinNets.end());
        const std::vector<NetId> notSelectNets = negateEach(circuit, selectNets);

        uint32_t firstGateIndex = 0;
        for (std::size_t out = 0; out < outputNets.size(); ++out) {
            std::vector<NetId> terms = selectTermsForCombination(selectNets, notSelectNets, out);
            terms.push_back(data);
            const uint32_t gateIndex = circuit.addGate(GateType::And, terms, outputNets[out]);
            if (out == 0) {
                firstGateIndex = gateIndex;
            }
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makePriorityEncoderDefinition() {
    ComponentDefinition definition;
    definition.typeId = "plexers.priorityEncoder";
    definition.displayName = "Codificador de prioridad";
    definition.description = "2^N entradas -> N bits de salida con el indice de la entrada activa de mayor "
                              "prioridad (la de indice mas alto), mas un pin 'valid' encendido si alguna entrada "
                              "esta activa.";
    definition.category = ComponentCategory::Plexers;
    definition.properties = {makeSelectBitsProperty(), makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(), makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        const auto selectBits = std::get<uint64_t>(properties.at("selectBits"));
        std::vector<PinTemplate> pins;
        for (uint64_t i = 0; i < (uint64_t{1} << selectBits); ++i) {
            pins.push_back(PinTemplate{"I" + std::to_string(i), core::PinDirection::Input});
        }
        for (uint64_t i = 0; i < selectBits; ++i) {
            pins.push_back(PinTemplate{"Y" + std::to_string(i), core::PinDirection::Output});
        }
        pins.push_back(PinTemplate{"valid", core::PinDirection::Output});
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto selectBits = static_cast<std::size_t>(std::get<uint64_t>(properties.at("selectBits")));
        const std::size_t inputCount = std::size_t{1} << selectBits;
        const std::vector<NetId> inputNets(pinNets.begin(), pinNets.begin() + static_cast<std::ptrdiff_t>(inputCount));
        const std::vector<NetId> outputNets(pinNets.begin() + static_cast<std::ptrdiff_t>(inputCount),
                                             pinNets.begin() + static_cast<std::ptrdiff_t>(inputCount + selectBits));
        const NetId validNet = pinNets.back();
        const std::vector<NetId> notInputNets = negateEach(circuit, inputNets);

        // select[i]: la entrada i es la de mayor prioridad activa (ella
        // misma activa y ninguna de indice mayor lo esta). La de indice mas
        // alto no necesita AND alguno: si esta activa, gana sin condiciones.
        std::vector<NetId> selectNets(inputCount);
        for (std::size_t i = 0; i < inputCount; ++i) {
            std::vector<NetId> terms{inputNets[i]};
            for (std::size_t higher = i + 1; higher < inputCount; ++higher) {
                terms.push_back(notInputNets[higher]);
            }
            if (terms.size() == 1) {
                selectNets[i] = inputNets[i];
            } else {
                selectNets[i] = circuit.addNet();
                (void)circuit.addGate(GateType::And, terms, selectNets[i]);
            }
        }

        uint32_t firstGateIndex = 0;
        for (std::size_t bit = 0; bit < selectBits; ++bit) {
            std::vector<NetId> terms;
            for (std::size_t i = 0; i < inputCount; ++i) {
                if (((i >> bit) & 1) != 0) {
                    terms.push_back(selectNets[i]);
                }
            }
            const uint32_t gateIndex = driveNetFromTerms(circuit, GateType::Or, terms, outputNets[bit]);
            if (bit == 0) {
                firstGateIndex = gateIndex;
            }
        }
        (void)circuit.addGate(GateType::Or, inputNets, validNet);

        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

// Compartida por sumador/restador/comparador: cuantos bits tiene cada
// operando (A/B); a diferencia de selectBits, aca los pines escalan
// linealmente con el ancho (2 pines de operando + 1 de salida por bit,
// aprox.), no como 2^N, por eso el rango sigue al de inputCount en vez de al
// de selectBits.
PropertyDescriptor makeBitsProperty() {
    return PropertyDescriptor{
        .id = "bits",
        .displayName = "Bits",
        .description = "Ancho en bits de cada operando (A y B).",
        .type = PropertyType::UnsignedInteger,
        .defaultValue = uint64_t{4},
        .minValue = uint64_t{1},
        .maxValue = uint64_t{64},
        .enumOptions = {},
        .affectsSimulation = true,
        .affectsAppearance = true,
    };
}

ComponentDefinition makeAdderDefinition() {
    ComponentDefinition definition;
    definition.typeId = "arithmetic.adder";
    definition.displayName = "Sumador";
    definition.description = "Sumador binario de N bits con acarreo de entrada (Cin) y de salida (Cout).";
    definition.category = ComponentCategory::Arithmetic;
    definition.properties = {makeBitsProperty(),        makeLabelProperty(), makeLabelRotationProperty(),      makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(), makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        const auto bits = std::get<uint64_t>(properties.at("bits"));
        std::vector<PinTemplate> pins;
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"A" + std::to_string(i), core::PinDirection::Input});
        }
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"B" + std::to_string(i), core::PinDirection::Input});
        }
        pins.push_back(PinTemplate{"Cin", core::PinDirection::Input});
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"Sum" + std::to_string(i), core::PinDirection::Output});
        }
        pins.push_back(PinTemplate{"Cout", core::PinDirection::Output});
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto bits = static_cast<std::size_t>(std::get<uint64_t>(properties.at("bits")));
        const std::vector<NetId> aNets(pinNets.begin(), pinNets.begin() + static_cast<std::ptrdiff_t>(bits));
        const std::vector<NetId> bNets(pinNets.begin() + static_cast<std::ptrdiff_t>(bits),
                                        pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits));
        const NetId cin = pinNets[2 * bits];
        const std::vector<NetId> sumNets(pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits + 1),
                                          pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits + 1 + bits));
        const NetId cout = pinNets.back();

        uint32_t firstGateIndex = 0;
        NetId carry = cin;
        for (std::size_t i = 0; i < bits; ++i) {
            const NetId p = circuit.addNet();
            const uint32_t gateIndex = circuit.addGate(GateType::Xor, std::vector<NetId>{aNets[i], bNets[i]}, p);
            if (i == 0) {
                firstGateIndex = gateIndex;
            }
            (void)circuit.addGate(GateType::Xor, std::vector<NetId>{p, carry}, sumNets[i]);
            const NetId g = circuit.addNet();
            (void)circuit.addGate(GateType::And, std::vector<NetId>{aNets[i], bNets[i]}, g);
            const NetId h = circuit.addNet();
            (void)circuit.addGate(GateType::And, std::vector<NetId>{p, carry}, h);
            const NetId nextCarry = (i + 1 == bits) ? cout : circuit.addNet();
            (void)circuit.addGate(GateType::Or, std::vector<NetId>{g, h}, nextCarry);
            carry = nextCarry;
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeSubtractorDefinition() {
    ComponentDefinition definition;
    definition.typeId = "arithmetic.subtractor";
    definition.displayName = "Restador";
    definition.description = "Restador binario de N bits (A - B) con prestamo de entrada (Bin) y de salida (Bout).";
    definition.category = ComponentCategory::Arithmetic;
    definition.properties = {makeBitsProperty(),        makeLabelProperty(), makeLabelRotationProperty(),      makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(), makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        const auto bits = std::get<uint64_t>(properties.at("bits"));
        std::vector<PinTemplate> pins;
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"A" + std::to_string(i), core::PinDirection::Input});
        }
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"B" + std::to_string(i), core::PinDirection::Input});
        }
        pins.push_back(PinTemplate{"Bin", core::PinDirection::Input});
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"Diff" + std::to_string(i), core::PinDirection::Output});
        }
        pins.push_back(PinTemplate{"Bout", core::PinDirection::Output});
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto bits = static_cast<std::size_t>(std::get<uint64_t>(properties.at("bits")));
        const std::vector<NetId> aNets(pinNets.begin(), pinNets.begin() + static_cast<std::ptrdiff_t>(bits));
        const std::vector<NetId> bNets(pinNets.begin() + static_cast<std::ptrdiff_t>(bits),
                                        pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits));
        const NetId bin = pinNets[2 * bits];
        const std::vector<NetId> diffNets(pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits + 1),
                                           pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits + 1 + bits));
        const NetId bout = pinNets.back();

        uint32_t firstGateIndex = 0;
        NetId borrow = bin;
        for (std::size_t i = 0; i < bits; ++i) {
            const NetId p = circuit.addNet();
            const uint32_t gateIndex = circuit.addGate(GateType::Xor, std::vector<NetId>{aNets[i], bNets[i]}, p);
            if (i == 0) {
                firstGateIndex = gateIndex;
            }
            (void)circuit.addGate(GateType::Xor, std::vector<NetId>{p, borrow}, diffNets[i]);
            const NetId notA = circuit.addNet();
            (void)circuit.addGate(GateType::Not, std::vector<NetId>{aNets[i]}, notA);
            const NetId t1 = circuit.addNet();
            (void)circuit.addGate(GateType::And, std::vector<NetId>{notA, bNets[i]}, t1);
            const NetId t2 = circuit.addNet();
            (void)circuit.addGate(GateType::And, std::vector<NetId>{notA, borrow}, t2);
            const NetId t3 = circuit.addNet();
            (void)circuit.addGate(GateType::And, std::vector<NetId>{bNets[i], borrow}, t3);
            const NetId nextBorrow = (i + 1 == bits) ? bout : circuit.addNet();
            (void)circuit.addGate(GateType::Or, std::vector<NetId>{t1, t2, t3}, nextBorrow);
            borrow = nextBorrow;
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeComparatorDefinition() {
    ComponentDefinition definition;
    definition.typeId = "arithmetic.comparator";
    definition.displayName = "Comparador";
    definition.description = "Comparador de magnitud de N bits sin signo: expone GT (A>B), EQ (A==B) y LT (A<B).";
    definition.category = ComponentCategory::Arithmetic;
    definition.properties = {makeBitsProperty(),        makeLabelProperty(), makeLabelRotationProperty(),      makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(), makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        const auto bits = std::get<uint64_t>(properties.at("bits"));
        std::vector<PinTemplate> pins;
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"A" + std::to_string(i), core::PinDirection::Input});
        }
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"B" + std::to_string(i), core::PinDirection::Input});
        }
        pins.push_back(PinTemplate{"GT", core::PinDirection::Output});
        pins.push_back(PinTemplate{"EQ", core::PinDirection::Output});
        pins.push_back(PinTemplate{"LT", core::PinDirection::Output});
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto bits = static_cast<std::size_t>(std::get<uint64_t>(properties.at("bits")));
        const std::vector<NetId> aNets(pinNets.begin(), pinNets.begin() + static_cast<std::ptrdiff_t>(bits));
        const std::vector<NetId> bNets(pinNets.begin() + static_cast<std::ptrdiff_t>(bits),
                                        pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits));
        const NetId gt = pinNets[2 * bits];
        const NetId eq = pinNets[2 * bits + 1];
        const NetId lt = pinNets[2 * bits + 2];
        const std::vector<NetId> notANets = negateEach(circuit, aNets);
        const std::vector<NetId> notBNets = negateEach(circuit, bNets);

        // eq_i (bit a bit, indice 0 = LSB); se recorre de MSB a LSB abajo.
        std::vector<NetId> eqNets(bits);
        uint32_t firstGateIndex = 0;
        for (std::size_t i = 0; i < bits; ++i) {
            eqNets[i] = circuit.addNet();
            const uint32_t gateIndex = circuit.addGate(GateType::Xnor, std::vector<NetId>{aNets[i], bNets[i]}, eqNets[i]);
            if (i == 0) {
                firstGateIndex = gateIndex;
            }
        }

        std::vector<NetId> gtTerms(bits);
        std::vector<NetId> ltTerms(bits);
        std::optional<NetId> runningEq;
        for (std::size_t idx = 0; idx < bits; ++idx) {
            const std::size_t i = bits - 1 - idx; // de MSB a LSB
            std::vector<NetId> gtInputs{aNets[i], notBNets[i]};
            std::vector<NetId> ltInputs{notANets[i], bNets[i]};
            if (runningEq.has_value()) {
                gtInputs.push_back(*runningEq);
                ltInputs.push_back(*runningEq);
            }
            gtTerms[i] = circuit.addNet();
            (void)circuit.addGate(GateType::And, gtInputs, gtTerms[i]);
            ltTerms[i] = circuit.addNet();
            (void)circuit.addGate(GateType::And, ltInputs, ltTerms[i]);

            if (i > 0) {
                if (runningEq.has_value()) {
                    const NetId nextRunningEq = circuit.addNet();
                    (void)circuit.addGate(GateType::And, std::vector<NetId>{eqNets[i], *runningEq}, nextRunningEq);
                    runningEq = nextRunningEq;
                } else {
                    runningEq = eqNets[i];
                }
            }
        }

        (void)driveNetFromTerms(circuit, GateType::Or, gtTerms, gt);
        (void)driveNetFromTerms(circuit, GateType::Or, ltTerms, lt);
        (void)driveNetFromTerms(circuit, GateType::And, eqNets, eq);

        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

// Compartida por memory.dFlipFlop/memory.jkFlipFlop/memory.tFlipFlop: si el
// tipo expone pines PRE/CLR asincronicos ademas de D(o J/K/T)/CLK. Apagada
// por defecto para no romper el simbolo simple ni el ancho de los proyectos
// ya guardados - ver derivePins de cada tipo, que solo agrega los pines
// cuando esta en true (siempre al FINAL de la lista, nunca en el medio: ver
// el comentario sobre pinIndex en CircuitDocument::setProperty()).
PropertyDescriptor makeAsyncPresetClearProperty() {
    return PropertyDescriptor{
        .id = "asyncPresetClear",
        .displayName = "Preset/Clear asincronos",
        .description = "Agrega pines PRE/CLR que fuerzan Q a 1/0 de inmediato, sin esperar un flanco de CLK "
                        "(y lo mantienen mientras esten activos). PRE y CLR activos a la vez es la combinacion "
                        "invalida clasica: Q y Q' quedan en error mientras dure.",
        .type = PropertyType::Boolean,
        .defaultValue = false,
        .minValue = std::nullopt,
        .maxValue = std::nullopt,
        .enumOptions = {},
        .affectsSimulation = true,
        .affectsAppearance = true,
    };
}

// Polaridad de PRE/CLR cuando estan presentes (ver makeAsyncPresetClearProperty
// arriba). "activeHigh" es mas facil de razonar (1 = activo, un pin sin
// conectar queda flotando en Z = inactivo) y es el default; "activeLow"
// imita la convencion real de los 74xx (ver makeDualDFlipFlopDefinition/
// makeDualJkFlipFlopDefinition, que la fijan sin exponer esta propiedad).
PropertyDescriptor makePresetClearPolarityProperty() {
    return PropertyDescriptor{
        .id = "presetClearPolarity",
        .displayName = "Polaridad de PRE/CLR",
        .description = "Si PRE/CLR se activan con 1 (activo en alto, mas simple) o con 0 (activo en bajo, como "
                        "en un chip 74xx real).",
        .type = PropertyType::Enum,
        .defaultValue = std::string("activeHigh"),
        .minValue = std::nullopt,
        .maxValue = std::nullopt,
        .enumOptions = {"activeHigh", "activeLow"},
        .affectsSimulation = true,
        .affectsAppearance = true,
    };
}

ComponentDefinition makeSrLatchDefinition() {
    ComponentDefinition definition;
    definition.typeId = "memory.srLatch";
    definition.displayName = "Latch SR";
    definition.description = "Latch SR por NOR cruzados: S=1 fija Q=1 (set), R=1 fija Q=0 (reset), S=R=0 "
                              "mantiene el ultimo estado. S=R=1 simultaneo es la combinacion invalida clasica "
                              "(Q=Q'=0 mientras dure).";
    definition.category = ComponentCategory::Memory;
    definition.appearanceVersion = 2; // paintMemory() ahora rotula pines/dibuja la burbuja de Q'/el punto de Q
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{
            PinTemplate{"S", core::PinDirection::Input},
            PinTemplate{"R", core::PinDirection::Input},
            PinTemplate{"Q ", core::PinDirection::Output},
            PinTemplate{"Q'", core::PinDirection::Output},
        };
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        const NetId s = pinNets[0];
        const NetId r = pinNets[1];
        const NetId q = pinNets[2];
        const NetId qn = pinNets[3];
        // Realimentacion cruzada: cada NOR lee la salida del otro, igual que
        // el acarreo del sumador encadena hacia el siguiente bit, pero aca
        // en ciclo en vez de en cadena - el simulador ya soporta ciclos de
        // gates sin ningun cambio (ver el test de oscilacion en
        // test_simulator.cpp).
        const uint32_t firstGateIndex = circuit.addGate(GateType::Nor, std::vector<NetId>{r, qn}, q);
        (void)circuit.addGate(GateType::Nor, std::vector<NetId>{s, q}, qn);
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

// Adapta la net de un pin PRE/CLR fisico a lo que GateType::DFlipFlop
// siempre espera (activo en alto, ver Simulator::step()). En "activeHigh" el
// pin ya esta en esa convencion: se devuelve tal cual (un pin sin conectar
// queda en Z = inactivo). En "activeLow" hace falta invertir - pero un NOT
// sobre un pin sin conectar (Z) daria Unknown y dejaria el biestable
// indeterminado para siempre, asi que primero se tira la net del pin a 1 con
// un GateType::WeakOne (mismo mecanismo que wiring.pullResistor: pierde
// limpio, sin conflicto, contra cualquier driver real que el usuario
// conecte) y recien despues se invierte.
NetId asyncControlNet(core::Circuit& circuit, NetId pinNet, bool activeLow) {
    if (!activeLow) {
        return pinNet;
    }
    (void)circuit.addGate(GateType::WeakOne, std::vector<NetId>{}, pinNet);
    const NetId inverted = circuit.addNet();
    (void)circuit.addGate(GateType::Not, std::vector<NetId>{pinNet}, inverted);
    return inverted;
}

// Adapta las dos nets PRE/CLR fisicas via asyncControlNet(). Compartida por
// los flip-flops genericos (memory.dFlipFlop/jkFlipFlop/tFlipFlop, donde
// `activeLow` sale de la propiedad "presetClearPolarity" - ver
// makePresetClearPolarityProperty) y por el 7474/7476 (donde `activeLow` va
// fijo en true, como el chip real, sin exponer esa propiedad).
std::pair<NetId, NetId> resolveAsyncPresetClear(core::Circuit& circuit, NetId preNet, NetId clrNet, bool activeLow) {
    return {asyncControlNet(circuit, preNet, activeLow), asyncControlNet(circuit, clrNet, activeLow)};
}

// D->Q en flanco ascendente de CLK + Q' = NOT Q, con PRE/CLR asincronicos
// opcionales (ya en la convencion activo-alto que espera el primitivo - ver
// asyncControlNet). Compartido por memory.dFlipFlop (un unico biestable),
// los dos biestables independientes de ic74ls.dualDFlipFlop (7474) y (vía
// addJkFlipFlopStage) memory.tFlipFlop. Devuelve el gateIndex del DFlipFlop
// (representativo para ComponentSimBinding).
uint32_t addDFlipFlopStage(core::Circuit& circuit, NetId d, NetId clk, NetId q, NetId qn,
                            std::optional<NetId> pre = std::nullopt, std::optional<NetId> clr = std::nullopt) {
    const uint32_t gateIndex = pre.has_value() && clr.has_value()
                                    ? circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d, clk, *pre, *clr}, q)
                                    : circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d, clk}, q);
    (void)circuit.addGate(GateType::Not, std::vector<NetId>{q}, qn);
    return gateIndex;
}

// Conversion JK -> D realimentando el propio Q (dEquiv = (J & !Q) | (!K &
// Q), igual que el comparador realimentaba runningEq) + Q' = NOT Q, con
// PRE/CLR asincronicos opcionales (idem addDFlipFlopStage). Al forzar Q, la
// realimentacion recalcula dEquiv sola: el proximo flanco de CLK arranca ya
// del estado forzado, sin nada mas que hacer. Compartido por
// memory.jkFlipFlop, los dos biestables independientes de
// ic74ls.dualJkFlipFlop (7476) y memory.tFlipFlop (T = JK con J=K=T).
// Devuelve el gateIndex del primer NOT (representativo para
// ComponentSimBinding).
uint32_t addJkFlipFlopStage(core::Circuit& circuit, NetId j, NetId k, NetId clk, NetId q, NetId qn,
                             std::optional<NetId> pre = std::nullopt, std::optional<NetId> clr = std::nullopt) {
    const NetId notQ = circuit.addNet();
    const uint32_t firstGateIndex = circuit.addGate(GateType::Not, std::vector<NetId>{q}, notQ);
    const NetId notK = circuit.addNet();
    (void)circuit.addGate(GateType::Not, std::vector<NetId>{k}, notK);
    const NetId term1 = circuit.addNet();
    (void)circuit.addGate(GateType::And, std::vector<NetId>{j, notQ}, term1);
    const NetId term2 = circuit.addNet();
    (void)circuit.addGate(GateType::And, std::vector<NetId>{notK, q}, term2);
    const NetId dEquiv = circuit.addNet();
    (void)circuit.addGate(GateType::Or, std::vector<NetId>{term1, term2}, dEquiv);
    if (pre.has_value() && clr.has_value()) {
        (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{dEquiv, clk, *pre, *clr}, q);
    } else {
        (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{dEquiv, clk}, q);
    }
    (void)circuit.addGate(GateType::Not, std::vector<NetId>{q}, qn);
    return firstGateIndex;
}

ComponentDefinition makeDFlipFlopDefinition() {
    ComponentDefinition definition;
    definition.typeId = "memory.dFlipFlop";
    definition.displayName = "Flip-Flop D";
    definition.description = "Biestable D disparado por flanco ascendente de CLK: en cada flanco 0->1 copia D a Q "
                              "y lo mantiene hasta el proximo flanco. Cambios en D con CLK quieto no afectan Q. "
                              "Con 'Preset/Clear asincronos' activado, PRE/CLR fuerzan Q a 1/0 de inmediato, sin "
                              "esperar un flanco.";
    definition.category = ComponentCategory::Memory;
    definition.definitionVersion = 2;
    definition.behaviorVersion = 2;
    definition.appearanceVersion = 2;
    definition.properties = {makeAsyncPresetClearProperty(),
                              makePresetClearPolarityProperty(),
                              makeLabelProperty(),
                              makeLabelRotationProperty(),
                              makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(),
                              makeCustomHeightProperty(),
                              makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        std::vector<PinTemplate> pins{
            PinTemplate{"D", core::PinDirection::Input},
            PinTemplate{"CLK", core::PinDirection::Input},
            PinTemplate{"Q ", core::PinDirection::Output},
            PinTemplate{"Q'", core::PinDirection::Output},
        };
        if (std::get<bool>(properties.at("asyncPresetClear"))) {
            pins.push_back(PinTemplate{"PRE", core::PinDirection::Input});
            pins.push_back(PinTemplate{"CLR", core::PinDirection::Input});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        if (std::get<bool>(properties.at("asyncPresetClear"))) {
            const bool activeLow = std::get<std::string>(properties.at("presetClearPolarity")) == "activeLow";
            const auto [pre, clr] = resolveAsyncPresetClear(circuit, pinNets[4], pinNets[5], activeLow);
            binding.gateIndex = addDFlipFlopStage(circuit, pinNets[0], pinNets[1], pinNets[2], pinNets[3], pre, clr);
        } else {
            binding.gateIndex = addDFlipFlopStage(circuit, pinNets[0], pinNets[1], pinNets[2], pinNets[3]);
        }
        return binding;
    };
    return definition;
}

ComponentDefinition makeJkFlipFlopDefinition() {
    ComponentDefinition definition;
    definition.typeId = "memory.jkFlipFlop";
    definition.displayName = "Flip-Flop JK";
    definition.description = "Biestable JK disparado por flanco ascendente de CLK: J=1,K=0 pone Q=1 (set); "
                              "J=0,K=1 pone Q=0 (reset); J=K=0 mantiene Q; J=K=1 invierte Q (toggle) en cada "
                              "flanco. Con 'Preset/Clear asincronos' activado, PRE/CLR fuerzan Q a 1/0 de "
                              "inmediato, sin esperar un flanco.";
    definition.category = ComponentCategory::Memory;
    definition.definitionVersion = 2;
    definition.behaviorVersion = 2;
    definition.appearanceVersion = 2;
    definition.properties = {makeAsyncPresetClearProperty(),
                              makePresetClearPolarityProperty(),
                              makeLabelProperty(),
                              makeLabelRotationProperty(),
                              makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(),
                              makeCustomHeightProperty(),
                              makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        std::vector<PinTemplate> pins{
            PinTemplate{"J", core::PinDirection::Input},
            PinTemplate{"K", core::PinDirection::Input},
            PinTemplate{"CLK", core::PinDirection::Input},
            PinTemplate{"Q ", core::PinDirection::Output},
            PinTemplate{"Q'", core::PinDirection::Output},
        };
        if (std::get<bool>(properties.at("asyncPresetClear"))) {
            pins.push_back(PinTemplate{"PRE", core::PinDirection::Input});
            pins.push_back(PinTemplate{"CLR", core::PinDirection::Input});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        if (std::get<bool>(properties.at("asyncPresetClear"))) {
            const bool activeLow = std::get<std::string>(properties.at("presetClearPolarity")) == "activeLow";
            const auto [pre, clr] = resolveAsyncPresetClear(circuit, pinNets[5], pinNets[6], activeLow);
            binding.gateIndex =
                addJkFlipFlopStage(circuit, pinNets[0], pinNets[1], pinNets[2], pinNets[3], pinNets[4], pre, clr);
        } else {
            binding.gateIndex =
                addJkFlipFlopStage(circuit, pinNets[0], pinNets[1], pinNets[2], pinNets[3], pinNets[4]);
        }
        return binding;
    };
    return definition;
}

ComponentDefinition makeTFlipFlopDefinition() {
    ComponentDefinition definition;
    definition.typeId = "memory.tFlipFlop";
    definition.displayName = "Flip-Flop T";
    definition.description = "Biestable T (toggle) disparado por flanco ascendente de CLK: T=1 invierte Q en "
                              "cada flanco; T=0 mantiene Q. Equivale a un JK con J=K=T - util para contadores "
                              "asincronicos y divisores de frecuencia. Con 'Preset/Clear asincronos' activado, "
                              "PRE/CLR fuerzan Q a 1/0 de inmediato, sin esperar un flanco.";
    definition.category = ComponentCategory::Memory;
    definition.properties = {makeAsyncPresetClearProperty(),
                              makePresetClearPolarityProperty(),
                              makeLabelProperty(),
                              makeLabelRotationProperty(),
                              makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(),
                              makeCustomHeightProperty(),
                              makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        std::vector<PinTemplate> pins{
            PinTemplate{"T", core::PinDirection::Input},
            PinTemplate{"CLK", core::PinDirection::Input},
            PinTemplate{"Q ", core::PinDirection::Output},
            PinTemplate{"Q'", core::PinDirection::Output},
        };
        if (std::get<bool>(properties.at("asyncPresetClear"))) {
            pins.push_back(PinTemplate{"PRE", core::PinDirection::Input});
            pins.push_back(PinTemplate{"CLR", core::PinDirection::Input});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        const NetId t = pinNets[0];
        const NetId clk = pinNets[1];
        const NetId q = pinNets[2];
        const NetId qn = pinNets[3];
        if (std::get<bool>(properties.at("asyncPresetClear"))) {
            const bool activeLow = std::get<std::string>(properties.at("presetClearPolarity")) == "activeLow";
            const auto [pre, clr] = resolveAsyncPresetClear(circuit, pinNets[4], pinNets[5], activeLow);
            binding.gateIndex = addJkFlipFlopStage(circuit, t, t, clk, q, qn, pre, clr);
        } else {
            binding.gateIndex = addJkFlipFlopStage(circuit, t, t, clk, q, qn);
        }
        return binding;
    };
    return definition;
}

ComponentDefinition makeRegisterDefinition() {
    ComponentDefinition definition;
    definition.typeId = "memory.register";
    definition.displayName = "Registro";
    definition.description = "Banco de N flip-flops D con un CLK compartido: en cada flanco ascendente de CLK "
                              "carga D0..D(bits-1) en Q0..Q(bits-1) simultaneamente.";
    definition.category = ComponentCategory::Memory;
    definition.appearanceVersion = 2; // paintMemory() ahora rotula pines/ancho 48px
    definition.properties = {makeBitsProperty(),        makeLabelProperty(), makeLabelRotationProperty(),      makeBodyColorProperty("#EBEBEB"),
                              makeCustomWidthProperty(), makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap& properties) {
        const auto bits = std::get<uint64_t>(properties.at("bits"));
        std::vector<PinTemplate> pins;
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"D" + std::to_string(i), core::PinDirection::Input});
        }
        pins.push_back(PinTemplate{"CLK", core::PinDirection::Input});
        for (uint64_t i = 0; i < bits; ++i) {
            pins.push_back(PinTemplate{"Q " + std::to_string(i), core::PinDirection::Output});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto bits = static_cast<std::size_t>(std::get<uint64_t>(properties.at("bits")));
        const std::vector<NetId> dNets(pinNets.begin(), pinNets.begin() + static_cast<std::ptrdiff_t>(bits));
        const NetId clk = pinNets[bits];
        const std::vector<NetId> qNets(pinNets.begin() + static_cast<std::ptrdiff_t>(bits + 1), pinNets.end());

        uint32_t firstGateIndex = 0;
        for (std::size_t i = 0; i < bits; ++i) {
            const uint32_t gateIndex =
                circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{dNets[i], clk}, qNets[i]);
            if (i == 0) {
                firstGateIndex = gateIndex;
            }
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeSubcircuitDefinition() {
    ComponentDefinition definition;
    definition.typeId = "structural.subcircuit";
    definition.displayName = "Subcircuito";
    definition.description = "Incrusta otro documento del proyecto como un componente: sus pines son los "
                              "wiring.input/wiring.output de ese documento, ordenados por posicion en el lienzo. "
                              "Sin documento valido en 'Documento' (o mientras el proyecto nunca se guardo), queda "
                              "sin pines.";
    definition.category = ComponentCategory::Subcircuits;
    definition.properties = {
        PropertyDescriptor{
            .id = "targetPath",
            .displayName = "Documento",
            .description = "Ruta relativa (dentro de la carpeta del proyecto) al documento .dfc que este "
                            "subcircuito representa.",
            .type = PropertyType::String,
            .defaultValue = std::string(""),
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        makeLabelProperty(), makeLabelRotationProperty(),
        makeBodyColorProperty("#EBEBEB"),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    // deriveExternalPins/buildExternalSimulation en vez de derivePins/
    // buildSimulation: este es el unico tipo que necesita leer *otro*
    // documento (sus wiring.input/output de frontera y su grafo interno)
    // para derivar pines/simular, algo que ninguna closure estatica de esta
    // biblioteca puede alcanzar por si sola (ver ExternalDocumentView.hpp).
    definition.deriveExternalPins = [](const PropertyMap& properties,
                                        const ExternalContext& context) -> std::vector<PinTemplate> {
        const auto& targetPath = std::get<std::string>(properties.at("targetPath"));
        if (!context.resolve || targetPath.empty()) {
            return {};
        }
        const ExternalDocumentView* view = context.resolve(targetPath);
        if (view == nullptr) {
            return {};
        }
        return view->boundaryPins();
    };
    definition.buildExternalSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                             const std::vector<NetId>& pinNets,
                                             const ExternalContext& context) -> ComponentSimBinding {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = 0;
        const auto& targetPath = std::get<std::string>(properties.at("targetPath"));
        if (!context.resolve || targetPath.empty()) {
            return binding;
        }
        const ExternalDocumentView* view = context.resolve(targetPath);
        if (view == nullptr) {
            return binding;
        }
        binding.gateIndex = view->flattenInto(circuit, pinNets);
        return binding;
    };
    return definition;
}

ComponentDefinition makeInputDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.input";
    definition.displayName = "Entrada";
    definition.description = "Pin de entrada digital controlado externamente.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {
        PropertyDescriptor{
            .id = "initialValue",
            .displayName = "Valor inicial",
            .description = "Valor mantenido hasta que el simulador controle explicitamente esta entrada.",
            .type = PropertyType::Enum,
            .defaultValue = std::string("Z"),
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {"0", "1", "Z", "X"},
            .affectsSimulation = true,
            .affectsAppearance = false,
        },
        makeLabelProperty(), makeLabelRotationProperty(),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"Y", core::PinDirection::Output}};
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(GateType::InputPin, std::vector<NetId>{}, pinNets[0]);
        return binding;
    };
    return definition;
}

// Practicamente identico a wiring.input (mismo GateType::InputPin/
// Kind::Driver, un unico pin de salida) - la diferencia real no esta aca
// sino en editor::CircuitDocument, el unico lugar de todo el proyecto con
// nocion de tiempo real de pared: un QTimer por instancia (ver
// CircuitDocument::rebuildClockTimers()/onClockTimeout()) alterna su valor
// cada medio "periodMs" mientras la simulacion esta en vivo, exactamente
// como si el usuario alternara un wiring.input a mano cada tanto. Esta
// ComponentDefinition en si es puramente sincronica, igual que las otras
// 45 - no sabe nada de QTimer ni de tiempo real.
ComponentDefinition makeClockDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.clock";
    definition.displayName = "Reloj";
    definition.description = "Genera una senal periodica alternando entre 0 y 1 mientras la simulacion esta en "
                              "vivo (Ejecutar) - en pausa se congela como cualquier otra propagacion.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {
        PropertyDescriptor{
            .id = "periodMs",
            .displayName = "Periodo (ms)",
            .description = "Tiempo de un ciclo completo 0->1->0, en milisegundos.",
            .type = PropertyType::UnsignedInteger,
            .defaultValue = uint64_t{1000},
            .minValue = uint64_t{20},
            .maxValue = uint64_t{60000},
            .enumOptions = {},
            // true: un cambio de periodo debe reconstruir el QTimer de
            // CircuitDocument con el nuevo intervalo (ver
            // rebuildClockTimers(), llamado desde rebuildSimulation()) -
            // mismo mecanismo que ya usan initialValue/variant/invertMask
            // para propiedades que no cambian pines pero si necesitan un
            // rebuild.
            .affectsSimulation = true,
            .affectsAppearance = false,
        },
        makeLabelProperty(), makeLabelRotationProperty(),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"CLK", core::PinDirection::Output}};
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(GateType::InputPin, std::vector<NetId>{}, pinNets[0]);
        return binding;
    };
    return definition;
}

ComponentDefinition makeOutputDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.output";
    definition.displayName = "Salida";
    definition.description = "Observa el valor de un net sin controlarlo.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeCustomWidthProperty(), makeCustomHeightProperty(),
                              makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"A", core::PinDirection::Input}};
    };
    definition.buildSimulation = [](core::Circuit&, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Sink;
        binding.observedNets = {pinNets[0]};
        return binding;
    };
    return definition;
}

ComponentDefinition makeConstantDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.constant";
    definition.displayName = "Constante";
    definition.description = "Fuente fija de logica-0 o logica-1.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {
        PropertyDescriptor{
            .id = "value",
            .displayName = "Valor",
            .description = "Valor fijo que esta fuente siempre controla.",
            .type = PropertyType::Enum,
            .defaultValue = std::string("0"),
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {"0", "1"},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        makeLabelProperty(), makeLabelRotationProperty(),
        makeBodyColorProperty("#EBEBEB"),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"Y", core::PinDirection::Output}};
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto& value = std::get<std::string>(properties.at("value"));
        const GateType type = (value == "1") ? GateType::ConstantOne : GateType::ConstantZero;
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(type, std::vector<NetId>{}, pinNets[0]);
        return binding;
    };
    return definition;
}

// A diferencia de wiring.constant, esta fuente es "debil" (GateType::
// WeakZero/WeakOne, ver isWeakType() en core/GateType.hpp): cualquier otro
// driver real en la misma net la pisa sin generar un conflicto (Error) -
// igual que una resistencia pull-up/pull-down real, que solo fija un nivel
// por defecto cuando la linea queda flotante.
ComponentDefinition makePullResistorDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.pullResistor";
    definition.displayName = "Resistencia pull-up/pull-down";
    definition.description = "Fija un valor por defecto (0 o 1) solo cuando ningun otro driver controla esta net "
                              "- cualquier otro driver real (una entrada, una compuerta, etc.) lo pisa sin generar "
                              "conflicto, igual que una resistencia pull-up/pull-down real.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {
        PropertyDescriptor{
            .id = "value",
            .displayName = "Valor",
            .description = "Hacia que nivel tira esta resistencia cuando la net queda flotante.",
            .type = PropertyType::Enum,
            .defaultValue = std::string("1"), // pull-up es el caso mas clasico/comun
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {"0", "1"},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        makeLabelProperty(), makeLabelRotationProperty(),
        makeBodyColorProperty("#EBEBEB"),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"Y", core::PinDirection::Output}};
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto& value = std::get<std::string>(properties.at("value"));
        const GateType type = (value == "1") ? GateType::WeakOne : GateType::WeakZero;
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(type, std::vector<NetId>{}, pinNets[0]);
        return binding;
    };
    return definition;
}

// Practicamente identico a wiring.clock (mismo GateType::InputPin/
// Kind::Driver, un unico pin de salida) - la diferencia real tampoco esta
// aca sino en editor::CircuitDocument: un QTimer *de un solo disparo* por
// instancia (ver CircuitDocument::rebuildPowerOnResetTimers()/
// onPowerOnResetTimeout()) que cae a 0 una unica vez, "pulseMs" despues de
// cada (re)construccion de la simulacion (que es exactamente el momento de
// "encender": tanto al abrir/recargar el proyecto como al presionar
// Reiniciar). Esta ComponentDefinition en si es puramente sincronica.
ComponentDefinition makePowerOnResetDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.powerOnReset";
    definition.displayName = "Reinicio al encender";
    definition.description = "Genera un pulso unico en 1 al (re)construir la simulacion (abrir el proyecto o "
                              "presionar Reiniciar), y cae a 0 una sola vez, pasado \"pulseMs\" - no se repite.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {
        PropertyDescriptor{
            .id = "pulseMs",
            .displayName = "Duracion del pulso (ms)",
            .description = "Tiempo en 1 antes de caer a 0, en milisegundos.",
            .type = PropertyType::UnsignedInteger,
            .defaultValue = uint64_t{100},
            .minValue = uint64_t{10},
            .maxValue = uint64_t{60000},
            .enumOptions = {},
            .affectsSimulation = true,
            .affectsAppearance = false,
        },
        makeLabelProperty(), makeLabelRotationProperty(),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"Y", core::PinDirection::Output}};
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(GateType::InputPin, std::vector<NetId>{}, pinNets[0]);
        return binding;
    };
    return definition;
}

// Puramente documental: marca un lugar donde intencionalmente no hay
// ninguna conexion. Sin pines (0 - ni siquiera ComponentItem::rebuildPins()
// necesita un caso especial, ningun loop ahi asume al menos un pin) y sin
// ninguna huella en la simulacion (ComponentSimBinding::Kind::None, el
// unico uso real de ese valor en todo el proyecto - ver el comentario en
// ComponentDefinition.hpp).
ComponentDefinition makeDoNotConnectDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.doNotConnect";
    definition.displayName = "No conectar";
    definition.description = "Marcador puramente documental: senala que este lugar queda intencionalmente sin "
                              "conexion. Sin pines, sin ninguna huella en la simulacion.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeCustomWidthProperty(), makeCustomHeightProperty(),
                              makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) { return std::vector<PinTemplate>{}; };
    definition.buildSimulation = [](core::Circuit&, const PropertyMap&, const std::vector<NetId>&) {
        return ComponentSimBinding{};
    };
    return definition;
}

// No es ni Driver ni Sink (ComponentSimBinding::Kind::None): la conexion
// real la hace CircuitDocument::rebuildSimulation(), agrupando por
// union-find todas las instancias wiring.tunnel que compartan la misma
// propiedad "label" - la net que le toque a este pin en bindPin() ya
// refleja ese grupo combinado, sin necesidad de ningun core::Gate propio.
// A diferencia de makeLabelProperty() (affectsSimulation=false en los
// otros 50 tipos), esta definicion arma su propio PropertyDescriptor
// "label" con affectsSimulation=true a proposito: cambiar el nombre de un
// tunel debe reconectarlo/desconectarlo al toque.
ComponentDefinition makeTunnelDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.tunnel";
    definition.displayName = "Tunel";
    definition.description = "Conecta dos o mas tuneles con la misma etiqueta a la misma net, sin necesidad de "
                              "dibujar un cable entre ellos - igual que en Logisim. Etiqueta vacia no conecta con "
                              "ningun otro tunel.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {
        PropertyDescriptor{
            .id = "label",
            .displayName = "Etiqueta",
            .description = "Nombre de este tunel: dos o mas wiring.tunnel con la misma etiqueta quedan "
                            "conectados entre si (misma net). Vacia = no conecta con ningun otro tunel.",
            .type = PropertyType::String,
            .defaultValue = std::string(""),
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"A", core::PinDirection::Output}};
    };
    definition.buildSimulation = [](core::Circuit&, const PropertyMap&, const std::vector<NetId>&) {
        return ComponentSimBinding{};
    };
    return definition;
}

// Fuente fija en 0, igual que wiring.constant con value="0" - pero sin esa
// propiedad (la tierra logica siempre es 0) y con el simbolo real de
// tierra en vez de un cuerpo generico. A diferencia de wiring.pullResistor,
// es un driver fuerte (GateType::ConstantZero): un corto real contra un 1
// es un conflicto electrico real, no debe resolverse en silencio como una
// resistencia.
ComponentDefinition makeGroundDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.ground";
    definition.displayName = "Tierra logica";
    definition.description = "Fuente fija de logica-0, con el simbolo real de tierra (IEC).";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeCustomWidthProperty(), makeCustomHeightProperty(),
                              makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"Y", core::PinDirection::Output}};
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(GateType::ConstantZero, std::vector<NetId>{}, pinNets[0]);
        return binding;
    };
    return definition;
}

// Logisim mismo simplifica su Transistor a un paso *unidireccional*
// condicionado por la compuerta (no simula corriente bidireccional real) -
// exactamente lo que ya hace GateType::TriStateBuffer ("si EN=1 repite D
// en Y, si no Y queda en alta impedancia"). Un NMOS simplificado ES un
// TriStateBuffer (EN = la senal de Gate tal cual); un PMOS es el mismo
// TriStateBuffer con esa senal invertida antes - misma idea que
// gates.tristateInverter invierte D antes del TriStateBuffer.
ComponentDefinition makeTransistorDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.transistor";
    definition.displayName = "Transistor";
    definition.description = "Interruptor NMOS/PMOS simplificado: repite Source en Drain solo cuando Gate lo "
                              "habilita (NMOS: Gate=1; PMOS: Gate=0) - no modela corriente bidireccional real, "
                              "igual simplificacion que usa Logisim.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {
        PropertyDescriptor{
            .id = "type",
            .displayName = "Tipo",
            .description = "NMOS: conduce con Gate=1. PMOS: conduce con Gate=0.",
            .type = PropertyType::Enum,
            .defaultValue = std::string("NMOS"),
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {"NMOS", "PMOS"},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        makeLabelProperty(), makeLabelRotationProperty(),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{
            PinTemplate{"Source", core::PinDirection::Input},
            PinTemplate{"Gate", core::PinDirection::Input},
            PinTemplate{"Drain", core::PinDirection::Output},
        };
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const bool isPmos = std::get<std::string>(properties.at("type")) == "PMOS";
        const NetId source = pinNets[0];
        const NetId gate = pinNets[1];
        const NetId drain = pinNets[2];
        NetId enable = gate;
        if (isPmos) {
            enable = circuit.addNet();
            (void)circuit.addGate(GateType::Not, std::vector<NetId>{gate}, enable);
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(GateType::TriStateBuffer, std::vector<NetId>{source, enable}, drain);
        return binding;
    };
    return definition;
}

// Par NMOS+PMOS en paralelo (mismo canal, compuertas complementarias N/P) -
// misma simplificacion unidireccional que wiring.transistor: solo pasa A a
// Y cuando las dos senales de control estan correctamente complementadas
// (N=1 y P=0); si estan mal cableadas (p. ej. las dos en 1), queda
// bloqueada en vez de ignorar directamente a P, un poco mas fiel al
// comportamiento real.
ComponentDefinition makeTransmissionGateDefinition() {
    ComponentDefinition definition;
    definition.typeId = "wiring.transmissionGate";
    definition.displayName = "Compuerta de transmision";
    definition.description = "Interruptor CMOS (NMOS+PMOS en paralelo): repite A en Y solo cuando N=1 y P=0 "
                              "(control complementario correcto) - no modela corriente bidireccional real.";
    definition.category = ComponentCategory::Wiring;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeCustomWidthProperty(), makeCustomHeightProperty(),
                              makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{
            PinTemplate{"A", core::PinDirection::Input},
            PinTemplate{"N", core::PinDirection::Input},
            PinTemplate{"P", core::PinDirection::Input},
            PinTemplate{"Y", core::PinDirection::Output},
        };
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        const NetId a = pinNets[0];
        const NetId n = pinNets[1];
        const NetId p = pinNets[2];
        const NetId y = pinNets[3];
        const NetId notP = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{p}, notP);
        const NetId enable = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{n, notP}, enable);
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = circuit.addGate(GateType::TriStateBuffer, std::vector<NetId>{a, enable}, y);
        return binding;
    };
    return definition;
}

ComponentDefinition makeLedDefinition() {
    ComponentDefinition definition;
    definition.typeId = "io.led";
    definition.displayName = "LED";
    definition.description = "Indicador visual del nivel logico de un net.";
    definition.category = ComponentCategory::IO;
    definition.properties = {
        PropertyDescriptor{
            .id = "activeHigh",
            .displayName = "Activo en alto",
            .description = "Si el LED se enciende con One (marcado) o con Zero (sin marcar).",
            .type = PropertyType::Boolean,
            .defaultValue = true,
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {},
            .affectsSimulation = false,
            .affectsAppearance = true,
        },
        makeColorOptionProperty(),
        makeLabelProperty(), makeLabelRotationProperty(),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"A", core::PinDirection::Input}};
    };
    definition.buildSimulation = [](core::Circuit&, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Sink;
        binding.observedNets = {pinNets[0]};
        return binding;
    };
    return definition;
}

ComponentDefinition makeProbeDefinition() {
    ComponentDefinition definition;
    definition.typeId = "debug.probe";
    definition.displayName = "Sonda logica";
    definition.description = "Sonda de diagnostico de solo lectura que informa el estado exacto de cinco valores de un net.";
    definition.category = ComponentCategory::Analysis;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{PinTemplate{"A", core::PinDirection::Input}};
    };
    definition.buildSimulation = [](core::Circuit&, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Sink;
        binding.observedNets = {pinNets[0]};
        return binding;
    };
    return definition;
}

ComponentDefinition makeSevenSegmentDefinition() {
    ComponentDefinition definition;
    definition.typeId = "io.seven_segment";
    definition.displayName = "Display 7 segmentos";
    definition.description = "Display de siete segmentos con segmentos a..g controlados de forma independiente y un punto decimal opcional.";
    definition.category = ComponentCategory::IO;
    definition.properties = {
        PropertyDescriptor{
            .id = "commonAnode",
            .displayName = "Anodo comun",
            .description = "Los displays de anodo comun encienden un segmento con Zero (activo en bajo); "
                            "los de catodo comun lo encienden con One (activo en alto).",
            .type = PropertyType::Boolean,
            .defaultValue = false,
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {},
            .affectsSimulation = false,
            .affectsAppearance = true,
        },
        PropertyDescriptor{
            .id = "hasDecimalPoint",
            .displayName = "Tiene punto decimal",
            .description = "Si se expone un pin de entrada extra 'dot' para el punto decimal.",
            .type = PropertyType::Boolean,
            .defaultValue = true,
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        makeLabelProperty(), makeLabelRotationProperty(),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap& properties) {
        std::vector<PinTemplate> pins{
            PinTemplate{"a", core::PinDirection::Input}, PinTemplate{"b", core::PinDirection::Input},
            PinTemplate{"c", core::PinDirection::Input}, PinTemplate{"d", core::PinDirection::Input},
            PinTemplate{"e", core::PinDirection::Input}, PinTemplate{"f", core::PinDirection::Input},
            PinTemplate{"g", core::PinDirection::Input},
        };
        if (std::get<bool>(properties.at("hasDecimalPoint"))) {
            pins.push_back(PinTemplate{"dot", core::PinDirection::Input});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit&, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Sink;
        binding.observedNets = pinNets;
        return binding;
    };
    return definition;
}

// A diferencia de io.seven_segment (7 entradas independientes sin
// decodificar), este decodifica internamente un valor binario de 4 bits a
// los 7 segmentos - por eso no tiene una propiedad "commonAnode": no hay una
// etapa de wiring real cuya polaridad importe, ya que este componente
// directamente informa que segmentos deben verse encendidos (ver
// hexDisplayState()).
ComponentDefinition makeHexDisplayDefinition() {
    ComponentDefinition definition;
    definition.typeId = "io.hexDisplay";
    definition.displayName = "Display hexadecimal";
    definition.description = "Display de 7 segmentos con decodificador hexadecimal interno: 4 bits de entrada "
                              "(0-15) se decodifican al digito 0-F correspondiente.";
    definition.category = ComponentCategory::IO;
    definition.properties = {
        PropertyDescriptor{
            .id = "hasDecimalPoint",
            .displayName = "Tiene punto decimal",
            .description = "Si se expone un pin de entrada extra 'dot' para el punto decimal (activo en alto).",
            .type = PropertyType::Boolean,
            .defaultValue = true,
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        PropertyDescriptor{
            .id = "bitOrder",
            .displayName = "Orden de bits",
            .description = "Orden fisico de los 4 pines de entrada: LSB primero (bit0 arriba, por defecto) o MSB "
                            "primero (bit3 arriba). No cambia el numero decodificado, solo que pin corresponde a "
                            "cada bit -- util para que el cableado quede prolijo segun de que lado vengan los bits.",
            .type = PropertyType::Enum,
            .defaultValue = std::string("lsbFirst"),
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {"lsbFirst", "msbFirst"},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        makeColorOptionProperty(),
        makeLabelProperty(), makeLabelRotationProperty(),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap& properties) {
        std::vector<PinTemplate> pins = hexDisplayIsMsbFirst(properties)
                                             ? std::vector<PinTemplate>{
                                                   PinTemplate{"bit3", core::PinDirection::Input},
                                                   PinTemplate{"bit2", core::PinDirection::Input},
                                                   PinTemplate{"bit1", core::PinDirection::Input},
                                                   PinTemplate{"bit0", core::PinDirection::Input},
                                               }
                                             : std::vector<PinTemplate>{
                                                   PinTemplate{"bit0", core::PinDirection::Input},
                                                   PinTemplate{"bit1", core::PinDirection::Input},
                                                   PinTemplate{"bit2", core::PinDirection::Input},
                                                   PinTemplate{"bit3", core::PinDirection::Input},
                                               };
        if (std::get<bool>(properties.at("hasDecimalPoint"))) {
            pins.push_back(PinTemplate{"dot", core::PinDirection::Input});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit&, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Sink;
        binding.observedNets = pinNets;
        return binding;
    };
    return definition;
}

ComponentDefinition makeLedMatrixDefinition() {
    ComponentDefinition definition;
    definition.typeId = "io.ledMatrix";
    definition.displayName = "Matriz LED";
    definition.description = "Grilla de LEDs, con un pin de entrada por celda (conexion directa) o con pines de "
                              "fila y columna como un panel multiplexado real.";
    definition.category = ComponentCategory::IO;
    definition.properties = {
        // Con conexion directa el tamano se limita a 8x8 (64 pines ya son
        // muchos de cablear a mano); multiplexada solo necesita filas+columnas
        // pines, asi que ahi tienen sentido paneles de hasta 32x32 - el limite
        // de cada propiedad es el mayor de los dos y derivePins() decide.
        PropertyDescriptor{
            .id = "rows",
            .displayName = "Filas",
            .description = "Cantidad de filas de la matriz.",
            .type = PropertyType::UnsignedInteger,
            .defaultValue = uint64_t{8},
            .minValue = uint64_t{1},
            .maxValue = uint64_t{32},
            .enumOptions = {},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        PropertyDescriptor{
            .id = "cols",
            .displayName = "Columnas",
            .description = "Cantidad de columnas de la matriz.",
            .type = PropertyType::UnsignedInteger,
            .defaultValue = uint64_t{8},
            .minValue = uint64_t{1},
            .maxValue = uint64_t{32},
            .enumOptions = {},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        PropertyDescriptor{
            .id = "wiring",
            .displayName = "Conexion",
            .description = "\"direct\": un pin de entrada por celda. \"multiplexed\": un pin por fila y uno por "
                            "columna (filas*columnas celdas con filas+columnas pines), como un panel real - cada "
                            "LED se enciende cuando su fila esta activa y su columna hunde corriente.",
            .type = PropertyType::Enum,
            .defaultValue = std::string("direct"),
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {"direct", "multiplexed"},
            .affectsSimulation = true,
            .affectsAppearance = true,
        },
        PropertyDescriptor{
            .id = "activeHigh",
            .displayName = "Activo en alto",
            .description = "Conexion directa: si cada LED se enciende con One (marcado) o con Zero (sin marcar). "
                            "Multiplexada: el nivel activo de las FILAS; las COLUMNAS son siempre el nivel "
                            "contrario, que es como funciona un panel real (la fila alimenta, la columna drena).",
            .type = PropertyType::Boolean,
            .defaultValue = true,
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {},
            .affectsSimulation = false,
            .affectsAppearance = true,
        },
        makeColorOptionProperty(),
        makeLabelProperty(), makeLabelRotationProperty(),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
    };
    definition.derivePins = [](const PropertyMap& properties) {
        const auto rows = std::get<uint64_t>(properties.at("rows"));
        const auto cols = std::get<uint64_t>(properties.at("cols"));
        std::vector<PinTemplate> pins;
        if (ledMatrixIsMultiplexed(properties)) {
            // Filas primero y columnas despues: el indice de pin de la fila r
            // es r, y el de la columna c es rows + c (ver
            // ledMatrixMultiplexedCellIsLit y el dibujo en ComponentItem).
            pins.reserve(rows + cols);
            for (uint64_t r = 0; r < rows; ++r) {
                pins.push_back(PinTemplate{"F" + std::to_string(r), core::PinDirection::Input});
            }
            for (uint64_t c = 0; c < cols; ++c) {
                pins.push_back(PinTemplate{"C" + std::to_string(c), core::PinDirection::Input});
            }
            return pins;
        }
        // Conexion directa: el tamano se recorta a 8x8 aunque las propiedades
        // admitan mas (ver el comentario de rows/cols) - un panel directo de
        // 32x32 serian 1024 pines.
        const uint64_t directRows = std::min<uint64_t>(rows, kLedMatrixMaxDirectSide);
        const uint64_t directCols = std::min<uint64_t>(cols, kLedMatrixMaxDirectSide);
        pins.reserve(directRows * directCols);
        for (uint64_t r = 0; r < directRows; ++r) {
            for (uint64_t c = 0; c < directCols; ++c) {
                pins.push_back(PinTemplate{"R" + std::to_string(r) + "C" + std::to_string(c),
                                            core::PinDirection::Input});
            }
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit&, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Sink;
        binding.observedNets = pinNets;
        return binding;
    };
    return definition;
}

// v1 sin estado: solo decodifica y muestra el caracter ASCII del valor
// actual del bus de 8 bits en cada paso - no hay scrollback/historial, ya
// que ComponentInstance/ComponentSimBinding son tipos de valor sin slot para
// estado persistente por instancia (ver docs/component-status.md).
ComponentDefinition makeTerminalDefinition() {
    ComponentDefinition definition;
    definition.typeId = "io.terminal";
    definition.displayName = "Terminal";
    definition.description = "Muestra el caracter ASCII correspondiente al byte actual de sus 8 entradas "
                              "(bit0 = LSB). Sin historial: solo el caracter actual.";
    definition.category = ComponentCategory::IO;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#EBEBEB"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        std::vector<PinTemplate> pins;
        pins.reserve(8);
        for (int i = 0; i < 8; ++i) {
            pins.push_back(PinTemplate{"bit" + std::to_string(i), core::PinDirection::Input});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit&, const PropertyMap&, const std::vector<NetId>& pinNets) {
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Sink;
        binding.observedNets = pinNets;
        return binding;
    };
    return definition;
}

// A diferencia de io.hexDisplay (que decodifica y *muestra* el digito en un
// unico componente), este es el driver en si: 4 entradas BCD (bit0..bit3,
// 0-9) -> 7 salidas a..g, sintetizado con logica real (igual patron que
// Plexers: un "detector de digito" AND de 4 entradas por cada valor 0-9,
// reutilizando negateEach()/selectTermsForCombination(), mas un OR por
// segmento sobre los digitos donde ese segmento debe encenderse segun
// hexDigitSegments()). Pensado para cablearse a un io.seven_segment real
// (sus salidas a..g calzan con los pines de entrada a..g de ese tipo) -
// equivalente logico de un 7447/7448 real. Solo cubre 0-9 (BCD): las
// combinaciones 10-15 no encienden ningun segmento (ningun digito 0-9 las
// cubre) - un BCD invalido no tiene un digito que mostrar, igual que en un
// 7447 real.
ComponentDefinition makeBcdDriverDefinition() {
    ComponentDefinition definition;
    definition.typeId = "ic74ls.bcdDriver";
    // "7447/7448 - ..." en vez de solo la descripcion, para que el nombre
    // siga el mismo patron "numero de parte - descripcion" que los otros
    // 13 ic74ls.* (p. ej. "7400 - NAND cuadruple (2 entradas)") - antes era
    // el unico sin numero de parte en el nombre visible.
    definition.displayName = "7447/7448 - Driver BCD a 7 segmentos";
    definition.description = "Decodifica un valor BCD de 4 bits (0-9) a las 7 salidas a..g de un display de 7 "
                              "segmentos. Combinaciones 10-15 (BCD invalido) no encienden ningun segmento.";
    // LT (lamp test)/RBI (ripple blanking in)/BI-RBO (blanking in/ripple
    // blanking out) del 7447/7448 real no se modelan (no hay una etapa de
    // "prueba de lampara"/"apagado en cascada" en este simulador) - solo
    // decorativos, para que el cuerpo tenga los 16 pines del DIP real.
    // Orden fisico real del DIP de 16 pines (pin1->16, B=bit1,C=bit2,
    // D=bit3,A=bit0 - numeracion BCD estandar): B,C,LT,BI/RBO,RBI,D,A,GND,
    // e,d,c,b,a,g,f,VCC (verificado contra el datasheet real).
    definition.physicalPinout = {
        {"bit1", true}, {"bit2", true}, {"LT", false}, {"BI/RBO", false}, {"RBI", false}, {"bit3", true},
        {"bit0", true}, {"GND", false}, {"e", true}, {"d", true}, {"c", true}, {"b", true},
        {"a", true}, {"g", true}, {"f", true}, {"VCC", false},
    };
    definition.category = ComponentCategory::Ic74LS;
    definition.properties = {
        PropertyDescriptor{
            .id = "variant",
            .displayName = "Variante",
            .description = "7447: salidas activas en bajo (para display de anodo comun, One = segmento apagado). "
                            "7448: salidas activas en alto (para display de catodo comun, One = segmento encendido).",
            .type = PropertyType::Enum,
            .defaultValue = std::string("7447"),
            .minValue = std::nullopt,
            .maxValue = std::nullopt,
            .enumOptions = {"7447", "7448"},
            .affectsSimulation = true,
            .affectsAppearance = false,
        },
        makeLabelProperty(), makeLabelRotationProperty(),
        // Cuerpo oscuro por defecto (plastico de un IC DIP real), a
        // diferencia de los bloques MSI/gates.* que usan el gris claro
        // generico - sigue siendo la propiedad "bodyColor" de siempre, asi
        // que el usuario puede cambiarlo igual que en cualquier otro tipo.
        makeBodyColorProperty("#2B2B2B"),
        makeCustomWidthProperty(),
        makeCustomHeightProperty(),
        makeNotesProperty(),
        makePropagationDelayProperty(),
    };
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{
            PinTemplate{"bit0", core::PinDirection::Input}, 
            PinTemplate{"bit1", core::PinDirection::Input},
            PinTemplate{"bit2", core::PinDirection::Input}, 
            PinTemplate{"bit3", core::PinDirection::Input},
            PinTemplate{"a", core::PinDirection::Output},   
            PinTemplate{"b", core::PinDirection::Output},
            PinTemplate{"c", core::PinDirection::Output},   
            PinTemplate{"d", core::PinDirection::Output},
            PinTemplate{"e", core::PinDirection::Output},   
            PinTemplate{"f", core::PinDirection::Output},
            PinTemplate{"g", core::PinDirection::Output},
        };
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const std::vector<NetId> bitNets(pinNets.begin(), pinNets.begin() + 4);
        const std::vector<NetId> segmentNets(pinNets.begin() + 4, pinNets.end());
        const auto delay = static_cast<uint16_t>(std::get<uint64_t>(properties.at("propagationDelay")));
        // 7447 real: salidas activas en bajo (pensado para un display de
        // anodo comun - One = segmento apagado). 7448: activas en alto
        // (catodo comun - One = segmento encendido, igual que la logica de
        // "encendido" ya usada en el resto del proyecto). Se sintetiza un
        // NOT extra por segmento para la variante 7447, mismo patron que
        // gates.tristateInverter sobre el gate compartido.
        const bool activeLow = std::get<std::string>(properties.at("variant")) == "7447";
        const std::vector<NetId> notBitNets = negateEach(circuit, bitNets);

        std::array<NetId, 10> digitTerms{};
        for (std::size_t digit = 0; digit < digitTerms.size(); ++digit) {
            const std::vector<NetId> terms = selectTermsForCombination(bitNets, notBitNets, digit);
            digitTerms[digit] = circuit.addNet();
            (void)circuit.addGate(GateType::And, terms, digitTerms[digit], delay);
        }

        uint32_t firstGateIndex = 0;
        for (std::size_t segment = 0; segment < segmentNets.size(); ++segment) {
            std::vector<NetId> litDigitTerms;
            for (std::size_t digit = 0; digit < digitTerms.size(); ++digit) {
                if (hexDigitSegments(static_cast<uint8_t>(digit))[segment]) {
                    litDigitTerms.push_back(digitTerms[digit]);
                }
            }
            const NetId orOutput = activeLow ? circuit.addNet() : segmentNets[segment];
            const uint32_t gateIndex = driveNetFromTerms(circuit, GateType::Or, litDigitTerms, orOutput);
            if (segment == 0) {
                firstGateIndex = gateIndex;
            }
            if (activeLow) {
                (void)circuit.addGate(GateType::Not, std::vector<NetId>{orOutput}, segmentNets[segment], delay);
            }
        }

        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

// --- Familia 74xx restante (ver docs/component-status.md, "Familia 74xx
// completa") --- Todos comparten: ComponentCategory::Ic74LS, bodyColor
// oscuro por defecto ("#2B2B2B", plastico de IC real, igual que
// bcdDriver), sin pines VCC/GND, y orden de pines "todas las entradas
// primero, despues todas las salidas" (igual que el resto del proyecto).

// Cuatro puertas de 2 entradas totalmente independientes en un mismo
// paquete (7400/7402/7408/7432/7486 reales) - a diferencia de gates.*
// (variadico, 2-64 entradas configurables + invertMask), un 74xx real es
// fijo: 4 puertas de exactamente 2 entradas cada una, sin negacion por
// entrada.
ComponentDefinition makeQuadGate2Definition(std::string typeId, std::string displayName, std::string description,
                                             std::string partNumber, GateType gateType) {
    ComponentDefinition definition;
    definition.typeId = std::move(typeId);
    definition.displayName = std::move(displayName);
    definition.description = std::move(description);
    definition.partNumber = std::move(partNumber);
    definition.category = ComponentCategory::Ic74LS;
    // Orden fisico real de un 74x00/02/08/32/86 (14 pines, pin1->14):
    // 1A,1B,1Y,2A,2B,2Y,GND,3Y,3B,3A,4Y,4B,4A,VCC - las dos primeras
    // puertas van entrada-entrada-salida, pero las dos ultimas van al
    // reves (salida-entrada-entrada): asimetria real del DIP, no un error.
    definition.physicalPinout = {
        {"A1", true}, {"B1", true}, {"Y1", true}, {"A2", true}, {"B2", true}, {"Y2", true}, {"GND", false},
        {"Y3", true}, {"B3", true}, {"A3", true}, {"Y4", true}, {"B4", true}, {"A4", true}, {"VCC", false},
    };
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#2B2B2B"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty(), makePropagationDelayProperty()};
    definition.derivePins = [](const PropertyMap&) {
        std::vector<PinTemplate> pins;
        for (int gate = 1; gate <= 4; ++gate) {
            pins.push_back(PinTemplate{"A" + std::to_string(gate), core::PinDirection::Input});
            pins.push_back(PinTemplate{"B" + std::to_string(gate), core::PinDirection::Input});
        }
        for (int gate = 1; gate <= 4; ++gate) {
            pins.push_back(PinTemplate{"Y" + std::to_string(gate), core::PinDirection::Output});
        }
        return pins;
    };
    definition.buildSimulation = [gateType](core::Circuit& circuit, const PropertyMap& properties,
                                             const std::vector<NetId>& pinNets) {
        const auto delay = static_cast<uint16_t>(std::get<uint64_t>(properties.at("propagationDelay")));
        uint32_t firstGateIndex = 0;
        for (int gate = 0; gate < 4; ++gate) {
            const NetId a = pinNets[static_cast<std::size_t>(gate * 2)];
            const NetId b = pinNets[static_cast<std::size_t>(gate * 2 + 1)];
            const NetId y = pinNets[static_cast<std::size_t>(8 + gate)];
            const uint32_t gateIndex = circuit.addGate(gateType, std::vector<NetId>{a, b}, y, delay);
            if (gate == 0) {
                firstGateIndex = gateIndex;
            }
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeHexInverterDefinition() {
    ComponentDefinition definition;
    definition.typeId = "ic74ls.hexInverter";
    definition.displayName = "7404 - Inversor hexuple";
    definition.partNumber = "7404";
    definition.description = "6 inversores independientes en un mismo paquete (7404 real).";
    definition.category = ComponentCategory::Ic74LS;
    // Orden fisico real (14 pines, pin1->14): 1A,1Y,2A,2Y,3A,3Y,GND,4Y,
    // 4A,5Y,5A,6Y,6A,VCC.
    definition.physicalPinout = {
        {"A1", true}, {"Y1", true}, {"A2", true}, {"Y2", true}, {"A3", true}, {"Y3", true}, {"GND", false},
        {"Y4", true}, {"A4", true}, {"Y5", true}, {"A5", true}, {"Y6", true}, {"A6", true}, {"VCC", false},
    };
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#2B2B2B"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty(), makePropagationDelayProperty()};
    definition.derivePins = [](const PropertyMap&) {
        std::vector<PinTemplate> pins;
        for (int i = 1; i <= 6; ++i) {
            pins.push_back(PinTemplate{"A" + std::to_string(i), core::PinDirection::Input});
        }
        for (int i = 1; i <= 6; ++i) {
            pins.push_back(PinTemplate{"Y" + std::to_string(i), core::PinDirection::Output});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap& properties,
                                     const std::vector<NetId>& pinNets) {
        const auto delay = static_cast<uint16_t>(std::get<uint64_t>(properties.at("propagationDelay")));
        uint32_t firstGateIndex = 0;
        for (int i = 0; i < 6; ++i) {
            const uint32_t gateIndex =
                circuit.addGate(GateType::Not, std::vector<NetId>{pinNets[static_cast<std::size_t>(i)]},
                                 pinNets[static_cast<std::size_t>(6 + i)], delay);
            if (i == 0) {
                firstGateIndex = gateIndex;
            }
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeDualDFlipFlopDefinition() {
    ComponentDefinition definition;
    definition.typeId = "ic74ls.dualDFlipFlop";
    definition.displayName = "7474 - Flip-Flop D dual";
    definition.partNumber = "7474";
    definition.description = "Dos biestables D independientes (cada uno disparado por flanco ascendente de su "
                              "propio CLK, con su propio PR/CLR asincronicos activos en bajo) en un mismo "
                              "paquete (7474 real).";
    definition.definitionVersion = 2;
    definition.behaviorVersion = 2;
    definition.appearanceVersion = 2;
    // Orden fisico real (14 pines, pin1->14): 1CLR,1D,1CLK,1PRE,1Q,1Qn,
    // GND,2Qn,2Q,2PRE,2CLK,2D,2CLR,VCC. PR/CLR ahora son funcionales
    // (activos en bajo, como el chip real - ver resolveAsyncPresetClear con
    // activeLow fijo mas abajo); GND/VCC siguen siendo decorativos.
    definition.physicalPinout = {
        {"CLR1", true}, {"D1", true}, {"CLK1", true}, {"PR1", true}, {"Q1", true}, {"Qn1", true},
        {"GND", false}, {"Qn2", true}, {"Q2", true}, {"PR2", true}, {"CLK2", true}, {"D2", true},
        {"CLR2", true}, {"VCC", false},
    };
    definition.category = ComponentCategory::Ic74LS;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#2B2B2B"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{
            PinTemplate{"D1", core::PinDirection::Input},  PinTemplate{"CLK1", core::PinDirection::Input},
            PinTemplate{"D2", core::PinDirection::Input},  PinTemplate{"CLK2", core::PinDirection::Input},
            PinTemplate{"Q1", core::PinDirection::Output}, PinTemplate{"Qn1", core::PinDirection::Output},
            PinTemplate{"Q2", core::PinDirection::Output}, PinTemplate{"Qn2", core::PinDirection::Output},
            // PR/CLR al final, no en el medio: no correr el indice de
            // ningun pin ya existente (ver CircuitDocument::setProperty(),
            // que invalida cables comparando indice contra cantidad de
            // pines - insertarlos en el medio recablearia Q/Q' en
            // silencio).
            PinTemplate{"PR1", core::PinDirection::Input}, PinTemplate{"CLR1", core::PinDirection::Input},
            PinTemplate{"PR2", core::PinDirection::Input}, PinTemplate{"CLR2", core::PinDirection::Input},
        };
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        const auto [pre1, clr1] = resolveAsyncPresetClear(circuit, pinNets[8], pinNets[9], /*activeLow=*/true);
        const auto [pre2, clr2] = resolveAsyncPresetClear(circuit, pinNets[10], pinNets[11], /*activeLow=*/true);
        const uint32_t firstGateIndex =
            addDFlipFlopStage(circuit, pinNets[0], pinNets[1], pinNets[4], pinNets[5], pre1, clr1);
        (void)addDFlipFlopStage(circuit, pinNets[2], pinNets[3], pinNets[6], pinNets[7], pre2, clr2);
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeDualJkFlipFlopDefinition() {
    ComponentDefinition definition;
    definition.typeId = "ic74ls.dualJkFlipFlop";
    definition.displayName = "7476 - Flip-Flop JK dual";
    definition.partNumber = "7476";
    definition.description = "Dos biestables JK independientes (cada uno disparado por flanco ascendente de su "
                              "propio CLK, con su propio PR/CLR asincronicos activos en bajo) en un mismo "
                              "paquete (7476 real).";
    definition.definitionVersion = 2;
    definition.behaviorVersion = 2;
    definition.appearanceVersion = 2;
    // Orden fisico real (16 pines, pin1->16): 1CLK,1PRE,1CLR,1J,VCC,2CLK,
    // 2PRE,2CLR,2J,2Qn,2Q,2K,GND,1Qn,1Q,1K. PR/CLR ahora son funcionales
    // (activos en bajo, como el chip real); VCC/GND siguen decorativos.
    definition.physicalPinout = {
        {"CLK1", true}, {"PR1", true}, {"CLR1", true}, {"J1", true}, {"VCC", false}, {"CLK2", true},
        {"PR2", true}, {"CLR2", true}, {"J2", true}, {"Qn2", true}, {"Q2", true}, {"K2", true},
        {"GND", false}, {"Qn1", true}, {"Q1", true}, {"K1", true},
    };
    definition.category = ComponentCategory::Ic74LS;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#2B2B2B"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{
            PinTemplate{"J1", core::PinDirection::Input},   PinTemplate{"K1", core::PinDirection::Input},
            PinTemplate{"CLK1", core::PinDirection::Input}, PinTemplate{"J2", core::PinDirection::Input},
            PinTemplate{"K2", core::PinDirection::Input},   PinTemplate{"CLK2", core::PinDirection::Input},
            PinTemplate{"Q1", core::PinDirection::Output},  PinTemplate{"Qn1", core::PinDirection::Output},
            PinTemplate{"Q2", core::PinDirection::Output},  PinTemplate{"Qn2", core::PinDirection::Output},
            // Al final, no en el medio - mismo motivo que en el 7474.
            PinTemplate{"PR1", core::PinDirection::Input},  PinTemplate{"CLR1", core::PinDirection::Input},
            PinTemplate{"PR2", core::PinDirection::Input},  PinTemplate{"CLR2", core::PinDirection::Input},
        };
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        const auto [pre1, clr1] = resolveAsyncPresetClear(circuit, pinNets[10], pinNets[11], /*activeLow=*/true);
        const auto [pre2, clr2] = resolveAsyncPresetClear(circuit, pinNets[12], pinNets[13], /*activeLow=*/true);
        const uint32_t firstGateIndex =
            addJkFlipFlopStage(circuit, pinNets[0], pinNets[1], pinNets[2], pinNets[6], pinNets[7], pre1, clr1);
        (void)addJkFlipFlopStage(circuit, pinNets[3], pinNets[4], pinNets[5], pinNets[8], pinNets[9], pre2, clr2);
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeDecadeCounterDefinition() {
    ComponentDefinition definition;
    definition.typeId = "ic74ls.decadeCounter";
    definition.displayName = "7490 - Contador decada";
    definition.partNumber = "7490";
    definition.description = "Contador BCD sincronico (cuenta 0-9 y envuelve a 0 en el flanco siguiente) con un "
                              "unico CLK y un CLR sincronico (fuerza 0 en el proximo flanco, no asincronico como "
                              "el 7490 real - el primitivo de flip-flop D de este simulador no tiene un tercer pin "
                              "de clear). No replica las etapas internas divide-por-2/divide-por-5 cascadeables ni "
                              "los pines R0(1)/R0(2)/R9(1)/R9(2) del 7490 real.";
    // CLKB (segundo reloj, para la etapa divide-por-5 independiente del
    // 7490 real) + R0(1)/R0(2)/R9(1)/R9(2) (reset a 0/reset a 9) + NC - solo
    // decorativos: este tipo simplifica todo eso a un unico CLK/CLR
    // sincronico (ver la descripcion de arriba). Nuestro CLR unico ocupa
    // la posicion fisica real de R0(1) (pin2); el resto de las senales de
    // reset del chip real queda decorativo.
    // Orden fisico real (14 pines, pin1->14): CKB,R0(1),R0(2),NC,VCC,
    // R9(1),R9(2),QC,QB,GND,QD,QA,NC,CKA.
    definition.physicalPinout = {
        {"CLKB", false}, {"CLR", true}, {"R0(2)", false}, {"NC", false}, {"VCC", false}, {"R9(1)", false},
        {"R9(2)", false}, {"Q2", true}, {"Q1", true}, {"GND", false}, {"Q3", true}, {"Q0", true},
        {"NC", false}, {"CLK", true},
    };
    definition.category = ComponentCategory::Ic74LS;
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#2B2B2B"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        return std::vector<PinTemplate>{
            PinTemplate{"CLK", core::PinDirection::Input}, PinTemplate{"CLR", core::PinDirection::Input},
            PinTemplate{"Q0", core::PinDirection::Output}, PinTemplate{"Q1", core::PinDirection::Output},
            PinTemplate{"Q2", core::PinDirection::Output}, PinTemplate{"Q3", core::PinDirection::Output},
        };
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        const NetId clk = pinNets[0];
        const NetId clr = pinNets[1];
        const NetId q0 = pinNets[2];
        const NetId q1 = pinNets[3];
        const NetId q2 = pinNets[4];
        const NetId q3 = pinNets[5];

        // Ecuaciones de "toggle" de un contador BCD sincronico estandar,
        // verificadas a mano contra el ciclo completo 0-9 (ver el plan/
        // docs/component-status.md): D0 = NOT(Q0) (T0 siempre en 1 - NOT ya
        // es Q0 XOR 1, no hace falta una puerta de toggle explicita).
        // D1 = Q1 XOR (Q0 AND NOT Q3). D2 = Q2 XOR (Q0 AND Q1).
        // D3 = Q3 XOR ((Q0 AND Q1 AND Q2) OR (Q0 AND Q3)).
        const NetId notQ3 = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{q3}, notQ3);
        const NetId t1 = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{q0, notQ3}, t1);
        const NetId t2 = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{q0, q1}, t2);
        const NetId t3a = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{q0, q1, q2}, t3a);
        const NetId t3b = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{q0, q3}, t3b);
        const NetId t3 = circuit.addNet();
        (void)circuit.addGate(GateType::Or, std::vector<NetId>{t3a, t3b}, t3);

        const NetId d0 = circuit.addNet();
        const uint32_t firstGateIndex = circuit.addGate(GateType::Not, std::vector<NetId>{q0}, d0);
        const NetId d1 = circuit.addNet();
        (void)circuit.addGate(GateType::Xor, std::vector<NetId>{q1, t1}, d1);
        const NetId d2 = circuit.addNet();
        (void)circuit.addGate(GateType::Xor, std::vector<NetId>{q2, t2}, d2);
        const NetId d3 = circuit.addNet();
        (void)circuit.addGate(GateType::Xor, std::vector<NetId>{q3, t3}, d3);

        // CLR sincronico: fuerza D=0 en los 4 bits (AND con NOT(CLR)) - se
        // aplica en el proximo flanco de CLK, no de inmediato.
        const NetId notClr = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{clr}, notClr);
        const NetId d0Final = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{d0, notClr}, d0Final);
        const NetId d1Final = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{d1, notClr}, d1Final);
        const NetId d2Final = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{d2, notClr}, d2Final);
        const NetId d3Final = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{d3, notClr}, d3Final);

        (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d0Final, clk}, q0);
        (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d1Final, clk}, q1);
        (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d2Final, clk}, q2);
        (void)circuit.addGate(GateType::DFlipFlop, std::vector<NetId>{d3Final, clk}, q3);

        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeMux8To1Definition() {
    ComponentDefinition definition;
    definition.typeId = "ic74ls.mux8to1";
    definition.displayName = "74151 - Multiplexor 8 a 1";
    definition.partNumber = "74151";
    definition.description = "8 lineas de datos + 3 bits de seleccion (S0..S2) -> Y (linea elegida) y W "
                              "(complemento). Strobe activo en bajo: en alto fuerza Y=0/W=1 sin importar los datos "
                              "(igual que el 74151 real).";
    definition.category = ComponentCategory::Ic74LS;
    // Orden fisico real (16 pines, pin1->16): D3,D2,D1,D0,Y,W,Strobe,GND,
    // C(S2),B(S1),A(S0),D7,D6,D5,D4,VCC.
    definition.physicalPinout = {
        {"D3", true}, {"D2", true}, {"D1", true}, {"D0", true}, {"Y", true}, {"W", true}, {"Strobe", true},
        {"GND", false}, {"S2", true}, {"S1", true}, {"S0", true}, {"D7", true}, {"D6", true}, {"D5", true},
        {"D4", true}, {"VCC", false},
    };
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#2B2B2B"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        std::vector<PinTemplate> pins;
        for (int i = 0; i < 8; ++i) {
            pins.push_back(PinTemplate{"D" + std::to_string(i), core::PinDirection::Input});
        }
        pins.push_back(PinTemplate{"S0", core::PinDirection::Input});
        pins.push_back(PinTemplate{"S1", core::PinDirection::Input});
        pins.push_back(PinTemplate{"S2", core::PinDirection::Input});
        pins.push_back(PinTemplate{"Strobe", core::PinDirection::Input});
        pins.push_back(PinTemplate{"Y", core::PinDirection::Output});
        pins.push_back(PinTemplate{"W", core::PinDirection::Output});
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        const std::vector<NetId> dataNets(pinNets.begin(), pinNets.begin() + 8);
        const std::vector<NetId> selectNets(pinNets.begin() + 8, pinNets.begin() + 11);
        const NetId strobe = pinNets[11];
        const NetId y = pinNets[12];
        const NetId w = pinNets[13];

        const std::vector<NetId> notSelectNets = negateEach(circuit, selectNets);
        std::vector<NetId> terms(dataNets.size());
        uint32_t firstGateIndex = 0;
        for (std::size_t i = 0; i < dataNets.size(); ++i) {
            std::vector<NetId> andInputs = selectTermsForCombination(selectNets, notSelectNets, i);
            andInputs.push_back(dataNets[i]);
            terms[i] = circuit.addNet();
            const uint32_t gateIndex = circuit.addGate(GateType::And, andInputs, terms[i]);
            if (i == 0) {
                firstGateIndex = gateIndex;
            }
        }
        const NetId muxOutput = circuit.addNet();
        (void)driveNetFromTerms(circuit, GateType::Or, terms, muxOutput);
        const NetId notStrobe = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{strobe}, notStrobe);
        (void)circuit.addGate(GateType::And, std::vector<NetId>{muxOutput, notStrobe}, y);
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{y}, w);

        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeDecoder3To8Definition() {
    ComponentDefinition definition;
    definition.typeId = "ic74ls.decoder3to8";
    definition.displayName = "74138 - Decodificador 3 a 8";
    definition.partNumber = "74138";
    definition.description = "3 bits de seleccion (A,B,C) -> 8 salidas activas en bajo (Y0..Y7), habilitadas por "
                              "G1 (activo en alto) y G2A/G2B (activos en bajo) - si no estan todos habilitados, "
                              "todas las salidas quedan en alto (igual que el 74138 real deshabilitado).";
    definition.category = ComponentCategory::Ic74LS;
    // Orden fisico real (16 pines, pin1->16): A,B,C,G2A,G2B,G1,Y7,GND,Y6,
    // Y5,Y4,Y3,Y2,Y1,Y0,VCC.
    definition.physicalPinout = {
        {"A", true}, {"B", true}, {"C", true}, {"G2A", true}, {"G2B", true}, {"G1", true}, {"Y7", true},
        {"GND", false}, {"Y6", true}, {"Y5", true}, {"Y4", true}, {"Y3", true}, {"Y2", true}, {"Y1", true},
        {"Y0", true}, {"VCC", false},
    };
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#2B2B2B"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        std::vector<PinTemplate> pins{
            PinTemplate{"A", core::PinDirection::Input},   PinTemplate{"B", core::PinDirection::Input},
            PinTemplate{"C", core::PinDirection::Input},   PinTemplate{"G1", core::PinDirection::Input},
            PinTemplate{"G2A", core::PinDirection::Input}, PinTemplate{"G2B", core::PinDirection::Input},
        };
        for (int i = 0; i < 8; ++i) {
            pins.push_back(PinTemplate{"Y" + std::to_string(i), core::PinDirection::Output});
        }
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        const std::vector<NetId> selectNets(pinNets.begin(), pinNets.begin() + 3);
        const NetId g1 = pinNets[3];
        const NetId g2a = pinNets[4];
        const NetId g2b = pinNets[5];
        const std::vector<NetId> outputNets(pinNets.begin() + 6, pinNets.end());

        const NetId notG2a = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{g2a}, notG2a);
        const NetId notG2b = circuit.addNet();
        (void)circuit.addGate(GateType::Not, std::vector<NetId>{g2b}, notG2b);
        const NetId enable = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{g1, notG2a, notG2b}, enable);

        const std::vector<NetId> notSelectNets = negateEach(circuit, selectNets);
        uint32_t firstGateIndex = 0;
        for (std::size_t out = 0; out < outputNets.size(); ++out) {
            std::vector<NetId> andInputs = selectTermsForCombination(selectNets, notSelectNets, out);
            andInputs.push_back(enable);
            const NetId andOutput = circuit.addNet();
            const uint32_t gateIndex = circuit.addGate(GateType::And, andInputs, andOutput);
            if (out == 0) {
                firstGateIndex = gateIndex;
            }
            (void)circuit.addGate(GateType::Not, std::vector<NetId>{andOutput}, outputNets[out]);
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeAdder4BitDefinition() {
    ComponentDefinition definition;
    definition.typeId = "ic74ls.adder4bit";
    definition.displayName = "7483 - Sumador 4 bits";
    definition.partNumber = "7483";
    definition.description = "Sumador binario de 4 bits con acarreo de entrada (Cin) y salida (Cout) - version IC "
                              "fija de arithmetic.adder (bits=4, sin la propiedad configurable).";
    definition.category = ComponentCategory::Ic74LS;
    // Orden fisico real (16 pines, pin1->16): A4,S3,A3,B3,VCC,S2,B2,A2,
    // S1,A1,B1,GND,C0,C4,S4,B4 (bits numerados 1-4 = MSB..LSB en el
    // datasheet real; se mapean a nuestros A0..A3/B0..B3/Sum0..Sum3 de
    // 0-indexado, A(n) real = nuestro A(n-1)).
    definition.physicalPinout = {
        {"A3", true}, {"Sum2", true}, {"A2", true}, {"B2", true}, {"VCC", false}, {"Sum1", true},
        {"B1", true}, {"A1", true}, {"Sum0", true}, {"A0", true}, {"B0", true}, {"GND", false},
        {"Cin", true}, {"Cout", true}, {"Sum3", true}, {"B3", true},
    };
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#2B2B2B"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        std::vector<PinTemplate> pins;
        for (int i = 0; i < 4; ++i) {
            pins.push_back(PinTemplate{"A" + std::to_string(i), core::PinDirection::Input});
        }
        for (int i = 0; i < 4; ++i) {
            pins.push_back(PinTemplate{"B" + std::to_string(i), core::PinDirection::Input});
        }
        pins.push_back(PinTemplate{"Cin", core::PinDirection::Input});
        for (int i = 0; i < 4; ++i) {
            pins.push_back(PinTemplate{"Sum" + std::to_string(i), core::PinDirection::Output});
        }
        pins.push_back(PinTemplate{"Cout", core::PinDirection::Output});
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        constexpr std::size_t bits = 4;
        const std::vector<NetId> aNets(pinNets.begin(), pinNets.begin() + static_cast<std::ptrdiff_t>(bits));
        const std::vector<NetId> bNets(pinNets.begin() + static_cast<std::ptrdiff_t>(bits),
                                        pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits));
        const NetId cin = pinNets[2 * bits];
        const std::vector<NetId> sumNets(pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits + 1),
                                          pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits + 1 + bits));
        const NetId cout = pinNets.back();

        uint32_t firstGateIndex = 0;
        NetId carry = cin;
        for (std::size_t i = 0; i < bits; ++i) {
            const NetId p = circuit.addNet();
            const uint32_t gateIndex = circuit.addGate(GateType::Xor, std::vector<NetId>{aNets[i], bNets[i]}, p);
            if (i == 0) {
                firstGateIndex = gateIndex;
            }
            (void)circuit.addGate(GateType::Xor, std::vector<NetId>{p, carry}, sumNets[i]);
            const NetId g = circuit.addNet();
            (void)circuit.addGate(GateType::And, std::vector<NetId>{aNets[i], bNets[i]}, g);
            const NetId h = circuit.addNet();
            (void)circuit.addGate(GateType::And, std::vector<NetId>{p, carry}, h);
            const NetId nextCarry = (i + 1 == bits) ? cout : circuit.addNet();
            (void)circuit.addGate(GateType::Or, std::vector<NetId>{g, h}, nextCarry);
            carry = nextCarry;
        }
        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

ComponentDefinition makeComparator4BitDefinition() {
    ComponentDefinition definition;
    definition.typeId = "ic74ls.comparator4bit";
    definition.displayName = "7485 - Comparador 4 bits";
    definition.partNumber = "7485";
    definition.description = "Comparador de magnitud de 4 bits sin signo con entradas de cascada (CasGT/CasEQ/"
                              "CasLT, igual que IA>B/IA=B/IA<B del 7485 real) para encadenar comparadores de mas "
                              "bits: si los 4 bits son iguales, el resultado lo deciden las entradas de cascada; si "
                              "no, lo decide la comparacion local. Para uso individual (sin cascada real) hay que "
                              "cablear CasEQ a una wiring.constant en 1 (y CasGT/CasLT en 0) - un pin de cascada "
                              "sin cablear queda flotante, igual que en el datasheet real.";
    definition.category = ComponentCategory::Ic74LS;
    // Orden fisico real (16 pines, pin1->16): B3,IA<B,IA=B,IA>B,OA>B,
    // OA=B,OA<B,GND,B0,A0,B1,A1,A2,B2,A3,VCC.
    definition.physicalPinout = {
        {"B3", true}, {"CasLT", true}, {"CasEQ", true}, {"CasGT", true}, {"GT", true}, {"EQ", true},
        {"LT", true}, {"GND", false}, {"B0", true}, {"A0", true}, {"B1", true}, {"A1", true},
        {"A2", true}, {"B2", true}, {"A3", true}, {"VCC", false},
    };
    definition.properties = {makeLabelProperty(), makeLabelRotationProperty(), makeBodyColorProperty("#2B2B2B"), makeCustomWidthProperty(),
                              makeCustomHeightProperty(), makeNotesProperty()};
    definition.derivePins = [](const PropertyMap&) {
        std::vector<PinTemplate> pins;
        for (int i = 0; i < 4; ++i) {
            pins.push_back(PinTemplate{"A" + std::to_string(i), core::PinDirection::Input});
        }
        for (int i = 0; i < 4; ++i) {
            pins.push_back(PinTemplate{"B" + std::to_string(i), core::PinDirection::Input});
        }
        pins.push_back(PinTemplate{"CasGT", core::PinDirection::Input});
        pins.push_back(PinTemplate{"CasEQ", core::PinDirection::Input});
        pins.push_back(PinTemplate{"CasLT", core::PinDirection::Input});
        pins.push_back(PinTemplate{"GT", core::PinDirection::Output});
        pins.push_back(PinTemplate{"EQ", core::PinDirection::Output});
        pins.push_back(PinTemplate{"LT", core::PinDirection::Output});
        return pins;
    };
    definition.buildSimulation = [](core::Circuit& circuit, const PropertyMap&, const std::vector<NetId>& pinNets) {
        constexpr std::size_t bits = 4;
        const std::vector<NetId> aNets(pinNets.begin(), pinNets.begin() + static_cast<std::ptrdiff_t>(bits));
        const std::vector<NetId> bNets(pinNets.begin() + static_cast<std::ptrdiff_t>(bits),
                                        pinNets.begin() + static_cast<std::ptrdiff_t>(2 * bits));
        const NetId casGt = pinNets[2 * bits];
        const NetId casEq = pinNets[2 * bits + 1];
        const NetId casLt = pinNets[2 * bits + 2];
        const NetId gt = pinNets[2 * bits + 3];
        const NetId eq = pinNets[2 * bits + 4];
        const NetId lt = pinNets[2 * bits + 5];

        const std::vector<NetId> notANets = negateEach(circuit, aNets);
        const std::vector<NetId> notBNets = negateEach(circuit, bNets);

        std::vector<NetId> eqNets(bits);
        uint32_t firstGateIndex = 0;
        for (std::size_t i = 0; i < bits; ++i) {
            eqNets[i] = circuit.addNet();
            const uint32_t gateIndex =
                circuit.addGate(GateType::Xnor, std::vector<NetId>{aNets[i], bNets[i]}, eqNets[i]);
            if (i == 0) {
                firstGateIndex = gateIndex;
            }
        }

        std::vector<NetId> gtTerms(bits);
        std::vector<NetId> ltTerms(bits);
        std::optional<NetId> runningEq;
        for (std::size_t idx = 0; idx < bits; ++idx) {
            const std::size_t i = bits - 1 - idx; // de MSB a LSB
            std::vector<NetId> gtInputs{aNets[i], notBNets[i]};
            std::vector<NetId> ltInputs{notANets[i], bNets[i]};
            if (runningEq.has_value()) {
                gtInputs.push_back(*runningEq);
                ltInputs.push_back(*runningEq);
            }
            gtTerms[i] = circuit.addNet();
            (void)circuit.addGate(GateType::And, gtInputs, gtTerms[i]);
            ltTerms[i] = circuit.addNet();
            (void)circuit.addGate(GateType::And, ltInputs, ltTerms[i]);

            if (i > 0) {
                if (runningEq.has_value()) {
                    const NetId nextRunningEq = circuit.addNet();
                    (void)circuit.addGate(GateType::And, std::vector<NetId>{eqNets[i], *runningEq}, nextRunningEq);
                    runningEq = nextRunningEq;
                } else {
                    runningEq = eqNets[i];
                }
            }
        }

        const NetId localGt = circuit.addNet();
        (void)driveNetFromTerms(circuit, GateType::Or, gtTerms, localGt);
        const NetId localLt = circuit.addNet();
        (void)driveNetFromTerms(circuit, GateType::Or, ltTerms, localLt);
        const NetId localEq = circuit.addNet();
        (void)driveNetFromTerms(circuit, GateType::And, eqNets, localEq);

        // Cascada real del 7485: si los 4 bits son iguales (localEq=1), el
        // resultado lo deciden las entradas de cascada; si no, la
        // comparacion local ya es definitiva.
        const NetId eqAndCasGt = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{localEq, casGt}, eqAndCasGt);
        (void)circuit.addGate(GateType::Or, std::vector<NetId>{localGt, eqAndCasGt}, gt);

        const NetId eqAndCasLt = circuit.addNet();
        (void)circuit.addGate(GateType::And, std::vector<NetId>{localEq, casLt}, eqAndCasLt);
        (void)circuit.addGate(GateType::Or, std::vector<NetId>{localLt, eqAndCasLt}, lt);

        (void)circuit.addGate(GateType::And, std::vector<NetId>{localEq, casEq}, eq);

        ComponentSimBinding binding;
        binding.kind = ComponentSimBinding::Kind::Driver;
        binding.gateIndex = firstGateIndex;
        return binding;
    };
    return definition;
}

} // namespace

void registerBasicComponentLibrary(ComponentRegistry& registry) {
    registry.registerDefinition(makeInputDefinition());
    registry.registerDefinition(makeClockDefinition());
    registry.registerDefinition(makeOutputDefinition());
    registry.registerDefinition(makeConstantDefinition());
    registry.registerDefinition(makePullResistorDefinition());
    registry.registerDefinition(makePowerOnResetDefinition());
    registry.registerDefinition(makeDoNotConnectDefinition());
    registry.registerDefinition(makeTunnelDefinition());
    registry.registerDefinition(makeGroundDefinition());
    registry.registerDefinition(makeTransistorDefinition());
    registry.registerDefinition(makeTransmissionGateDefinition());
    registry.registerDefinition(makeVariadicGateDefinition("gates.and", "AND", GateType::And));
    registry.registerDefinition(makeVariadicGateDefinition("gates.or", "OR", GateType::Or));
    registry.registerDefinition(makeNotDefinition());
    registry.registerDefinition(makeBufferDefinition());
    registry.registerDefinition(makeTriStateBufferDefinition());
    registry.registerDefinition(makeTriStateInverterDefinition());
    registry.registerDefinition(makeVariadicGateDefinition("gates.nand", "NAND", GateType::Nand));
    registry.registerDefinition(makeVariadicGateDefinition("gates.nor", "NOR", GateType::Nor));
    registry.registerDefinition(makeVariadicGateDefinition("gates.xor", "XOR", GateType::Xor));
    registry.registerDefinition(makeVariadicGateDefinition("gates.xnor", "XNOR", GateType::Xnor));
    registry.registerDefinition(makeDecoderDefinition());
    registry.registerDefinition(makeMultiplexerDefinition());
    registry.registerDefinition(makeDemultiplexerDefinition());
    registry.registerDefinition(makePriorityEncoderDefinition());
    registry.registerDefinition(makeAdderDefinition());
    registry.registerDefinition(makeSubtractorDefinition());
    registry.registerDefinition(makeComparatorDefinition());
    registry.registerDefinition(makeSrLatchDefinition());
    registry.registerDefinition(makeDFlipFlopDefinition());
    registry.registerDefinition(makeJkFlipFlopDefinition());
    registry.registerDefinition(makeTFlipFlopDefinition());
    registry.registerDefinition(makeRegisterDefinition());
    registry.registerDefinition(makeSubcircuitDefinition());
    registry.registerDefinition(makeLedDefinition());
    registry.registerDefinition(makeProbeDefinition());
    registry.registerDefinition(makeSevenSegmentDefinition());
    registry.registerDefinition(makeHexDisplayDefinition());
    registry.registerDefinition(makeLedMatrixDefinition());
    registry.registerDefinition(makeTerminalDefinition());
    registry.registerDefinition(makeBcdDriverDefinition());
    registry.registerDefinition(
        makeQuadGate2Definition("ic74ls.quadNand2", "7400 - NAND cuadruple (2 entradas)",
                                 "4 puertas NAND de 2 entradas totalmente independientes.", "7400", GateType::Nand));
    registry.registerDefinition(
        makeQuadGate2Definition("ic74ls.quadNor2", "7402 - NOR cuadruple (2 entradas)",
                                 "4 puertas NOR de 2 entradas totalmente independientes.", "7402", GateType::Nor));
    registry.registerDefinition(
        makeQuadGate2Definition("ic74ls.quadAnd2", "7408 - AND cuadruple (2 entradas)",
                                 "4 puertas AND de 2 entradas totalmente independientes.", "7408", GateType::And));
    registry.registerDefinition(
        makeQuadGate2Definition("ic74ls.quadOr2", "7432 - OR cuadruple (2 entradas)",
                                 "4 puertas OR de 2 entradas totalmente independientes.", "7432", GateType::Or));
    registry.registerDefinition(
        makeQuadGate2Definition("ic74ls.quadXor2", "7486 - XOR cuadruple (2 entradas)",
                                 "4 puertas XOR de 2 entradas totalmente independientes.", "7486", GateType::Xor));
    registry.registerDefinition(makeHexInverterDefinition());
    registry.registerDefinition(makeDualDFlipFlopDefinition());
    registry.registerDefinition(makeDualJkFlipFlopDefinition());
    registry.registerDefinition(makeDecadeCounterDefinition());
    registry.registerDefinition(makeMux8To1Definition());
    registry.registerDefinition(makeDecoder3To8Definition());
    registry.registerDefinition(makeAdder4BitDefinition());
    registry.registerDefinition(makeComparator4BitDefinition());
}

LogicValue parseLogicValueEnum(const std::string& text) {
    if (text == "0") return LogicValue::Zero;
    if (text == "1") return LogicValue::One;
    if (text == "Z") return LogicValue::HighImpedance;
    if (text == "X") return LogicValue::Unknown;
    throw std::invalid_argument("parseLogicValueEnum: invalid literal '" + text + "'");
}

LogicValue inputInitialValue(const ComponentInstance& input) {
    if (input.typeId() != "wiring.input") {
        throw std::invalid_argument("inputInitialValue: instance is not a wiring.input");
    }
    return parseLogicValueEnum(std::get<std::string>(input.property("initialValue")));
}

bool ledIsLit(const ComponentInstance& led, LogicValue netValue) {
    if (led.typeId() != "io.led") {
        throw std::invalid_argument("ledIsLit: instance is not an io.led");
    }
    if (netValue != LogicValue::Zero && netValue != LogicValue::One) {
        return false;
    }
    const bool activeHigh = std::get<bool>(led.property("activeHigh"));
    return activeHigh ? (netValue == LogicValue::One) : (netValue == LogicValue::Zero);
}

bool segmentIsLit(const ComponentInstance& display, LogicValue netValue) {
    if (display.typeId() != "io.seven_segment") {
        throw std::invalid_argument("segmentIsLit: instance is not an io.seven_segment");
    }
    if (netValue != LogicValue::Zero && netValue != LogicValue::One) {
        return false;
    }
    const bool commonAnode = std::get<bool>(display.property("commonAnode"));
    return commonAnode ? (netValue == LogicValue::Zero) : (netValue == LogicValue::One);
}

std::array<bool, 7> hexDigitSegments(uint8_t value) {
    // Orden a,b,c,d,e,f,g. Codificacion estandar de 7 segmentos para 0-F.
    static constexpr std::array<std::array<bool, 7>, 16> kTable{{
        {true, true, true, true, true, true, false},     // 0
        {false, true, true, false, false, false, false}, // 1
        {true, true, false, true, true, false, true},    // 2
        {true, true, true, true, false, false, true},    // 3
        {false, true, true, false, false, true, true},   // 4
        {true, false, true, true, false, true, true},    // 5
        {true, false, true, true, true, true, true},     // 6
        {true, true, true, false, false, false, false},  // 7
        {true, true, true, true, true, true, true},      // 8
        {true, true, true, true, false, true, true},     // 9
        {true, true, true, false, true, true, true},     // A
        {false, false, true, true, true, true, true},    // b
        {true, false, false, true, true, true, false},   // C
        {false, true, true, true, true, false, true},    // d
        {true, false, false, true, true, true, true},    // E
        {true, false, false, false, true, true, true},   // F
    }};
    if (value > 15) {
        throw std::invalid_argument("hexDigitSegments: value must be 0-15");
    }
    return kTable[value];
}

HexDisplayState hexDisplayState(const ComponentInstance& display, LogicValue bit0, LogicValue bit1, LogicValue bit2,
                                 LogicValue bit3) {
    if (display.typeId() != "io.hexDisplay") {
        throw std::invalid_argument("hexDisplayState: instance is not an io.hexDisplay");
    }
    const std::array<LogicValue, 4> bits{bit0, bit1, bit2, bit3};
    for (const LogicValue bit : bits) {
        if (bit != LogicValue::Zero && bit != LogicValue::One) {
            return HexDisplayState{.valid = false, .hasConflict = bit == LogicValue::Error};
        }
    }
    uint8_t value = 0;
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (bits[i] == LogicValue::One) {
            value = static_cast<uint8_t>(value | (uint8_t{1} << i));
        }
    }
    return HexDisplayState{.valid = true, .segments = hexDigitSegments(value)};
}

bool hexDisplayIsMsbFirst(const PropertyMap& properties) {
    const auto it = properties.find("bitOrder");
    if (it == properties.end()) {
        return false; // proyecto guardado antes de que existiera la propiedad
    }
    const auto* order = std::get_if<std::string>(&it->second);
    return order != nullptr && *order == "msbFirst";
}

bool hexDisplayIsMsbFirst(const ComponentInstance& display) {
    if (display.typeId() != "io.hexDisplay") {
        return false;
    }
    const auto* order = std::get_if<std::string>(&display.property("bitOrder"));
    return order != nullptr && *order == "msbFirst";
}

bool ledMatrixIsMultiplexed(const PropertyMap& properties) {
    const auto it = properties.find("wiring");
    if (it == properties.end()) {
        return false; // proyecto guardado antes de que existiera la propiedad
    }
    const auto* mode = std::get_if<std::string>(&it->second);
    return mode != nullptr && *mode == "multiplexed";
}

bool ledMatrixIsMultiplexed(const ComponentInstance& matrix) {
    if (matrix.typeId() != "io.ledMatrix") {
        return false;
    }
    const auto* mode = std::get_if<std::string>(&matrix.property("wiring"));
    return mode != nullptr && *mode == "multiplexed";
}

bool ledMatrixCellIsLit(const ComponentInstance& matrix, LogicValue netValue) {
    if (matrix.typeId() != "io.ledMatrix") {
        throw std::invalid_argument("ledMatrixCellIsLit: instance is not an io.ledMatrix");
    }
    if (netValue != LogicValue::Zero && netValue != LogicValue::One) {
        return false;
    }
    const bool activeHigh = std::get<bool>(matrix.property("activeHigh"));
    return activeHigh ? (netValue == LogicValue::One) : (netValue == LogicValue::Zero);
}

bool ledMatrixMultiplexedCellIsLit(const ComponentInstance& matrix, LogicValue rowValue, LogicValue colValue) {
    if (matrix.typeId() != "io.ledMatrix") {
        throw std::invalid_argument("ledMatrixMultiplexedCellIsLit: instance is not an io.ledMatrix");
    }
    const bool cleanLevels = (rowValue == LogicValue::Zero || rowValue == LogicValue::One) &&
                              (colValue == LogicValue::Zero || colValue == LogicValue::One);
    if (!cleanLevels) {
        return false;
    }
    // La fila alimenta en su nivel activo y la columna drena en el contrario:
    // solo esa combinacion cierra el circuito del LED, igual que en el panel
    // real (por eso alcanza con una unica propiedad de polaridad).
    const bool activeHigh = std::get<bool>(matrix.property("activeHigh"));
    const LogicValue rowActive = activeHigh ? LogicValue::One : LogicValue::Zero;
    const LogicValue colActive = activeHigh ? LogicValue::Zero : LogicValue::One;
    return rowValue == rowActive && colValue == colActive;
}

std::optional<char> terminalCharacter(const ComponentInstance& terminal, std::span<const LogicValue> bits) {
    if (terminal.typeId() != "io.terminal") {
        throw std::invalid_argument("terminalCharacter: instance is not an io.terminal");
    }
    if (bits.size() != 8) {
        throw std::invalid_argument("terminalCharacter: expects exactly 8 bits");
    }
    uint8_t value = 0;
    for (std::size_t i = 0; i < bits.size(); ++i) {
        const LogicValue bit = bits[i];
        if (bit != LogicValue::Zero && bit != LogicValue::One) {
            return std::nullopt;
        }
        if (bit == LogicValue::One) {
            value = static_cast<uint8_t>(value | (uint8_t{1} << i));
        }
    }
    return static_cast<char>(value);
}

} // namespace digitalforge::components
