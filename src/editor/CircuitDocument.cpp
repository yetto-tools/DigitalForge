#include "CircuitDocument.hpp"

#include <QTimer>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "components/BasicComponentLibrary.hpp"

namespace digitalforge::editor {

namespace {

WireEndpoint dsuFind(std::map<WireEndpoint, WireEndpoint>& parent, WireEndpoint x) {
    WireEndpoint& p = parent.at(x);
    if (p == x) {
        return x;
    }
    const WireEndpoint root = dsuFind(parent, p);
    p = root;
    return root;
}

void dsuUnion(std::map<WireEndpoint, WireEndpoint>& parent, WireEndpoint a, WireEndpoint b) {
    const WireEndpoint rootA = dsuFind(parent, a);
    const WireEndpoint rootB = dsuFind(parent, b);
    if (!(rootA == rootB)) {
        parent[rootA] = rootB;
    }
}

} // namespace

CircuitDocument::CircuitDocument(QObject* parent) : QObject(parent) {
    components::registerBasicComponentLibrary(registry_);
    rebuildSimulation();
}

uint32_t CircuitDocument::addComponent(const std::string& typeId, components::PropertyMap overrides,
                                        ComponentPlacement placement) {
    const uint32_t id = nextComponentId_++;
    addComponentWithId(id, typeId, std::move(overrides), placement);
    return id;
}

void CircuitDocument::addComponentWithId(uint32_t id, const std::string& typeId, components::PropertyMap overrides,
                                          ComponentPlacement placement) {
    auto instance = std::make_unique<components::ComponentInstance>(registry_.create(typeId, id, std::move(overrides)));
    // El ExternalContext (si aplica - solo structural.subcircuit lo usa)
    // todavia no existia cuando el constructor de ComponentInstance corrio
    // su primera derivePins() de arriba, asi que hace falta un refresh
    // explicito con el contexto real antes de que este componente participe
    // en rebuildSimulation().
    instance->setExternalContext(makeExternalContext());
    instance->refreshDerivedPins();
    components_[id] = std::move(instance);
    placements_[id] = placement;
    nextComponentId_ = std::max(nextComponentId_, id + 1);
    emit componentAdded(id);
    rebuildSimulation();
}

void CircuitDocument::removeComponent(uint32_t componentId) {
    if (!components_.contains(componentId)) {
        return;
    }
    for (const WireConnection& w : wiresAttachedToComponent(componentId)) {
        eraseWireCascading(w.id);
    }
    emit componentAboutToBeRemoved(componentId);
    components_.erase(componentId);
    placements_.erase(componentId);
    rebuildSimulation();
}

void CircuitDocument::clear() {
    for (const uint32_t id : componentIds()) {
        removeComponent(id);
    }
}

ComponentPlacement CircuitDocument::componentPlacement(uint32_t componentId) const {
    const auto it = placements_.find(componentId);
    return it == placements_.end() ? ComponentPlacement{} : it->second;
}

void CircuitDocument::setComponentPlacement(uint32_t componentId, ComponentPlacement placement) {
    placements_[componentId] = placement;
    emit componentPlacementChanged(componentId);
}

components::ComponentInstance* CircuitDocument::component(uint32_t componentId) {
    const auto it = components_.find(componentId);
    return it == components_.end() ? nullptr : it->second.get();
}

const components::ComponentInstance* CircuitDocument::component(uint32_t componentId) const {
    const auto it = components_.find(componentId);
    return it == components_.end() ? nullptr : it->second.get();
}

std::vector<uint32_t> CircuitDocument::componentIds() const {
    std::vector<uint32_t> ids;
    ids.reserve(components_.size());
    for (const auto& [id, instance] : components_) {
        ids.push_back(id);
    }
    return ids;
}

uint32_t CircuitDocument::addWire(WireEndpoint a, WireEndpoint b) {
    if (a == b) {
        throw std::invalid_argument("addWire: cannot connect an endpoint to itself");
    }
    if (!a.isJunction) {
        const auto* compA = component(a.id);
        if (compA == nullptr || a.pinIndex >= compA->pins().size()) {
            throw std::invalid_argument("addWire: unknown component or pin index out of range");
        }
    } else if (!junctions_.contains(a.id)) {
        throw std::invalid_argument("addWire: unknown junction");
    }
    if (!b.isJunction) {
        const auto* compB = component(b.id);
        if (compB == nullptr || b.pinIndex >= compB->pins().size()) {
            throw std::invalid_argument("addWire: unknown component or pin index out of range");
        }
    } else if (!junctions_.contains(b.id)) {
        throw std::invalid_argument("addWire: unknown junction");
    }

    const uint32_t id = nextWireId_++;
    wires_[id] = WireConnection{id, a, b, {}};
    emit wireAdded(id);
    rebuildSimulation();
    return id;
}

void CircuitDocument::removeWire(uint32_t wireId) {
    if (!wires_.contains(wireId)) {
        return;
    }
    eraseWireCascading(wireId);
    rebuildSimulation();
}

void CircuitDocument::restoreWire(const WireConnection& wire) {
    wires_[wire.id] = wire;
    nextWireId_ = std::max(nextWireId_, wire.id + 1);
    emit wireAdded(wire.id);
    rebuildSimulation();
}

void CircuitDocument::setWireWaypoints(uint32_t wireId, std::vector<QPointF> waypoints) {
    const auto it = wires_.find(wireId);
    if (it == wires_.end()) {
        return;
    }
    it->second.waypoints = std::move(waypoints);
    emit wireGeometryChanged(wireId);
}

bool CircuitDocument::retargetWire(uint32_t wireId, bool endIsA, WireEndpoint newEndpoint) {
    const auto it = wires_.find(wireId);
    if (it == wires_.end()) {
        return false;
    }
    // El nuevo destino debe existir.
    if (!newEndpoint.isJunction) {
        const auto* comp = component(newEndpoint.id);
        if (comp == nullptr || newEndpoint.pinIndex >= comp->pins().size()) {
            return false;
        }
    } else if (!junctions_.contains(newEndpoint.id)) {
        return false;
    }

    WireConnection updated = it->second;
    const WireEndpoint old = endIsA ? updated.a : updated.b;
    (endIsA ? updated.a : updated.b) = newEndpoint;
    if (updated.a == updated.b) {
        return false; // no se permite un cable conectado a si mismo
    }

    // Recrear el WireItem para que la vista tome la nueva ancla: mismo id, se
    // emite quitar+agregar. No se toca el otro extremo.
    emit wireAboutToBeRemoved(wireId);
    wires_[wireId] = updated;
    emit wireAdded(wireId);

    // Si el extremo viejo era un punto de union y este era su ultimo cable,
    // queda huerfano (grado 0): se elimina, igual que eraseWireCascading().
    if (old.isJunction && wiresAttachedToJunction(old.id).empty()) {
        emit junctionAboutToBeRemoved(old.id);
        junctions_.erase(old.id);
    }

    rebuildSimulation();
    return true;
}

const WireConnection* CircuitDocument::wire(uint32_t wireId) const {
    const auto it = wires_.find(wireId);
    return it == wires_.end() ? nullptr : &it->second;
}

std::vector<uint32_t> CircuitDocument::wireIds() const {
    std::vector<uint32_t> ids;
    ids.reserve(wires_.size());
    for (const auto& [id, w] : wires_) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<WireConnection> CircuitDocument::wiresAttachedToComponent(uint32_t componentId) const {
    std::vector<WireConnection> result;
    for (const auto& [id, w] : wires_) {
        if ((!w.a.isJunction && w.a.id == componentId) || (!w.b.isJunction && w.b.id == componentId)) {
            result.push_back(w);
        }
    }
    return result;
}

bool CircuitDocument::pinHasWire(PinRef pin) const {
    const WireEndpoint endpoint{pin};
    for (const auto& [id, w] : wires_) {
        if (w.a == endpoint || w.b == endpoint) {
            return true;
        }
    }
    return false;
}

void CircuitDocument::addJunctionWithId(uint32_t junctionId, QPointF position) {
    junctions_[junctionId] = position;
    nextJunctionId_ = std::max(nextJunctionId_, junctionId + 1);
    emit junctionAdded(junctionId);
}

void CircuitDocument::removeJunction(uint32_t junctionId) {
    if (!junctions_.contains(junctionId) || !wiresAttachedToJunction(junctionId).empty()) {
        return;
    }
    emit junctionAboutToBeRemoved(junctionId);
    junctions_.erase(junctionId);
}

QPointF CircuitDocument::junctionPosition(uint32_t junctionId) const {
    const auto it = junctions_.find(junctionId);
    return it == junctions_.end() ? QPointF{} : it->second;
}

void CircuitDocument::setJunctionPosition(uint32_t junctionId, QPointF position) {
    const auto it = junctions_.find(junctionId);
    if (it == junctions_.end()) {
        return;
    }
    it->second = position;
    emit junctionPositionChanged(junctionId);
}

std::vector<uint32_t> CircuitDocument::junctionIds() const {
    std::vector<uint32_t> ids;
    ids.reserve(junctions_.size());
    for (const auto& [id, position] : junctions_) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<WireConnection> CircuitDocument::wiresAttachedToJunction(uint32_t junctionId) const {
    std::vector<WireConnection> result;
    const WireEndpoint endpoint = WireEndpoint::junction(junctionId);
    for (const auto& [id, w] : wires_) {
        if (w.a == endpoint || w.b == endpoint) {
            result.push_back(w);
        }
    }
    return result;
}

void CircuitDocument::eraseWireCascading(uint32_t wireId) {
    const auto it = wires_.find(wireId);
    if (it == wires_.end()) {
        return;
    }
    const WireConnection wire = it->second;
    emit wireAboutToBeRemoved(wireId);
    wires_.erase(it);

    for (const WireEndpoint& endpoint : {wire.a, wire.b}) {
        if (!endpoint.isJunction) {
            continue;
        }
        if (wiresAttachedToJunction(endpoint.id).empty()) {
            emit junctionAboutToBeRemoved(endpoint.id);
            junctions_.erase(endpoint.id);
        }
    }
}

std::vector<WireConnection> CircuitDocument::setProperty(uint32_t componentId, const std::string& propertyId,
                                                          components::PropertyValue value) {
    components::ComponentInstance* instance = component(componentId);
    if (instance == nullptr) {
        throw std::invalid_argument("setProperty: unknown component");
    }

    if (instance->typeId() == "structural.subcircuit" && propertyId == "targetPath") {
        validateSubcircuitTarget(std::get<std::string>(value));
    }

    const components::PropertyDescriptor* descriptor = instance->definition().findProperty(propertyId);
    const std::vector<components::PinTemplate> oldPins = instance->pins();
    instance->setProperty(propertyId, std::move(value)); // lanza excepcion si el valor es invalido; en tal caso nada de lo siguiente se ejecuta

    std::vector<WireConnection> removedWires;
    const bool pinsChanged = instance->pins() != oldPins;
    if (pinsChanged) {
        const std::size_t newPinCount = instance->pins().size();
        for (const WireConnection& w : wiresAttachedToComponent(componentId)) {
            const bool aInvalid = !w.a.isJunction && w.a.id == componentId && w.a.pinIndex >= newPinCount;
            const bool bInvalid = !w.b.isJunction && w.b.id == componentId && w.b.pinIndex >= newPinCount;
            if (aInvalid || bInvalid) {
                removedWires.push_back(w);
                eraseWireCascading(w.id);
            }
        }
    }
    // Reconstruye si cambio la disposicion de pines (siempre necesario) o si
    // la propiedad esta marcada affectsSimulation (p. ej. "initialValue" de
    // wiring.input, "invertMask", "variant" del driver BCD, "activeHigh") -
    // sin esto, el cambio quedaba guardado en la propiedad pero el
    // core::Simulator en ejecucion seguia con el valor viejo hasta el
    // proximo rebuild "de casualidad" (otro cambio que si alterara pines) o
    // hasta Reiniciar/recargar el proyecto (el bug reportado: "los valores
    // default no se estan aplicando"). setProperty() solo es alcanzable en
    // pausa (PropertyInspector exige requireEditable() antes de llamarlo),
    // asi que reconstruir aca es tan seguro como pulsar Reiniciar.
    if (pinsChanged || (descriptor != nullptr && descriptor->affectsSimulation)) {
        rebuildSimulation();
    }
    emit propertyChanged(componentId);
    return removedWires;
}

void CircuitDocument::rebuildSimulation() {
    circuit_ = std::make_unique<core::Circuit>();
    pinToNet_.clear();
    junctionToNet_.clear();

    // Union-find sobre WireEndpoint (pines de componente + puntos de union
    // libres): un cable entre cualquier combinacion de ambos los agrupa en
    // la misma red. Cada raiz recibe un NetId propio, tenga o no algun pin
    // (una raiz sin pines es un tramo de cable colgado entre puntos de union
    // nada mas -- sin driver, igual que cualquier net no conectado hoy, pero
    // igual necesita un NetId valido para que endpointValue() tenga algo que
    // devolver).
    std::map<WireEndpoint, WireEndpoint> parent;
    for (const auto& [id, instance] : components_) {
        for (uint16_t p = 0; p < instance->pins().size(); ++p) {
            const WireEndpoint ref{PinRef{id, p}};
            parent[ref] = ref;
        }
    }
    for (const auto& [junctionId, position] : junctions_) {
        const WireEndpoint ref = WireEndpoint::junction(junctionId);
        parent[ref] = ref;
    }
    for (const auto& [wireId, w] : wires_) {
        dsuUnion(parent, w.a, w.b);
    }

    // wiring.tunnel: dos o mas instancias que comparten la misma "label" (no
    // vacia) quedan unidas a la misma net sin necesidad de un cable dibujado
    // - se resuelve con el mismo union-find de arriba, agrupando por el
    // valor de esa propiedad.
    std::map<std::string, WireEndpoint> tunnelByLabel;
    for (const auto& [id, instance] : components_) {
        if (instance->typeId() != "wiring.tunnel") {
            continue;
        }
        const std::string& label = std::get<std::string>(instance->property("label"));
        if (label.empty()) {
            continue;
        }
        const WireEndpoint ref{PinRef{id, 0}};
        const auto [it, inserted] = tunnelByLabel.try_emplace(label, ref);
        if (!inserted) {
            dsuUnion(parent, it->second, ref);
        }
    }

    // Uniones geometricas provistas por la vista (coincidencia en la misma
    // celda de grilla, o derivacion en T donde un extremo cae sobre el cuerpo
    // de otro cable). Se guarda contra extremos ya inexistentes (provider
    // desfasado) verificando que ambos esten sembrados en el union-find.
    if (geometricConnectionProvider_) {
        for (const auto& [a, b] : geometricConnectionProvider_()) {
            if (parent.contains(a) && parent.contains(b)) {
                dsuUnion(parent, a, b);
            }
        }
    }

    std::map<WireEndpoint, core::NetId> rootToNet;
    auto netForRoot = [&](const WireEndpoint& root) {
        const auto [it, inserted] = rootToNet.try_emplace(root, core::NetId{});
        if (inserted) {
            it->second = circuit_->addNet();
        }
        return it->second;
    };
    for (const auto& [id, instance] : components_) {
        for (uint16_t p = 0; p < instance->pins().size(); ++p) {
            const WireEndpoint ref{PinRef{id, p}};
            const core::NetId net = netForRoot(dsuFind(parent, ref));
            pinToNet_[PinRef{id, p}] = net;
            instance->bindPin(p, net);
        }
    }
    for (const auto& [junctionId, position] : junctions_) {
        const WireEndpoint ref = WireEndpoint::junction(junctionId);
        junctionToNet_[junctionId] = netForRoot(dsuFind(parent, ref));
    }

    for (const auto& [id, instance] : components_) {
        instance->buildSimulation(*circuit_);
    }

    simulator_ = std::make_unique<core::Simulator>(*circuit_);
    applyInputInitialValues();
    // Un circuito recien reconstruido debe estabilizarse al menos una vez
    // para que las constantes y los valores iniciales de las entradas
    // queden realmente reflejados (setInput() solo encola eventos; nunca los
    // procesa). Esto se ejecuta sin importar liveSimulation_, que solo rige
    // si los cambios interactivos *posteriores* se propagan automaticamente.
    simulator_->runUntilStable();
    rebuildClockTimers();
    rebuildPowerOnResetTimers();
    emit simulationRebuilt();
}

void CircuitDocument::applyInputInitialValues() {
    if (!simulator_) {
        return;
    }
    for (const auto& [id, instance] : components_) {
        if (instance->typeId() == "wiring.input") {
            core::LogicValue value = components::inputInitialValue(*instance);
            if (value == core::LogicValue::HighImpedance) {
                value = positiveLogicPolarity_ ? core::LogicValue::Zero : core::LogicValue::One;
            }
            simulator_->setInput(instance->simBinding().gateIndex, value);
        } else if (instance->typeId() == "wiring.clock") {
            // Un reloj real siempre arranca en un nivel conocido - sin
            // propiedad de valor inicial como wiring.input, simplemente Zero.
            simulator_->setInput(instance->simBinding().gateIndex, core::LogicValue::Zero);
        } else if (instance->typeId() == "wiring.powerOnReset") {
            // Arranca "reseteando" (One) - rebuildPowerOnResetTimers() arma
            // el QTimer de un solo disparo que lo hace caer a Zero mas
            // adelante, pasado "pulseMs".
            simulator_->setInput(instance->simBinding().gateIndex, core::LogicValue::One);
        }
    }
}

void CircuitDocument::rebuildClockTimers() {
    for (auto& [id, timer] : clockTimers_) {
        timer->stop();
        timer->deleteLater();
    }
    clockTimers_.clear();
    for (const auto& [id, instance] : components_) {
        if (instance->typeId() != "wiring.clock") {
            continue;
        }
        const auto periodMs = std::get<uint64_t>(instance->property("periodMs"));
        auto* timer = new QTimer(this);
        // Medio periodo = un flanco (0->1 o 1->0); minimo 1ms para que un
        // periodMs muy chico no termine en un intervalo de QTimer de 0.
        timer->setInterval(static_cast<int>(std::max<uint64_t>(periodMs / 2, 1)));
        connect(timer, &QTimer::timeout, this, [this, id] { onClockTimeout(id); });
        clockTimers_[id] = timer;
        if (liveSimulation_) {
            timer->start();
        }
    }
}

void CircuitDocument::onClockTimeout(uint32_t componentId) {
    if (!simulator_) {
        return;
    }
    components::ComponentInstance* instance = component(componentId);
    if (instance == nullptr) {
        return;
    }
    const core::LogicValue next =
        pinValue(componentId, 0) == core::LogicValue::One ? core::LogicValue::Zero : core::LogicValue::One;
    simulator_->setInput(instance->simBinding().gateIndex, next);
    runIfLive();
}

void CircuitDocument::rebuildPowerOnResetTimers() {
    for (auto& [id, timer] : powerOnResetTimers_) {
        timer->stop();
        timer->deleteLater();
    }
    powerOnResetTimers_.clear();
    firedPowerOnResets_.clear();
    for (const auto& [id, instance] : components_) {
        if (instance->typeId() != "wiring.powerOnReset") {
            continue;
        }
        const auto pulseMs = std::get<uint64_t>(instance->property("pulseMs"));
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(static_cast<int>(std::max<uint64_t>(pulseMs, 1)));
        connect(timer, &QTimer::timeout, this, [this, id] { onPowerOnResetTimeout(id); });
        powerOnResetTimers_[id] = timer;
        if (liveSimulation_) {
            timer->start();
        }
    }
}

void CircuitDocument::onPowerOnResetTimeout(uint32_t componentId) {
    firedPowerOnResets_.insert(componentId);
    if (!simulator_) {
        return;
    }
    components::ComponentInstance* instance = component(componentId);
    if (instance == nullptr) {
        return;
    }
    simulator_->setInput(instance->simBinding().gateIndex, core::LogicValue::Zero);
    runIfLive();
}

void CircuitDocument::setPositiveLogicPolarity(bool positive) {
    if (positiveLogicPolarity_ == positive) {
        return;
    }
    positiveLogicPolarity_ = positive;
    emit positiveLogicPolarityChanged(positive);
}

bool CircuitDocument::requireEditable(const QString& action) {
    if (!liveSimulation_) {
        return true;
    }
    emit editBlocked(QString("No se puede %1 mientras la simulacion esta en ejecucion. Pausa primero.").arg(action));
    return false;
}

void CircuitDocument::setLiveSimulation(bool live) {
    liveSimulation_ = live;
    // Pausar debe congelar cualquier wiring.clock tal como congela el
    // resto de la propagacion - reanudar los retoma desde donde quedaron
    // (QTimer::start() en un timer ya corriendo simplemente reinicia la
    // cuenta, no hay flancos perdidos que recuperar).
    for (auto& [id, timer] : clockTimers_) {
        if (live) {
            timer->start();
        } else {
            timer->stop();
        }
    }
    // wiring.powerOnReset es de un solo disparo: a diferencia del reloj, si
    // ya cayo a Zero (ver firedPowerOnResets_) pausar/reanudar no debe
    // rearmar otro pulso - solo una reconstruccion completa (rebuildSimulation(),
    // p. ej. "Reiniciar") vuelve a "encender". Igual que el reloj, reanudar
    // antes de que dispare reinicia la cuenta completa (no hay conteo de
    // tiempo real entre pausas en todo el proyecto).
    for (auto& [id, timer] : powerOnResetTimers_) {
        if (live && firedPowerOnResets_.find(id) == firedPowerOnResets_.end()) {
            timer->start();
        } else {
            timer->stop();
        }
    }
    if (live) {
        runIfLive();
    }
    emit liveSimulationChanged(live);
}

void CircuitDocument::step() {
    if (!simulator_) {
        return;
    }
    simulator_->step();
    emit simulationStepped();
}

bool CircuitDocument::runUntilStable() {
    if (!simulator_) {
        return false;
    }
    const bool stable = simulator_->runUntilStable();
    emit simulationStepped();
    return stable;
}

void CircuitDocument::setInputValue(uint32_t componentId, core::LogicValue value) {
    components::ComponentInstance* instance = component(componentId);
    if (instance == nullptr || instance->typeId() != "wiring.input") {
        throw std::invalid_argument("setInputValue: component is not a wiring.input");
    }
    if (!simulator_) {
        return;
    }
    simulator_->setInput(instance->simBinding().gateIndex, value);
    runIfLive();
}

void CircuitDocument::runIfLive() {
    if (liveSimulation_ && simulator_) {
        simulator_->runUntilStable();
    }
    emit simulationStepped();
}

core::LogicValue CircuitDocument::pinValue(uint32_t componentId, uint16_t pinIndex) const {
    return endpointValue(PinRef{componentId, pinIndex});
}

core::LogicValue CircuitDocument::endpointValue(WireEndpoint endpoint) const {
    if (!simulator_) {
        return core::LogicValue::HighImpedance;
    }
    if (endpoint.isJunction) {
        const auto it = junctionToNet_.find(endpoint.id);
        return it == junctionToNet_.end() ? core::LogicValue::HighImpedance : simulator_->getNetValue(it->second);
    }
    const auto it = pinToNet_.find(PinRef{endpoint.id, endpoint.pinIndex});
    if (it == pinToNet_.end()) {
        return core::LogicValue::HighImpedance;
    }
    return simulator_->getNetValue(it->second);
}

bool CircuitDocument::oscillationDetected() const noexcept {
    return simulator_ ? simulator_->stats().oscillationDetected : false;
}

std::vector<CircuitDocument::BoundaryEntry> CircuitDocument::boundaryEntries() const {
    std::vector<BoundaryEntry> entries;
    for (const auto& [id, instance] : components_) {
        if (instance->typeId() == "wiring.input") {
            entries.push_back({id, true});
        } else if (instance->typeId() == "wiring.output") {
            entries.push_back({id, false});
        }
    }
    // Orden estable de pines de frontera: posicion en el lienzo (Y y luego
    // X), no el orden de creacion - ver la decision de diseno en el plan de
    // Fase 4.
    std::sort(entries.begin(), entries.end(), [this](const BoundaryEntry& a, const BoundaryEntry& b) {
        const QPointF pa = componentPlacement(a.componentId).position;
        const QPointF pb = componentPlacement(b.componentId).position;
        return std::pair(pa.y(), pa.x()) < std::pair(pb.y(), pb.x());
    });
    return entries;
}

std::vector<components::PinTemplate> CircuitDocument::boundaryPins() const {
    std::vector<components::PinTemplate> pins;
    std::size_t inIndex = 0;
    std::size_t outIndex = 0;
    for (const BoundaryEntry& entry : boundaryEntries()) {
        const std::string& label = std::get<std::string>(component(entry.componentId)->property("label"));
        if (entry.isInput) {
            pins.push_back({label.empty() ? ("In" + std::to_string(inIndex)) : label, core::PinDirection::Input});
            ++inIndex;
        } else {
            pins.push_back({label.empty() ? ("Out" + std::to_string(outIndex)) : label, core::PinDirection::Output});
            ++outIndex;
        }
    }
    return pins;
}

uint32_t CircuitDocument::flattenInto(core::Circuit& circuit, std::span<const core::NetId> pinNets) const {
    // Mismo union-find que rebuildSimulation(), pero las fronteras se alian
    // a `pinNets` (el net externo del pin del subcircuito que las coloco)
    // en vez de recibir un net nuevo propio.
    std::map<WireEndpoint, WireEndpoint> parent;
    for (const auto& [id, instance] : components_) {
        for (uint16_t p = 0; p < instance->pins().size(); ++p) {
            const WireEndpoint ref{PinRef{id, p}};
            parent[ref] = ref;
        }
    }
    for (const auto& [junctionId, position] : junctions_) {
        const WireEndpoint ref = WireEndpoint::junction(junctionId);
        parent[ref] = ref;
    }
    for (const auto& [wireId, w] : wires_) {
        dsuUnion(parent, w.a, w.b);
    }

    const std::vector<BoundaryEntry> boundary = boundaryEntries();
    std::map<WireEndpoint, core::NetId> rootToNet;
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        // wiring.input/wiring.output tienen un unico pin, indice 0.
        const WireEndpoint ref{PinRef{boundary[i].componentId, 0}};
        rootToNet[dsuFind(parent, ref)] = pinNets[i];
    }
    auto netForRoot = [&](const WireEndpoint& root) {
        const auto [it, inserted] = rootToNet.try_emplace(root, core::NetId{});
        if (inserted) {
            it->second = circuit.addNet();
        }
        return it->second;
    };

    uint32_t firstGateIndex = 0;
    bool haveFirstGateIndex = false;
    for (const auto& [id, instance] : components_) {
        // Los de frontera no construyen nada propio: su unico rol es marcar
        // donde esta el borde. En particular, el GateType::InputPin de un
        // wiring.input interno NUNCA se agrega aca - lo agregaria como
        // segundo driver del mismo net que ya impulsa quien cablee este
        // subcircuito desde afuera.
        if (instance->typeId() == "wiring.input" || instance->typeId() == "wiring.output") {
            continue;
        }
        components::PropertyMap overrides;
        for (const components::PropertyDescriptor& descriptor : instance->definition().properties) {
            overrides[descriptor.id] = instance->property(descriptor.id);
        }
        components::ComponentInstance clone(instance->definition(), id, overrides);
        for (uint16_t p = 0; p < clone.pins().size(); ++p) {
            const WireEndpoint ref{PinRef{id, p}};
            clone.bindPin(p, netForRoot(dsuFind(parent, ref)));
        }
        clone.buildSimulation(circuit);
        if (!haveFirstGateIndex) {
            firstGateIndex = clone.simBinding().gateIndex;
            haveFirstGateIndex = true;
        }
    }
    return firstGateIndex;
}

bool CircuitDocument::containsSubcircuit() const {
    for (const auto& [id, instance] : components_) {
        if (instance->typeId() == "structural.subcircuit") {
            return true;
        }
    }
    return false;
}

components::ExternalContext CircuitDocument::makeExternalContext() const {
    if (!siblingResolver_) {
        return {};
    }
    const auto resolver = siblingResolver_;
    return components::ExternalContext{
        [resolver](const std::string& targetPath) -> const components::ExternalDocumentView* {
            return resolver(QString::fromStdString(targetPath));
        }};
}

void CircuitDocument::validateSubcircuitTarget(const std::string& targetPath) const {
    if (targetPath.empty()) {
        return; // volver al estado "sin documento" siempre es valido
    }
    if (!siblingResolver_) {
        throw std::invalid_argument(
            "setProperty: no hay documentos hermanos disponibles todavia (guarda el proyecto primero)");
    }
    const CircuitDocument* target = siblingResolver_(QString::fromStdString(targetPath));
    if (target == nullptr) {
        throw std::invalid_argument("setProperty: '" + targetPath + "' no corresponde a ningun documento del proyecto");
    }
    if (target == this) {
        throw std::invalid_argument("setProperty: un documento no puede usarse como subcircuito de si mismo");
    }
    if (target->containsSubcircuit()) {
        throw std::invalid_argument(
            "setProperty: ese documento ya contiene un subcircuito (no se admite mas de un nivel de anidamiento)");
    }
}

void CircuitDocument::setSiblingResolver(std::function<const CircuitDocument*(const QString&)> resolver) {
    siblingResolver_ = std::move(resolver);
    bool any = false;
    for (const auto& [id, instance] : components_) {
        if (instance->typeId() == "structural.subcircuit") {
            instance->setExternalContext(makeExternalContext());
            instance->refreshDerivedPins();
            any = true;
        }
    }
    if (any) {
        rebuildSimulation();
    }
}

void CircuitDocument::setGeometricConnectionProvider(GeometricConnectionProvider provider) {
    geometricConnectionProvider_ = std::move(provider);
    rebuildSimulation();
}

void CircuitDocument::recomputeConnectivity() { rebuildSimulation(); }

} // namespace digitalforge::editor
