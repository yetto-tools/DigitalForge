#pragma once

#include <QObject>
#include <QPointF>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

class QTimer;

#include "components/ComponentInstance.hpp"
#include "components/ComponentRegistry.hpp"
#include "components/ExternalDocumentView.hpp"
#include "components/JsonComponentLoader.hpp"
#include "core/Circuit.hpp"
#include "core/Simulator.hpp"

namespace digitalforge::editor {

// Un extremo de un cable: el pin `pinIndex` del componente `componentId`.
struct PinRef {
    uint32_t componentId = 0;
    uint16_t pinIndex = 0;

    [[nodiscard]] bool operator==(const PinRef& other) const noexcept {
        return componentId == other.componentId && pinIndex == other.pinIndex;
    }
    [[nodiscard]] bool operator<(const PinRef& other) const noexcept {
        return componentId != other.componentId ? componentId < other.componentId : pinIndex < other.pinIndex;
    }
};

// Un extremo de un cable: o bien un pin de componente (isJunction=false,
// id=componentId), o bien un punto de union libre sin componente asociado
// (isJunction=true, id=junctionId), donde 2 o mas cables se encuentran
// formando una derivacion. Se puede construir implicitamente desde un PinRef
// para que el codigo pin-a-pin existente siga compilando sin cambios.
struct WireEndpoint {
    bool isJunction = false;
    uint32_t id = 0;       // componentId si !isJunction, junctionId si isJunction
    uint16_t pinIndex = 0; // solo valido si !isJunction

    WireEndpoint() = default;
    WireEndpoint(PinRef pin) : id(pin.componentId), pinIndex(pin.pinIndex) {}

    [[nodiscard]] static WireEndpoint junction(uint32_t junctionId) {
        WireEndpoint endpoint;
        endpoint.isJunction = true;
        endpoint.id = junctionId;
        return endpoint;
    }

    [[nodiscard]] PinRef pin() const noexcept { return PinRef{id, pinIndex}; }

    [[nodiscard]] bool operator==(const WireEndpoint& other) const noexcept {
        return isJunction == other.isJunction && id == other.id && (isJunction || pinIndex == other.pinIndex);
    }
    [[nodiscard]] bool operator<(const WireEndpoint& other) const noexcept {
        if (isJunction != other.isJunction) {
            return isJunction < other.isJunction;
        }
        if (id != other.id) {
            return id < other.id;
        }
        return !isJunction && pinIndex < other.pinIndex;
    }
};

struct WireConnection {
    uint32_t id = 0;
    WireEndpoint a;
    WireEndpoint b;
    // Puntos de quiebre intermedios definidos por el usuario (coordenadas de
    // escena). Puramente presentacional -- igual que ComponentPlacement,
    // nunca dispara una reconstruccion de la simulacion. Vacio = auto-ruteo.
    std::vector<QPointF> waypoints;
};

// Un punto de union libre (sin componente) donde 2 o mas cables se
// encuentran, formando una derivacion. A diferencia de un waypoint de
// WireConnection, un Junction si afecta la conectividad: participa en el
// union-find de rebuildSimulation() igual que un pin. Se crea al conectar un
// cable al cuerpo de otro cable existente (o a un punto de union ya
// existente) y se elimina automaticamente cuando el ultimo cable que lo
// referencia se elimina.
struct Junction {
    uint32_t id = 0;
    QPointF position;
};

// El posicionamiento (placement) es puramente estado de presentacion (nunca
// afecta a la simulacion), pero vive en el documento y no unicamente en
// ComponentItem, para que los comandos de undo y el futuro formato de
// proyecto puedan leerlo/restaurarlo sin necesidad de acceder a la escena
// grafica.
struct ComponentPlacement {
    QPointF position{0.0, 0.0};
    int rotationDegrees = 0; // uno de 0, 90, 180, 270
    // Orden de apilado (ComponentItem::setZValue) entre los ComponentItem
    // entre si -- no afecta a WireItem/JunctionItem, que siempre quedan
    // detras (zValue -1/-0.5, ver sus constructores) sin importar esto. Dos
    // componentes recien colocados comparten 0 (orden de insercion, como
    // cualquier par de QGraphicsItem con el mismo zValue); "Traer al
    // frente"/"Enviar al fondo"/etc (ver CircuitScene::bringSelectedToFront()
    // y companeros) lo ajustan relativo al resto.
    int zOrder = 0;
    // Desplazamiento (en el espacio LOCAL del componente, igual que los
    // pines) de la etiqueta de instancia respecto de su posicion por
    // defecto (debajo del cuerpo) -- (0,0) es esa posicion por defecto, para
    // que los proyectos guardados antes de que existiera este campo se vean
    // identicos. Se arrastra directamente sobre ComponentItem (ver
    // ComponentItem::mousePressEvent), nunca via SelectionTool -- es un gesto
    // propio del item, no una reubicacion del componente.
    QPointF labelOffset{0.0, 0.0};
};

// Es propietario de cada ComponentInstance colocado y de cada WireConnection
// en un unico circuito, y es la unica pieza del editor que se comunica con
// el nucleo de simulacion. Las subclases de QGraphicsItem (ComponentItem,
// WireItem) son puramente presentacionales: leen el estado desde
// CircuitDocument y le reenvian los gestos del usuario (normalmente via
// QUndoCommand), pero nunca evaluan logica por si mismas - "el modelo
// grafico no debe contener la logica de simulacion".
//
// core::Circuit no tiene una API de eliminacion (por diseno: el nucleo de
// simulacion se mantiene append-only y cache-friendly para circuitos con
// millones de puertas). Un documento editable, en cambio, reconstruye un
// Circuit/Simulator nuevo a partir de sus componentes y cables actuales tras
// cada edicion estructural (agregar/eliminar componente, agregar/eliminar
// cable, un cambio de propiedad que altera el numero de pines). Los cambios
// puramente geometricos (mover, rotar) nunca disparan una reconstruccion.
// Ademas de QObject, implementa components::ExternalDocumentView para poder
// actuar como el documento *referenciado* por un structural.subcircuit
// colocado en otro CircuitDocument - ver boundaryPins()/flattenInto() mas
// abajo y el comentario de la interfaz en ExternalDocumentView.hpp sobre por
// que components:: no puede depender de este header directamente.
class CircuitDocument : public QObject, public components::ExternalDocumentView {
    Q_OBJECT

public:
    explicit CircuitDocument(QObject* parent = nullptr);

    [[nodiscard]] const components::ComponentRegistry& registry() const noexcept { return registry_; }

    // Resultado de cargar la biblioteca de componentes desde JSON al construir
    // el documento (typeIds cargados y errores por archivo). Vacio si no habia
    // directorio de componentes. Pensado para que la interfaz pueda avisar de
    // un componente que no cargo, en vez de fallar en silencio.
    [[nodiscard]] const components::ComponentLoadReport& componentLibraryReport() const noexcept {
        return componentLibraryReport_;
    }

    // Lanza std::invalid_argument si typeId es desconocido o los overrides son invalidos.
    uint32_t addComponent(const std::string& typeId, components::PropertyMap overrides = {},
                          ComponentPlacement placement = {});
    // Coloca (o reemplaza) un componente bajo un id elegido por quien lo
    // invoca, preservando la identidad a lo largo de ciclos de undo/redo.
    // Usado por PlaceComponentCommand, cuyo propio redo() debe reproducir
    // exactamente el mismo componentId cada vez.
    void addComponentWithId(uint32_t id, const std::string& typeId, components::PropertyMap overrides,
                             ComponentPlacement placement);
    // Tambien elimina cada cable conectado a este componente.
    void removeComponent(uint32_t componentId);
    // Elimina todos los componentes (y por lo tanto todos los cables). Se
    // usa antes de cargar un proyecto distinto en este documento.
    void clear();

    [[nodiscard]] ComponentPlacement componentPlacement(uint32_t componentId) const;
    // Geometria pura: nunca dispara una reconstruccion de la simulacion.
    void setComponentPlacement(uint32_t componentId, ComponentPlacement placement);

    [[nodiscard]] components::ComponentInstance* component(uint32_t componentId);
    [[nodiscard]] const components::ComponentInstance* component(uint32_t componentId) const;
    [[nodiscard]] std::vector<uint32_t> componentIds() const;

    // Lanza std::invalid_argument si alguno de los extremos-pin ya esta
    // conectado, o si ambos extremos nombran exactamente el mismo pin. Acepta
    // PinRef donde antes se pedia (conversion implicita a WireEndpoint).
    uint32_t addWire(WireEndpoint a, WireEndpoint b);
    void removeWire(uint32_t wireId);
    // Restaura un cable con un id especifico (usado por undo tras removeWire()).
    void restoreWire(const WireConnection& wire);
    // Cambia el trazado presentacional (puntos de quiebre) de un cable ya
    // existente; geometria pura, nunca dispara una reconstruccion.
    void setWireWaypoints(uint32_t wireId, std::vector<QPointF> waypoints);

    // Reconecta uno de los dos extremos de un cable existente a un nuevo
    // destino (pin o punto de union), sin borrar el otro extremo. Reconstruye
    // el WireItem correspondiente (via wireAboutToBeRemoved + wireAdded, para
    // que la vista tome la nueva ancla) y limpia el punto de union viejo si
    // quedo huerfano. Devuelve false (sin cambios) si el destino no existe o
    // dejaria el cable conectado a si mismo. `endIsA` elige cual extremo se
    // reconecta.
    bool retargetWire(uint32_t wireId, bool endIsA, WireEndpoint newEndpoint);

    [[nodiscard]] const WireConnection* wire(uint32_t wireId) const;
    [[nodiscard]] std::vector<uint32_t> wireIds() const;
    [[nodiscard]] std::vector<WireConnection> wiresAttachedToComponent(uint32_t componentId) const;
    // Todos los cables conectados directamente a este pin - un pin acepta
    // cuantos cables hagan falta (igual criterio que un punto de union, ver
    // wiresAttachedToJunction()), asi que puede haber 0, 1 o varios. Sirve
    // tanto para el punto de empalme relleno cuando hay 2+ (ver
    // PinItem::paint()) como para cualquier chequeo de conectividad
    // ("size() >= 1" reemplaza al viejo pinHasWire()/"tiene algun cable").
    [[nodiscard]] std::vector<WireConnection> wiresAttachedToPin(PinRef pin) const;

    // Punto de union libre: ver el comentario de Junction mas arriba.
    // reserveJunctionId()/addJunctionWithId() siguen el mismo patron de
    // "reservar identidad por adelantado, aplicar despues" que
    // reserveComponentId()/addComponentWithId(), para que los comandos de
    // undo puedan reproducir el mismo id en cada redo().
    [[nodiscard]] uint32_t reserveJunctionId() noexcept { return nextJunctionId_++; }
    void addJunctionWithId(uint32_t junctionId, QPointF position);
    // Elimina un punto de union libre. No-op si no existe o si todavia tiene
    // cables conectados (para no dejar extremos colgando); pensado para el
    // undo de AddJunctionCommand, donde el cable que lo acompanaba ya se quito.
    void removeJunction(uint32_t junctionId);
    [[nodiscard]] QPointF junctionPosition(uint32_t junctionId) const;
    // Reubica un punto de union ya existente (JunctionItem ahora es
    // arrastrable - ver su comentario de clase). Geometria pura, igual que
    // setComponentPlacement(): nunca dispara una reconstruccion de la
    // simulacion. No-op si junctionId no existe.
    void setJunctionPosition(uint32_t junctionId, QPointF position);
    [[nodiscard]] std::vector<uint32_t> junctionIds() const;
    [[nodiscard]] std::vector<WireConnection> wiresAttachedToJunction(uint32_t junctionId) const;

    // Valida y aplica un cambio de propiedad, lanzando std::invalid_argument
    // si falla (en ese caso no se modifica nada). Reconstruye la simulacion
    // solo si la propiedad cambio la disposicion de pines. Devuelve los
    // cables que tuvieron que eliminarse en cascada porque los pines a los
    // que estaban conectados ya no existen, de modo que quienes llaman
    // (comandos de undo) puedan restaurarlos si el cambio se deshace mas
    // adelante.
    std::vector<WireConnection> setProperty(uint32_t componentId, const std::string& propertyId,
                                             components::PropertyValue value);

    [[nodiscard]] uint32_t reserveComponentId() noexcept { return nextComponentId_++; }
    [[nodiscard]] uint32_t reserveWireId() noexcept { return nextWireId_++; }

    // Reconstruccion completa: reconstruye core::Circuit y core::Simulator a
    // partir de los componentes/cables actuales, y luego reaplica el
    // initialValue de cada wiring.input. Cualquier estado interactivo
    // transitorio (p. ej. una entrada conmutada que difiere de su
    // initialValue) se pierde, en consonancia con el hecho de que
    // core::Circuit no puede eliminar puertas in situ.
    void rebuildSimulation();

    [[nodiscard]] bool isLiveSimulation() const noexcept { return liveSimulation_; }
    void setLiveSimulation(bool live);

    // Polaridad global usada para resolver el "Valor inicial" Z de cada
    // wiring.input al (re)construir la simulacion (ver
    // applyInputInitialValues): positiva -> 0, negativa -> 1. Las entradas
    // con Valor inicial 0/1/X no se ven afectadas. Solo tiene efecto en la
    // proxima llamada a rebuildSimulation(), no sobre una simulacion ya en
    // curso.
    [[nodiscard]] bool positiveLogicPolarity() const noexcept { return positiveLogicPolarity_; }
    void setPositiveLogicPolarity(bool positive);

    // Las ediciones de topologia (colocar/eliminar/cablear/recablear/cambios
    // de propiedad) se rechazan mientras la simulacion esta en ejecucion: al
    // editar se reconstruye implicitamente todo el Circuit/Simulator,
    // descartando cualquier estado interactivo en vivo (una entrada
    // conmutada, una oscilacion en curso), lo cual resulta confuso mientras
    // el usuario esta observando la ejecucion. Las herramientas del editor
    // llaman a esto antes de construir/apilar un comando de undo; si
    // devuelve false, no ocurre nada y se emite editBlocked() con un motivo
    // orientado al usuario.
    bool requireEditable(const QString& action);

    // Avanza exactamente un evento. Solo tiene sentido principalmente en pausa.
    void step();

    // Ejecuta el simulador hasta que se estabilice (o hasta detectar una
    // oscilacion), sin importar isLiveSimulation() ni alterarlo. Pensado
    // para herramientas de analisis (p. ej. TruthTablePanel) que necesitan
    // forzar un settle completo despues de barrer una combinacion de
    // entradas, sin activar/desactivar el modo en vivo del documento.
    // Devuelve false si todavia no se construyo ninguna simulacion.
    bool runUntilStable();

    // Establece el valor impulsado de una instancia wiring.input. En modo en
    // vivo, esto tambien ejecuta el simulador hasta un estado estable. Lanza
    // std::invalid_argument si `componentId` no es un wiring.input.
    void setInputValue(uint32_t componentId, core::LogicValue value);

    // HighImpedance si aun no se ha construido ninguna simulacion.
    [[nodiscard]] core::LogicValue pinValue(uint32_t componentId, uint16_t pinIndex) const;
    // Igual que pinValue(), pero acepta cualquier WireEndpoint (pin o punto
    // de union) -- usado por WireItem/JunctionItem para colorear cables y
    // puntos de union sin importar de que tipo es cada extremo.
    [[nodiscard]] core::LogicValue endpointValue(WireEndpoint endpoint) const;

    [[nodiscard]] bool oscillationDetected() const noexcept;

    // --- components::ExternalDocumentView --- (ver ese header para el
    // contrato completo; usado cuando este documento es el *destino* de un
    // structural.subcircuit colocado en otro documento del mismo proyecto).
    [[nodiscard]] std::vector<components::PinTemplate> boundaryPins() const override;
    uint32_t flattenInto(core::Circuit& circuit, std::span<const core::NetId> pinNets) const override;
    [[nodiscard]] bool containsSubcircuit() const override;

    // Establecido por Project cada vez que la lista de documentos del
    // proyecto cambia: dada una ruta relativa (la misma nocion que
    // ProjectManifestEntry::relativePath), devuelve el CircuitDocument
    // hermano correspondiente, o nullptr si ninguno coincide todavia (p.
    // ej. el proyecto nunca se guardo). Re-deriva los pines de cualquier
    // structural.subcircuit ya colocado con el resolver nuevo.
    void setSiblingResolver(std::function<const CircuitDocument*(const QString&)> resolver);

    // Uniones extra derivadas de la GEOMETRIA (no de un WireConnection
    // explicito): pares de extremos que deben quedar en la misma net porque
    // coinciden en la misma celda de grilla o porque uno cae sobre el cuerpo
    // de un cable del otro (derivacion en T). Las calcula la capa de vista
    // (CircuitScene), que es la unica que conoce las coordenadas de escena de
    // los pines; el modelo las incorpora al union-find en rebuildSimulation().
    // Provider nulo (tests headless, subcircuitos) = sin uniones geometricas.
    using GeometricConnectionProvider = std::function<std::vector<std::pair<WireEndpoint, WireEndpoint>>()>;
    void setGeometricConnectionProvider(GeometricConnectionProvider provider);
    // Fuerza un recalculo de la conectividad (vuelve a correr el union-find,
    // incluidas las uniones geometricas del provider). Lo llama la vista tras
    // mover un componente/union, ya que un movimiento puede hacer que dos
    // puntos de conexion pasen a tocarse -- algo que antes no cambiaba la
    // simulacion.
    void recomputeConnectivity();

signals:
    void componentAdded(uint32_t componentId);
    void componentAboutToBeRemoved(uint32_t componentId);
    void wireAdded(uint32_t wireId);
    void wireAboutToBeRemoved(uint32_t wireId);
    void wireGeometryChanged(uint32_t wireId);
    void junctionAdded(uint32_t junctionId);
    void junctionAboutToBeRemoved(uint32_t junctionId);
    void propertyChanged(uint32_t componentId);
    void componentPlacementChanged(uint32_t componentId);
    void junctionPositionChanged(uint32_t junctionId);
    void simulationRebuilt();
    void simulationStepped();
    void editBlocked(const QString& reason);
    void liveSimulationChanged(bool live);
    void positiveLogicPolarityChanged(bool positive);

private:
    void applyInputInitialValues();
    void runIfLive();
    // Unico lugar de todo el proyecto con nocion de tiempo real de pared:
    // un QTimer por instancia wiring.clock presente en components_,
    // alternando su valor cada medio "periodMs" mientras liveSimulation_ es
    // true. Llamado al final de rebuildSimulation() (la topologia o el
    // propio periodMs de un reloj pueden haber cambiado) - primero para,
    // limpia todos los timers viejos y arma de nuevo la lista completa.
    void rebuildClockTimers();
    // Alterna el valor actual del pin CLK de `componentId` (0->1 o 1->0) y
    // propaga si liveSimulation_ esta activo - conectado al timeout() de
    // cada QTimer de clockTimers_.
    void onClockTimeout(uint32_t componentId);
    // Mismo patron que rebuildClockTimers(), pero un QTimer *de un solo
    // disparo* por cada wiring.powerOnReset presente en components_ - cae a
    // Zero una unica vez, "pulseMs" despues de este rebuild (ver
    // firedPowerOnResets_).
    void rebuildPowerOnResetTimers();
    // Pone en Zero el pin Y de `componentId` (una sola vez: ver
    // firedPowerOnResets_), propaga si liveSimulation_ esta activo -
    // conectado al timeout() de cada QTimer de powerOnResetTimers_.
    void onPowerOnResetTimeout(uint32_t componentId);
    // Elimina el cable `wireId` (debe existir) y, si alguno de sus extremos
    // era un punto de union que queda en grado 0 como resultado, tambien lo
    // elimina en cascada. Comun a removeWire()/removeComponent()/el cascade
    // de setProperty(), que necesitan exactamente el mismo comportamiento.
    void eraseWireCascading(uint32_t wireId);

    // Los wiring.input/wiring.output que actuan como pines de frontera de
    // este documento cuando se lo usa como subcircuito, en el orden comun a
    // boundaryPins()/flattenInto() (posicion en el lienzo: Y y luego X).
    struct BoundaryEntry {
        uint32_t componentId;
        bool isInput;
    };
    [[nodiscard]] std::vector<BoundaryEntry> boundaryEntries() const;
    [[nodiscard]] components::ExternalContext makeExternalContext() const;
    // Lanza std::invalid_argument si `targetPath` (ya no vacio) no resuelve
    // a ningun documento hermano, es este mismo documento, o el documento
    // resuelto ya contiene un subcircuito (mas de un nivel de anidamiento).
    void validateSubcircuitTarget(const std::string& targetPath) const;

    components::ComponentRegistry registry_;
    components::ComponentLoadReport componentLibraryReport_;
    std::map<uint32_t, std::unique_ptr<components::ComponentInstance>> components_;
    std::map<uint32_t, ComponentPlacement> placements_;
    std::map<uint32_t, WireConnection> wires_;
    std::map<uint32_t, QPointF> junctions_;
    uint32_t nextComponentId_ = 0;
    uint32_t nextWireId_ = 0;
    uint32_t nextJunctionId_ = 0;

    std::unique_ptr<core::Circuit> circuit_;
    std::unique_ptr<core::Simulator> simulator_;
    std::map<PinRef, core::NetId> pinToNet_;
    std::map<uint32_t, core::NetId> junctionToNet_;
    // Comienza detenida: el usuario debe pulsar Ejecutar explicitamente para
    // iniciar la propagacion en vivo, en consonancia con el requisito de que
    // la simulacion nunca arranque de forma implicita.
    bool liveSimulation_ = false;
    bool positiveLogicPolarity_ = true;
    std::function<const CircuitDocument*(const QString&)> siblingResolver_;
    GeometricConnectionProvider geometricConnectionProvider_;
    // Uno por cada wiring.clock presente - ver rebuildClockTimers(). Cada
    // QTimer esta parentado a `this` (destruido junto con el documento);
    // se recrean por completo en cada rebuildSimulation() en vez de
    // reutilizarse, mas simple que tratar de diffear la lista de relojes.
    std::map<uint32_t, QTimer*> clockTimers_;
    // Uno por cada wiring.powerOnReset presente - ver
    // rebuildPowerOnResetTimers(); de un solo disparo, a diferencia de
    // clockTimers_.
    std::map<uint32_t, QTimer*> powerOnResetTimers_;
    // Que wiring.powerOnReset ya cayeron a Zero en este build de la
    // simulacion - setLiveSimulation() lo consulta para no rearmar otro
    // pulso solo porque el usuario pauso y reanudo despues de que ya
    // disparo. Se vacia en cada rebuildPowerOnResetTimers().
    std::set<uint32_t> firedPowerOnResets_;
};

} // namespace digitalforge::editor
