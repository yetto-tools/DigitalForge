#pragma once

#include <string>

#include "ComponentDefinition.hpp"
#include "core/Sha256.hpp"

namespace digitalforge::components {

// Definida en ComponentInstance.hpp; aca solo se referencia por referencia en
// una sobrecarga, asi que alcanza con declararla y se evita que este header
// arrastre toda la instancia.
class ComponentInstance;

// Huellas de compatibilidad de un componente. Cada una cubre un aspecto
// distinto para que un cambio pueda clasificarse por si solo: cambiar el
// simbolo no debe invalidar la simulacion, y cambiar la logica interna no
// debe romper las conexiones de un proyecto guardado.
//
// Se calculan sobre las ComponentDefinition en C++ que ya existen; no hace
// falta DFML para tenerlas. Las que ese formato agregaria mas adelante
// (sourceHash sobre el .dfml y dependencyHash sobre subcomponentes) todavia
// no aplican: no hay archivo fuente ni grafo de dependencias que hashear.
struct ComponentFingerprints {
    // Todo lo que otro circuito puede observar: identidad, pines, propiedades
    // publicas y encapsulado. Si cambia, hay que analizar si el proyecto
    // necesita migracion.
    core::Hash256 publicInterface;
    // Solo el contrato de pines (clave, direccion, orden estable por clave).
    core::Hash256 pinInterface;
    // Solo el contrato de propiedades (id, tipo, default, limites, opciones).
    core::Hash256 propertyInterface;
    // Comportamiento simulado. Ver behaviorVersion en ComponentDefinition:
    // buildSimulation es una closure de C++, no es inspeccionable, asi que
    // esta huella depende de una version declarada por el autor mas los datos
    // que si son observables.
    core::Hash256 simulation;
    // Dibujo en el lienzo. Misma limitacion y misma solucion que simulation
    // (appearanceVersion).
    core::Hash256 appearance;
    // Encapsulado fisico: secuencia de pines del DIP y cuales son
    // funcionales. Vacio para todo lo que no sea ic74ls.*.
    core::Hash256 package;

    [[nodiscard]] bool operator==(const ComponentFingerprints& other) const noexcept {
        return publicInterface == other.publicInterface && pinInterface == other.pinInterface &&
               propertyInterface == other.propertyInterface && simulation == other.simulation &&
               appearance == other.appearance && package == other.package;
    }
};

// Calcula las huellas de `definition` para una configuracion concreta de
// propiedades. Las propiedades importan porque los pines se derivan de ellas
// (el inputCount de una AND, las filas/columnas de una matriz LED): dos
// instancias del mismo tipo con distinto inputCount tienen, correctamente,
// distinto pinInterface.
//
// `properties` debe ser el mapa completo de la instancia (el que devuelve
// ComponentInstance::properties tras aplicar los defaults). Si derivePins()
// lanza para esa configuracion, pinInterface queda nulo.
[[nodiscard]] ComponentFingerprints computeFingerprints(const ComponentDefinition& definition,
                                                         const PropertyMap& properties);

// Igual, pero para una instancia ya construida - usa sus propiedades reales y
// sus pines ya derivados (incluidos los de structural.subcircuit, que
// dependen de un documento externo y no de derivePins()).
[[nodiscard]] ComponentFingerprints computeFingerprints(const ComponentInstance& instance);

// Como difieren dos juegos de huellas del mismo typeId. El orden es de menor
// a mayor gravedad: quien clasifique un cambio se queda con el primero que
// aplique.
enum class CompatibilityVerdict : uint8_t {
    // Nada cambio: la definicion instalada es la misma con la que se guardo.
    Identical,
    // Solo cambio el dibujo. Actualizar el simbolo y nada mas.
    AppearanceOnly,
    // Cambio el comportamiento simulado pero la interfaz publica es la misma:
    // hay que reconstruir la simulacion, conservando conexiones y propiedades.
    RequiresRecompile,
    // Cambiaron pines o propiedades publicas: no se puede reconectar por
    // indice, hace falta migrar.
    RequiresMigration,
};

[[nodiscard]] CompatibilityVerdict compareFingerprints(const ComponentFingerprints& stored,
                                                        const ComponentFingerprints& current);

[[nodiscard]] constexpr const char* toString(CompatibilityVerdict verdict) noexcept {
    switch (verdict) {
        case CompatibilityVerdict::Identical:         return "identical";
        case CompatibilityVerdict::AppearanceOnly:    return "appearanceOnly";
        case CompatibilityVerdict::RequiresRecompile: return "requiresRecompile";
        case CompatibilityVerdict::RequiresMigration: return "requiresMigration";
    }
    return "unknown";
}

} // namespace digitalforge::components
