#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace digitalforge::components {

// Tipos escalares que puede contener una propiedad de componente. Se
// extendera en fases posteriores (Orientation, Frequency, FilePath,
// BitWidth) a medida que se implementen componentes que realmente los
// necesiten; el variant de abajo puede crecer sin romper los descriptores
// existentes.
enum class PropertyType : uint8_t {
    Boolean,
    Integer,
    UnsignedInteger,
    String,
    Enum,
    // Guardado en el mismo slot std::string de PropertyValue que String, como
    // un hex "#RRGGBB" - no agrega ninguna alternativa nueva al variant, solo
    // cambia como se valida (ver validatePropertyValue) y como lo dibuja
    // ui::PropertyInspector (un selector de color en vez de un QLineEdit).
    Color,
};

using PropertyValue = std::variant<bool, int64_t, uint64_t, std::string>;

[[nodiscard]] constexpr const char* toString(PropertyType type) noexcept {
    switch (type) {
        case PropertyType::Boolean:         return "boolean";
        case PropertyType::Integer:         return "integer";
        case PropertyType::UnsignedInteger: return "unsignedInteger";
        case PropertyType::String:          return "string";
        case PropertyType::Enum:            return "enum";
        case PropertyType::Color:           return "color";
    }
    return "unknown";
}

// Descripcion estatica de una propiedad editable de un tipo de componente.
// El mismo descriptor es compartido por todas las instancias de ese tipo;
// solo el PropertyValue actual vive por instancia (ver ComponentInstance).
struct PropertyDescriptor {
    std::string id;
    std::string displayName;
    std::string description;
    PropertyType type = PropertyType::Boolean;
    PropertyValue defaultValue;
    std::optional<PropertyValue> minValue;
    std::optional<PropertyValue> maxValue;
    std::vector<std::string> enumOptions;
    bool affectsSimulation = true;
    bool affectsAppearance = false;
    // Si es String (o Color), le dice a ui::PropertyInspector que use un
    // QPlainTextEdit multilinea en vez de un QLineEdit de una sola linea.
    bool multiline = false;
};

namespace detail {
[[nodiscard]] inline bool isValidHexColor(const std::string& value) {
    if (value.size() != 7 || value.front() != '#') {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(),
                        [](unsigned char c) { return std::isxdigit(c) != 0; });
}
} // namespace detail

// Verifica que `value` sea un valor legal para `descriptor`: la alternativa
// correcta para el tipo declarado, dentro de [minValue, maxValue] cuando
// esos limites estan definidos para Integer/UnsignedInteger, y entre los
// enumOptions cuando el tipo es Enum. Devuelve true si tiene exito; en caso
// de fallo, si `errorOut` no es nulo, lo llena con un motivo legible para
// que los llamadores puedan mostrar un error de validacion claro.
[[nodiscard]] inline bool validatePropertyValue(const PropertyDescriptor& descriptor,
                                                 const PropertyValue& value,
                                                 std::string* errorOut = nullptr) {
    const auto fail = [&](const std::string& message) {
        if (errorOut != nullptr) {
            *errorOut = message;
        }
        return false;
    };

    switch (descriptor.type) {
        case PropertyType::Boolean:
            if (!std::holds_alternative<bool>(value)) {
                return fail("property '" + descriptor.id + "' expects a boolean value");
            }
            return true;

        case PropertyType::Integer: {
            if (!std::holds_alternative<int64_t>(value)) {
                return fail("property '" + descriptor.id + "' expects an integer value");
            }
            const int64_t v = std::get<int64_t>(value);
            if (descriptor.minValue.has_value() && v < std::get<int64_t>(*descriptor.minValue)) {
                return fail("property '" + descriptor.id + "' is below its minimum value");
            }
            if (descriptor.maxValue.has_value() && v > std::get<int64_t>(*descriptor.maxValue)) {
                return fail("property '" + descriptor.id + "' is above its maximum value");
            }
            return true;
        }

        case PropertyType::UnsignedInteger: {
            if (!std::holds_alternative<uint64_t>(value)) {
                return fail("property '" + descriptor.id + "' expects an unsigned integer value");
            }
            const uint64_t v = std::get<uint64_t>(value);
            if (descriptor.minValue.has_value() && v < std::get<uint64_t>(*descriptor.minValue)) {
                return fail("property '" + descriptor.id + "' is below its minimum value");
            }
            if (descriptor.maxValue.has_value() && v > std::get<uint64_t>(*descriptor.maxValue)) {
                return fail("property '" + descriptor.id + "' is above its maximum value");
            }
            return true;
        }

        case PropertyType::String:
            if (!std::holds_alternative<std::string>(value)) {
                return fail("property '" + descriptor.id + "' expects a string value");
            }
            return true;

        case PropertyType::Enum: {
            if (!std::holds_alternative<std::string>(value)) {
                return fail("property '" + descriptor.id + "' expects one of its enum options");
            }
            const std::string& v = std::get<std::string>(value);
            for (const std::string& option : descriptor.enumOptions) {
                if (option == v) {
                    return true;
                }
            }
            return fail("property '" + descriptor.id + "' value '" + v + "' is not a valid option");
        }

        case PropertyType::Color:
            if (!std::holds_alternative<std::string>(value)) {
                return fail("property '" + descriptor.id + "' expects a color value");
            }
            if (!detail::isValidHexColor(std::get<std::string>(value))) {
                return fail("property '" + descriptor.id + "' expects a '#RRGGBB' hex color");
            }
            return true;
    }
    return fail("property '" + descriptor.id + "' has an unrecognized type");
}

} // namespace digitalforge::components
