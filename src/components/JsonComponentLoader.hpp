#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "ComponentDefinition.hpp"

namespace digitalforge::components {

class ComponentRegistry;

// Construye una ComponentDefinition a partir de un objeto JSON con el formato
// de componente "por netlist de primitivas" (ver docs/component-format.md):
// pines fijos con nombre + una lista de compuertas primitivas del nucleo
// conectadas por nets internas nombradas. El comportamiento no es una closure
// de C++ como en la biblioteca integrada, sino que se reconstruye reproduciendo
// ese netlist en el core::Circuit al simular.
//
// Toda la validacion ocurre aca, en tiempo de CARGA (no al colocar el
// componente): campo faltante, tipo de compuerta desconocido, aridad
// incorrecta, una salida que maneja un pin de entrada, un pin de salida sin
// driver, o una net interna que se lee pero nada controla. Lanza
// std::invalid_argument con un mensaje contextual ante cualquiera de estos.
[[nodiscard]] ComponentDefinition componentDefinitionFromJson(const nlohmann::json& json);

// Un archivo que no se pudo cargar, junto con el motivo legible. El resto de
// los archivos del directorio se cargan igual (ver loadComponentLibraryFromDirectory).
struct ComponentLoadError {
    std::string source; // ruta del archivo (o "<json>" para la sobrecarga en memoria)
    std::string message;
};

struct ComponentLoadReport {
    std::vector<std::string> loadedTypeIds;
    std::vector<ComponentLoadError> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

// Registra en `registry` cada archivo *.json de `directory` (no recursivo, en
// orden alfabetico por nombre de archivo para que la carga sea determinista).
// NO lanza: si un archivo esta malformado, no se parsea o su typeId choca con
// uno ya registrado, se acumula un ComponentLoadError y se sigue con el resto,
// de modo que un solo componente roto no impida cargar los demas. Un
// directorio inexistente no es un error: devuelve un reporte vacio.
ComponentLoadReport loadComponentLibraryFromDirectory(ComponentRegistry& registry,
                                                       const std::filesystem::path& directory);

} // namespace digitalforge::components
