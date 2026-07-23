#include "ComponentFingerprints.hpp"

#include <algorithm>
#include <vector>

#include "ComponentInstance.hpp"

namespace digitalforge::components {

using core::Hash256;
using core::Sha256Builder;

namespace {

// Serializa un PropertyValue de forma canonica, incluyendo cual de las
// alternativas del variant es: sin la etiqueta de tipo, el entero 1 y la
// cadena "1" (o el booleano true) darian la misma huella.
void hashPropertyValue(Sha256Builder& builder, const PropertyValue& value) {
    if (const auto* boolean = std::get_if<bool>(&value)) {
        builder.field(std::string_view("bool")).field(*boolean);
    } else if (const auto* integer = std::get_if<int64_t>(&value)) {
        builder.field(std::string_view("int")).field(std::to_string(*integer));
    } else if (const auto* unsignedInteger = std::get_if<uint64_t>(&value)) {
        builder.field(std::string_view("uint")).field(*unsignedInteger);
    } else if (const auto* text = std::get_if<std::string>(&value)) {
        builder.field(std::string_view("string")).field(*text);
    }
}

void hashOptionalPropertyValue(Sha256Builder& builder, const std::optional<PropertyValue>& value) {
    if (!value.has_value()) {
        builder.field(std::string_view("none"));
        return;
    }
    builder.field(std::string_view("some"));
    hashPropertyValue(builder, *value);
}

// Contrato de pines: clave y direccion, ordenados por clave. El indice de
// almacenamiento queda deliberadamente afuera - reordenar los pines sin
// cambiar sus claves no es un cambio de interfaz, y a la inversa, reconectar
// por indice es justamente lo que hay que evitar.
Hash256 hashPins(const std::vector<PinTemplate>& pins) {
    std::vector<const PinTemplate*> sorted;
    sorted.reserve(pins.size());
    for (const PinTemplate& pin : pins) {
        sorted.push_back(&pin);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const PinTemplate* a, const PinTemplate* b) { return a->name < b->name; });

    Sha256Builder builder;
    builder.field(std::string_view("pins.v1")).field(static_cast<uint64_t>(sorted.size()));
    for (const PinTemplate* pin : sorted) {
        // PinTemplate::name es la clave persistente del pin (no hay un campo
        // `key` aparte): es estable, significativa y ya se usa como identidad
        // en toda la libreria. Renombrar un pin es, correctamente, un cambio
        // de interfaz que exige alias o migracion.
        builder.field(pin->name).field(static_cast<uint64_t>(pin->direction));
    }
    return builder.finish();
}

Hash256 hashProperties(const std::vector<PropertyDescriptor>& properties) {
    std::vector<const PropertyDescriptor*> sorted;
    sorted.reserve(properties.size());
    for (const PropertyDescriptor& descriptor : properties) {
        sorted.push_back(&descriptor);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const PropertyDescriptor* a, const PropertyDescriptor* b) { return a->id < b->id; });

    Sha256Builder builder;
    builder.field(std::string_view("properties.v1")).field(static_cast<uint64_t>(sorted.size()));
    for (const PropertyDescriptor* descriptor : sorted) {
        builder.field(descriptor->id).field(static_cast<uint64_t>(descriptor->type));
        hashPropertyValue(builder, descriptor->defaultValue);
        hashOptionalPropertyValue(builder, descriptor->minValue);
        hashOptionalPropertyValue(builder, descriptor->maxValue);
        builder.field(static_cast<uint64_t>(descriptor->enumOptions.size()));
        for (const std::string& option : descriptor->enumOptions) {
            builder.field(option);
        }
        builder.field(descriptor->affectsSimulation).field(descriptor->affectsAppearance);
        // displayName/description/multiline quedan afuera a proposito: son
        // presentacion, no contrato. Retocar un texto de ayuda no puede
        // marcar un proyecto como incompatible.
    }
    return builder.finish();
}

Hash256 hashPackage(const ComponentDefinition& definition) {
    Sha256Builder builder;
    builder.field(std::string_view("package.v1"))
        .field(definition.partNumber)
        .field(static_cast<uint64_t>(definition.physicalPinout.size()));
    // Aca el orden SI es el contrato: es la numeracion fisica del DIP, pin 1
    // a N, y no puede reordenarse sin cambiar el chip.
    for (const ComponentDefinition::PhysicalPin& pin : definition.physicalPinout) {
        builder.field(pin.label).field(pin.isFunctional);
    }
    return builder.finish();
}

Hash256 hashAppearance(const ComponentDefinition& definition, const Hash256& packageHash) {
    Sha256Builder builder;
    builder.field(std::string_view("appearance.v1"))
        .field(definition.typeId)
        .field(static_cast<uint64_t>(definition.appearanceVersion))
        .field(static_cast<uint64_t>(definition.category))
        .field(packageHash.toHex());
    // Las propiedades marcadas affectsAppearance forman parte de como se ve
    // el componente, asi que agregar o quitar una es un cambio visual.
    std::vector<std::string> appearanceProperties;
    for (const PropertyDescriptor& descriptor : definition.properties) {
        if (descriptor.affectsAppearance) {
            appearanceProperties.push_back(descriptor.id);
        }
    }
    std::sort(appearanceProperties.begin(), appearanceProperties.end());
    builder.field(static_cast<uint64_t>(appearanceProperties.size()));
    for (const std::string& id : appearanceProperties) {
        builder.field(id);
    }
    return builder.finish();
}

Hash256 hashSimulation(const ComponentDefinition& definition, const Hash256& pinHash) {
    Sha256Builder builder;
    builder.field(std::string_view("simulation.v1"))
        .field(definition.typeId)
        .field(static_cast<uint64_t>(definition.behaviorVersion))
        .field(pinHash.toHex());
    std::vector<std::string> simulationProperties;
    for (const PropertyDescriptor& descriptor : definition.properties) {
        if (descriptor.affectsSimulation) {
            simulationProperties.push_back(descriptor.id);
        }
    }
    std::sort(simulationProperties.begin(), simulationProperties.end());
    builder.field(static_cast<uint64_t>(simulationProperties.size()));
    for (const std::string& id : simulationProperties) {
        builder.field(id);
    }
    return builder.finish();
}

ComponentFingerprints assemble(const ComponentDefinition& definition, const std::vector<PinTemplate>& pins) {
    ComponentFingerprints fingerprints;
    fingerprints.pinInterface = hashPins(pins);
    fingerprints.propertyInterface = hashProperties(definition.properties);
    fingerprints.package = hashPackage(definition);
    fingerprints.appearance = hashAppearance(definition, fingerprints.package);
    fingerprints.simulation = hashSimulation(definition, fingerprints.pinInterface);

    Sha256Builder publicBuilder;
    publicBuilder.field(std::string_view("public.v1"))
        .field(definition.typeId)
        .field(static_cast<uint64_t>(definition.definitionVersion))
        .field(fingerprints.pinInterface.toHex())
        .field(fingerprints.propertyInterface.toHex())
        .field(fingerprints.package.toHex());
    fingerprints.publicInterface = publicBuilder.finish();
    return fingerprints;
}

} // namespace

ComponentFingerprints computeFingerprints(const ComponentDefinition& definition, const PropertyMap& properties) {
    std::vector<PinTemplate> pins;
    if (definition.derivePins) {
        try {
            pins = definition.derivePins(properties);
        } catch (const std::exception&) {
            // Configuracion que esta definicion no sabe derivar (p. ej. le
            // falta una propiedad): la huella de pines queda nula, que es
            // como se representa "desconocida".
            pins.clear();
        }
    }
    return assemble(definition, pins);
}

ComponentFingerprints computeFingerprints(const ComponentInstance& instance) {
    // Se usan los pines ya derivados de la instancia y no derivePins(): para
    // structural.subcircuit dependen de un documento externo que solo la
    // instancia conoce.
    return assemble(instance.definition(), instance.pins());
}

CompatibilityVerdict compareFingerprints(const ComponentFingerprints& stored,
                                          const ComponentFingerprints& current) {
    if (stored == current) {
        return CompatibilityVerdict::Identical;
    }
    // De mayor a menor gravedad: un cambio de interfaz publica manda sobre
    // todo lo demas, aunque ademas hayan cambiado el dibujo y la simulacion.
    if (stored.publicInterface != current.publicInterface || stored.pinInterface != current.pinInterface ||
        stored.propertyInterface != current.propertyInterface) {
        return CompatibilityVerdict::RequiresMigration;
    }
    if (stored.simulation != current.simulation) {
        return CompatibilityVerdict::RequiresRecompile;
    }
    return CompatibilityVerdict::AppearanceOnly;
}

} // namespace digitalforge::components
