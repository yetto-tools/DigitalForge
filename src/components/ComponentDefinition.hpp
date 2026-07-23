#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Property.hpp"
#include "core/Circuit.hpp"
#include "core/Net.hpp"
#include "core/Pin.hpp"

namespace digitalforge::components {

// Definido en ExternalDocumentView.hpp; solo se referencia aca por
// puntero/referencia (deriveExternalPins/buildExternalSimulation mas
// abajo), asi que una declaracion adelantada alcanza y evita que
// ComponentDefinition.hpp dependa de ese header.
struct ExternalContext;

using PropertyMap = std::map<std::string, PropertyValue>;

// Categoria de paleta de alto nivel a la que pertenece un componente; determina
// bajo que rama del arbol de componentes (panel izquierdo, agregado en la fase
// de GUI) aparece, independientemente de su typeId o nombre visible.
enum class ComponentCategory : uint8_t {
    Wiring,
    Gates,
    Plexers,
    Arithmetic,
    Memory,
    Subcircuits,
    IO,
    Base,
    Analysis,
    Ic74LS,
};

[[nodiscard]] constexpr const char* toString(ComponentCategory category) noexcept {
    switch (category) {
        case ComponentCategory::Wiring:      return "Wiring";
        case ComponentCategory::Gates:       return "Gates";
        case ComponentCategory::Plexers:     return "Plexers";
        case ComponentCategory::Arithmetic:  return "Arithmetic";
        case ComponentCategory::Memory:      return "Memory";
        case ComponentCategory::Subcircuits: return "Subcircuits";
        case ComponentCategory::IO:          return "IO";
        case ComponentCategory::Base:        return "Base";
        case ComponentCategory::Analysis:    return "Analysis";
        case ComponentCategory::Ic74LS:      return "74LSxx";
    }
    return "Unknown";
}

// Un punto de conexion con nombre que expone una instancia de componente.
// Las listas de pines pueden depender de valores de propiedades (p. ej. el
// inputCount de una puerta AND), por lo que se derivan y no son fijas, segun
// ComponentDefinition::derivePins.
struct PinTemplate {
    std::string name;
    core::PinDirection direction = core::PinDirection::Input;

    [[nodiscard]] bool operator==(const PinTemplate& other) const noexcept {
        return name == other.name && direction == other.direction;
    }
};

// Como se corresponde una instancia de componente con el nucleo de
// simulacion existente: o bien posee exactamente un core::Gate que controla
// su net de salida (Driver), o solo observa un net sin agregar ningun
// core::Gate (Sink, p. ej. un LED o una sonda) - cumpliendo con el
// requisito de que los sinks nunca se evaluen como si fueran logica activa.
// `None` cubre piezas puramente estructurales que se introduciran mas
// adelante (etiquetas, comentarios) sin ninguna huella en la simulacion.
struct ComponentSimBinding {
    enum class Kind : uint8_t { None, Driver, Sink } kind = Kind::None;
    uint32_t gateIndex = 0;
    // Para componentes Sink, el/los net(s) que observa, en el mismo orden
    // que sus pines de entrada (p. ej. una entrada para un LED, siete u
    // ocho para las a..g + dot de un display de siete segmentos).
    std::vector<core::NetId> observedNets;
};

// Descripcion inmutable y compartida de un *tipo* de componente (p. ej.
// "gates.and"). Una unica ComponentDefinition sirve a todas las
// ComponentInstance de ese tipo; no posee ningun estado por instancia.
struct ComponentDefinition {
    std::string typeId;
    std::string displayName;
    std::string description;

    // --- Versionado (ver components/ComponentFingerprints.hpp) ---
    //
    // Se incrementa ante cualquier cambio de la definicion que el usuario
    // pueda notar: pines, propiedades, comportamiento o dibujo. Entra en
    // publicInterfaceHash, de modo que un proyecto guardado puede detectar
    // que la definicion instalada ya no es la que uso.
    uint32_t definitionVersion = 1;
    // Se incrementa SOLO cuando cambia lo que hace buildSimulation (netlist,
    // primitivas, retardos, estado secuencial). buildSimulation es una
    // closure de C++: no hay forma de hashear su cuerpo, asi que esta version
    // declarada es la unica manera de que simulationHash refleje el cambio.
    // Olvidarse de subirla no rompe proyectos, pero deja una cache de
    // simulacion sin invalidar.
    uint32_t behaviorVersion = 1;
    // Idem para el dibujo en el lienzo (ComponentItem::paint*). Subirla sola
    // clasifica el cambio como puramente visual, que no toca conexiones ni
    // propiedades ni obliga a reconstruir la simulacion.
    uint32_t appearanceVersion = 1;
    // Numero de parte real (p. ej. "7400"), vacio si no aplica. Solo lo
    // usan los ic74ls.* de numero fijo - ComponentItem::paintIc74ls() lo
    // dibuja rotado en el hueco central del cuerpo para poder distinguir a
    // simple vista de que chip se trata sin abrir el inspector.
    // ic74ls.bcdDriver es la excepcion: su numero de parte depende de la
    // propiedad "variant" (7447/7448), asi que no usa este campo - ver
    // paintIc74ls().
    std::string partNumber;

    // Un pin del DIP fisico real de un ic74ls.*: si `isFunctional` es true,
    // `label` coincide con el nombre de un PinTemplate real (de
    // derivePins()), cableable, y solo indica en que posicion fisica va.
    // Si es false, es un pin puramente decorativo (VCC/GND/NC/PR/CLR/LT/
    // RBI/BI-RBO/etc.) que este simulador no modela electricamente (ver el
    // "recorte" documentado de cada tipo en docs/component-status.md) -
    // se dibuja solo con nombre + linea de pin punteada, sin punto/PinItem
    // conectable.
    struct PhysicalPin {
        std::string label;
        bool isFunctional = true;
    };
    // Secuencia fisica real del DIP, pin 1 -> N: empieza arriba a la
    // izquierda (junto a la muesca), baja toda la columna izquierda, y
    // sube toda la columna derecha desde abajo hasta arriba - tal como
    // numera Texas Instruments un 74LSxx real. Vacio para cualquier tipo
    // que no sea ic74ls.* (esos usan el layout generico agrupado por
    // direccion) - ver ComponentItem::rebuildPins()/paintIc74ls().
    std::vector<PhysicalPin> physicalPinout;
    ComponentCategory category = ComponentCategory::Base;
    std::vector<PropertyDescriptor> properties;

    std::function<std::vector<PinTemplate>(const PropertyMap&)> derivePins;

    // `pinNets` es paralelo a derivePins(properties): pinNets[i] es el net
    // enlazado a ese pin. Registra en `circuit` el core::Gate que esta
    // instancia necesite (si acaso) e informa como debe leerse la
    // instancia desde un core::Simulator en ejecucion.
    std::function<ComponentSimBinding(core::Circuit&, const PropertyMap&, const std::vector<core::NetId>&)>
        buildSimulation;

    // Par paralelo y opcional a derivePins/buildSimulation, solo poblado
    // por structural.subcircuit (ver BasicComponentLibrary.cpp): necesita
    // leer el documento *referenciado* (sus wiring.input/output de
    // frontera, su grafo interno) para derivar pines/simular, algo que
    // ninguna otra closure de esta biblioteca necesita. Mantenerlas
    // separadas evita agregarle un parametro de contexto externo, mudo
    // para los demas 25 tipos, a derivePins/buildSimulation de arriba.
    // ComponentInstance usa estas en vez de las de arriba cuando estan
    // pobladas (ver ComponentInstance::derivePins()/buildSimulation()).
    std::function<std::vector<PinTemplate>(const PropertyMap&, const ExternalContext&)> deriveExternalPins;
    std::function<ComponentSimBinding(core::Circuit&, const PropertyMap&, const std::vector<core::NetId>&,
                                       const ExternalContext&)>
        buildExternalSimulation;

    [[nodiscard]] const PropertyDescriptor* findProperty(const std::string& id) const {
        for (const PropertyDescriptor& descriptor : properties) {
            if (descriptor.id == id) {
                return &descriptor;
            }
        }
        return nullptr;
    }
};

} // namespace digitalforge::components
