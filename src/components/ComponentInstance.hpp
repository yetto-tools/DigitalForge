#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <vector>

#include "ComponentDefinition.hpp"
#include "ExternalDocumentView.hpp"
#include "Property.hpp"
#include "core/Circuit.hpp"
#include "core/Net.hpp"

namespace digitalforge::components {

// Una instancia colocada de una ComponentDefinition: sus valores de
// propiedad actuales, los pines que esas propiedades derivan, los nets a
// los que esos pines estan cableados y (una vez construida) como participa
// en el nucleo de simulacion. Datos simples mas comportamiento - nunca un
// QObject, de modo que colocar un millon de estas siga siendo economico.
//
// Mantiene un puntero no propietario a su ComponentDefinition; la
// definicion (normalmente propiedad de un ComponentRegistry, que nunca
// elimina entradas) debe sobrevivir a toda instancia creada a partir de ella.
class ComponentInstance {
public:
    ComponentInstance(const ComponentDefinition& definition, uint32_t instanceId, PropertyMap overrides = {});

    [[nodiscard]] const std::string& typeId() const noexcept { return definition_->typeId; }
    [[nodiscard]] uint32_t instanceId() const noexcept { return instanceId_; }
    [[nodiscard]] const ComponentDefinition& definition() const noexcept { return *definition_; }

    [[nodiscard]] const PropertyValue& property(const std::string& id) const;

    // Valida `value` contra el descriptor de la definicion para `id`, y
    // luego lo aplica. Lanza std::invalid_argument si `id` es desconocido o
    // el valor no es valido. Vuelve a derivar los pines y descarta cualquier
    // cableado/binding de simulacion existente, ya que un cambio de
    // propiedad puede cambiar la disposicion de pines.
    void setProperty(const std::string& id, PropertyValue value);

    [[nodiscard]] const std::vector<PinTemplate>& pins() const noexcept { return pins_; }

    // Solo relevante para tipos con deriveExternalPins/buildExternalSimulation
    // (hoy unicamente structural.subcircuit); el resto de los tipos lo
    // ignoran por completo. Quien coloca la instancia (CircuitDocument) lo
    // establece justo despues de construirla/deserializarla.
    void setExternalContext(ExternalContext context) { externalContext_ = std::move(context); }

    // Vuelve a derivar los pines con las propiedades actuales (sin cambiar
    // ninguna) y el ExternalContext ya establecido - descarta el
    // cableado/binding existente si la disposicion de pines cambio, igual
    // que setProperty(). Pensado para cuando el documento *referenciado*
    // por un subcircuito cambia de forma en otro lado (ver
    // ExternalDocumentView) y hay que re-sincronizar esta instancia sin que
    // el usuario haya tocado ninguna de sus propiedades.
    void refreshDerivedPins();

    void bindPin(std::size_t pinIndex, core::NetId net);
    void unbindPin(std::size_t pinIndex);
    [[nodiscard]] core::NetId pinNet(std::size_t pinIndex) const { return pinNets_.at(pinIndex); }
    [[nodiscard]] bool allPinsBound() const noexcept;

    // Registra la logica de esta instancia (si tiene) en `circuit` usando
    // los nets actualmente enlazados a sus pines. Lanza std::logic_error si
    // algun pin sigue sin enlazar.
    void buildSimulation(core::Circuit& circuit);
    [[nodiscard]] const ComponentSimBinding& simBinding() const noexcept { return simBinding_; }

    [[nodiscard]] nlohmann::json toJson() const;

    // Reconstruye una instancia de `definition` a partir del JSON producido
    // previamente por toJson(). El resultado queda sin cablear: el llamador
    // debe invocar bindPin() en cada pin y volver a llamar a
    // buildSimulation() antes de simularla. Lanza std::invalid_argument si
    // el JSON esta mal formado o si el typeId no coincide.
    [[nodiscard]] static ComponentInstance fromJson(const ComponentDefinition& definition, const nlohmann::json& json);

private:
    void derivePins();
    [[nodiscard]] std::vector<PinTemplate> computePins() const;
    // Comun a setProperty()/refreshDerivedPins(): si `newPins` difiere de
    // pins_, lo reemplaza y descarta el cableado/binding de simulacion
    // existente (todos los nets vuelven a kInvalidNet) - un cambio
    // puramente cosmetico deja todo intacto.
    void applyRecomputedPins(std::vector<PinTemplate> newPins);

    const ComponentDefinition* definition_;
    uint32_t instanceId_;
    PropertyMap properties_;
    std::vector<PinTemplate> pins_;
    std::vector<core::NetId> pinNets_;
    ComponentSimBinding simBinding_;
    ExternalContext externalContext_;
};

} // namespace digitalforge::components
