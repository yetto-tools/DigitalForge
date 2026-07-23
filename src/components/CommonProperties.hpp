#pragma once

#include <string>
#include <utility>

#include "Property.hpp"

// Propiedades de presentacion comunes a (casi) todos los tipos de componente:
// etiqueta, rotacion de etiqueta, notas, color de cuerpo y tamano
// personalizado. Se extrajeron aca para que tanto la biblioteca integrada en
// C++ (components/BasicComponentLibrary.cpp) como el cargador de componentes
// desde JSON (components/JsonComponentLoader.cpp) las construyan desde una
// unica fuente -- si dos copias divergieran, dos componentes "iguales"
// tendrian propertyInterfaceHash distintos sin motivo.
namespace digitalforge::components {

[[nodiscard]] inline PropertyDescriptor makeLabelProperty() {
    return PropertyDescriptor{
        .id = "label",
        .displayName = "Etiqueta",
        .description = "Nombre opcional visible para el usuario, mostrado junto al componente.",
        .type = PropertyType::String,
        .defaultValue = std::string(""),
        .minValue = std::nullopt,
        .maxValue = std::nullopt,
        .enumOptions = {},
        .affectsSimulation = false,
        .affectsAppearance = true,
    };
}

// Rotacion propia de la etiqueta (0/90/180/270), independiente de la
// rotacion del componente en si (ComponentPlacement::rotationDegrees) -- por
// defecto (0) editor::ComponentItem la contrarresta para que el texto
// siempre se lea horizontal sin importar como quedo orientado el
// componente; un valor distinto de 0 la gira ademas ese tanto, a proposito
// del usuario (p. ej. para acomodarla en un espacio angosto).
[[nodiscard]] inline PropertyDescriptor makeLabelRotationProperty() {
    return PropertyDescriptor{
        .id = "labelRotation",
        .displayName = "Rotacion de etiqueta",
        .description = "Rotacion propia de la etiqueta, independiente de la rotacion del componente. "
                        "En 0, la etiqueta siempre se dibuja horizontal sin importar como este rotado el componente.",
        .type = PropertyType::Enum,
        .defaultValue = std::string("0"),
        .minValue = std::nullopt,
        .maxValue = std::nullopt,
        .enumOptions = {"0", "90", "180", "270"},
        .affectsSimulation = false,
        .affectsAppearance = true,
    };
}

// Color de relleno del cuerpo del componente en el lienzo. No se ofrece en
// io.led/io.seven_segment (que ya tienen su propio esquema de color
// semantico - lente/segmentos) ni en wiring.input (cuyo color de fondo
// refleja el valor logico actual, no un tinte estatico).
[[nodiscard]] inline PropertyDescriptor makeBodyColorProperty(std::string defaultHex) {
    return PropertyDescriptor{
        .id = "bodyColor",
        .displayName = "Color del cuerpo",
        .description = "Color de relleno del cuerpo del componente. Solo visual.",
        .type = PropertyType::Color,
        .defaultValue = std::move(defaultHex),
        .minValue = std::nullopt,
        .maxValue = std::nullopt,
        .enumOptions = {},
        .affectsSimulation = false,
        .affectsAppearance = true,
    };
}

[[nodiscard]] inline PropertyDescriptor makeCustomWidthProperty() {
    return PropertyDescriptor{
        .id = "customWidth",
        .displayName = "Ancho personalizado",
        .description = "Ancho del cuerpo en el lienzo, en pixeles; 0 = automatico segun el tipo y la cantidad de pines.",
        .type = PropertyType::UnsignedInteger,
        .defaultValue = uint64_t{0},
        .minValue = uint64_t{0},
        .maxValue = uint64_t{2000},
        .enumOptions = {},
        .affectsSimulation = false,
        .affectsAppearance = true,
    };
}

[[nodiscard]] inline PropertyDescriptor makeCustomHeightProperty() {
    return PropertyDescriptor{
        .id = "customHeight",
        .displayName = "Alto personalizado",
        .description = "Alto del cuerpo en el lienzo, en pixeles; 0 = automatico segun el tipo y la cantidad de pines.",
        .type = PropertyType::UnsignedInteger,
        .defaultValue = uint64_t{0},
        .minValue = uint64_t{0},
        .maxValue = uint64_t{2000},
        .enumOptions = {},
        .affectsSimulation = false,
        .affectsAppearance = true,
    };
}

[[nodiscard]] inline PropertyDescriptor makeNotesProperty() {
    return PropertyDescriptor{
        .id = "notes",
        .displayName = "Notas",
        .description = "Texto libre para uso propio; no afecta la simulacion ni se dibuja en el lienzo.",
        .type = PropertyType::String,
        .defaultValue = std::string(""),
        .minValue = std::nullopt,
        .maxValue = std::nullopt,
        .enumOptions = {},
        .affectsSimulation = false,
        .affectsAppearance = false,
        .multiline = true,
    };
}

} // namespace digitalforge::components
