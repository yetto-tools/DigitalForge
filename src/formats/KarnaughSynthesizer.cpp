#include "KarnaughSynthesizer.hpp"

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

#include "components/ComponentInstance.hpp"
#include "editor/CircuitDocument.hpp"
#include "editor/KarnaughMap.hpp"

namespace digitalforge::formats {

using editor::CircuitDocument;
using editor::ComponentPlacement;
using editor::Implicant;
using editor::KarnaughLiteral;
using editor::KarnaughResult;
using editor::PinRef;
using editor::WireEndpoint;

namespace {

constexpr qreal kColumnSpacing = 160.0;
constexpr qreal kRowSpacing = 80.0;
// Separacion entre los carriles verticales de bus de cada variable (ver
// busLaneX() mas abajo). Multiplo de ComponentItem::kGridSize (8.0) para que
// los quiebres que arma este archivo caigan sobre la misma grilla que usa el
// editor -- evita que un cable auto-generado se vea "torcido" un pixel al
// abrirlo despues a mano. No se puede incluir ComponentItem.hpp aca (es de la
// capa Qt Widgets, esto es formats/), asi que se repite el valor.
constexpr qreal kBusLaneSpacing = 16.0;
// Como mucho 4 variables (ver KarnaughMap::minimize()), asi que 4 carriles
// alcanzan siempre y caben comodos en el ancho de una columna.
constexpr int kMaxLanes = 4;

// Posicion X del carril vertical de la variable `variableIndex` dentro del
// hueco que arranca en `baseX` (el mismo valor que antes era el X fijo, unico,
// del punto de union). Centrado en baseX y repartido en incrementos de
// kBusLaneSpacing para que las redes de variables distintas no compartan
// carril -- si lo compartieran, sus buses se superpondrian exactamente igual
// que las diagonales que este esquema reemplaza.
qreal busLaneX(qreal baseX, int variableIndex) {
    const qreal centered = static_cast<qreal>(variableIndex) - (static_cast<qreal>(kMaxLanes) - 1.0) / 2.0;
    return baseX + centered * kBusLaneSpacing;
}

// Un pin consumidor de una red junto con la fila (Y de escena) aproximada del
// componente al que pertenece -- alcanza con la fila del componente, no hace
// falta el offset exacto del pin dentro de el (wireVertices() endereza sola
// el tramo final contra la posicion real del pin; ver WireRouting.hpp).
struct NetPin {
    PinRef pin;
    qreal row = 0.0;
};

// Cablea todos los pines de una misma red logica entre si: 2 pines es un
// cable directo, 3 o mas necesita un punto de union en estrella - mismo
// criterio que formats::importLogisimCircFile (ver LogisimImporter.cpp,
// el "pinsByRoot" de mas abajo de su cuerpo). Menos de 2 pines (una red que
// termino sin ningun consumidor) no deberia poder pasar aca dado como
// arma sus redes synthesizeToCircuit(), pero se tolera en silencio por las
// dudas -- nunca es un error real, solo trabajo de mas que no hizo falta.
//
// Los ramales de la estrella se rutean en BUS: cada uno primero viaja en
// vertical por el carril de `junctionPosition.x()` hasta la fila de su
// consumidor, y recien ahi entra en horizontal al pin. Como los ramales
// comparten ese mismo tramo vertical, se leen como un unico bus con
// derivaciones -- no como N lineas independientes cruzando la pantalla en
// diagonal cada una por su cuenta, que es lo que dejaba el circuito lleno de
// quiebres y cruces cuando una red (tipicamente una entrada, con muchos
// consumidores repartidos en filas muy distintas) se cableaba con un tramo
// recto de un solo codo por rama.
void wireNet(CircuitDocument& document, const std::vector<NetPin>& pins, QPointF junctionPosition) {
    if (pins.size() < 2) {
        return;
    }
    if (pins.size() == 2) {
        document.addWire(pins[0].pin, pins[1].pin);
        return;
    }
    const uint32_t junctionId = document.reserveJunctionId();
    document.addJunctionWithId(junctionId, junctionPosition);
    for (const NetPin& netPin : pins) {
        const uint32_t wireId = document.addWire(netPin.pin, WireEndpoint::junction(junctionId));
        if (std::abs(netPin.row - junctionPosition.y()) > 1.0) {
            document.setWireWaypoints(wireId, {QPointF(junctionPosition.x(), netPin.row)});
        }
    }
}

// Estado compartido entre todas las salidas de una sintesis (una sola, para
// synthesizeToCircuit()): la columna 0 (wiring.input por variable) se arma
// una unica vez en el constructor; la columna 1 (gates.not compartida) se
// arma perezosamente via negatedNetFor(), la primera vez que ALGUNA salida
// necesita esa variable negada sin invertMask. trueNet_/negatedNet_ solo se
// cablean al final (wireAll()), una vez que todas las salidas ya agregaron
// sus consumidores -- por eso esta clase sobrevive a todo el barrido de
// salidas en vez de terminar su trabajo en el constructor.
class SharedInputState {
public:
    SharedInputState(CircuitDocument& target, int variableCount, const std::vector<QString>& variableNames)
        : target_(target),
          trueNet_(static_cast<std::size_t>(variableCount)),
          negatedNet_(static_cast<std::size_t>(variableCount)) {
        for (int i = 0; i < variableCount; ++i) {
            components::PropertyMap overrides;
            overrides["label"] =
                components::PropertyValue{variableNames[static_cast<std::size_t>(i)].toStdString()};
            ComponentPlacement placement;
            const qreal row = i * kRowSpacing;
            placement.position = QPointF(0.0, row);
            const uint32_t inputId = target_.addComponent("wiring.input", overrides, placement);
            trueNet_[static_cast<std::size_t>(i)].push_back(NetPin{PinRef{inputId, 0}, row});
        }
    }

    std::vector<NetPin>& trueNetFor(int variableIndex) { return trueNet_[static_cast<std::size_t>(variableIndex)]; }

    std::vector<NetPin>& negatedNetFor(int variableIndex) {
        auto& slot = negatedNet_[static_cast<std::size_t>(variableIndex)];
        if (!slot.has_value()) {
            ComponentPlacement placement;
            const qreal row = variableIndex * kRowSpacing;
            placement.position = QPointF(kColumnSpacing, row);
            const uint32_t notId = target_.addComponent("gates.not", {}, placement);
            trueNet_[static_cast<std::size_t>(variableIndex)].push_back(NetPin{PinRef{notId, 0}, row}); // A consume el valor verdadero
            slot = std::vector<NetPin>{NetPin{PinRef{notId, 1}, row}};                                   // Y es el origen de la red negada
        }
        return *slot;
    }

    // Cablea todas las redes acumuladas hasta ahora -- llamar una unica vez,
    // despues de procesar TODAS las salidas (ver el comentario de clase).
    void wireAll(int variableCount) {
        for (int i = 0; i < variableCount; ++i) {
            wireNet(target_, trueNet_[static_cast<std::size_t>(i)],
                    QPointF(busLaneX(kColumnSpacing / 2.0, i), i * kRowSpacing));
            if (negatedNet_[static_cast<std::size_t>(i)].has_value()) {
                wireNet(target_, *negatedNet_[static_cast<std::size_t>(i)],
                        QPointF(busLaneX(kColumnSpacing + kColumnSpacing / 2.0, i), i * kRowSpacing));
            }
        }
    }

private:
    CircuitDocument& target_;
    std::vector<std::vector<NetPin>> trueNet_;
    std::vector<std::optional<std::vector<NetPin>>> negatedNet_;
};

// Cuantas filas ocupa el layout de una salida (para acumular la banda de la
// siguiente) -- al menos 1 aunque no tenga grupos seleccionados, para que
// dos salidas consecutivas nunca terminen compartiendo fila.
qreal outputBandHeight(const KarnaughResult& result) {
    return static_cast<qreal>(std::max<std::size_t>(result.selectedGroups.size(), 1)) * kRowSpacing;
}

// Separacion extra entre la banda de una salida y la siguiente, ademas de lo
// que ya ocupa su propio layout (outputBandHeight()) -- sin esto, la ultima
// fila de una salida y la primera de la siguiente quedarian pegadas.
constexpr qreal kOutputBandGap = kRowSpacing;

// Arma las columnas 2 (terminos AND/constante), 3 (combinacion OR) y 4
// (wiring.output llamada `outputName`) de UNA salida -- exactamente la
// misma topologia que antes tenia synthesizeToCircuit() completo, corrida
// `rowBandOffset` hacia abajo y agregando sus consumidores a las redes
// COMPARTIDAS de `shared` en vez de a unas propias (esa es la unica
// diferencia real respecto de la version de una sola salida: la columna 0/1
// no se repite, se le suman consumidores a la que ya existe).
void synthesizeOutputColumns(CircuitDocument& target, const KarnaughResult& result, const QString& outputName,
                              qreal rowBandOffset, SharedInputState& shared, const SynthesisOptions& options) {
    // Columna 2: un termino por grupo seleccionado. `termSource[g]` es el
    // pin (o, para terminos de 1 literal, ninguno - se consume directo de
    // trueNet/negatedNet) que un consumidor de ese termino debe cablear.
    // Los terminos de 2+ literales con alguna entrada negada resuelven esa
    // entrada via invertMask (si options.useInvertMask) empujando el pin
    // directo a trueNet en vez de crear/usar la red negada.
    struct TermSource {
        std::optional<PinRef> ownNet;    // con valor: un net propio (AND o constante) - agregar el consumidor aca
        int literalVariableIndex = -1;   // sin ownNet: que variable consumir directamente (literalNegated decide cual red)
        bool literalNegated = false;
    };
    std::vector<TermSource> termSources;
    termSources.reserve(result.selectedGroups.size());

    for (std::size_t g = 0; g < result.selectedGroups.size(); ++g) {
        const std::vector<KarnaughLiteral> literals = result.selectedGroups[g].literals(result.variableCount);
        if (literals.empty()) {
            // Grupo "siempre 1" (dashMask cubre todas las variables): una
            // fuente constante en vez de una compuerta con 0 entradas.
            ComponentPlacement placement;
            placement.position = QPointF(2 * kColumnSpacing, rowBandOffset + static_cast<qreal>(g) * kRowSpacing);
            components::PropertyMap overrides;
            overrides["value"] = components::PropertyValue{std::string("1")};
            const uint32_t constId = target.addComponent("wiring.constant", overrides, placement);
            termSources.push_back(TermSource{PinRef{constId, 0}, -1, false});
            continue;
        }
        if (literals.size() == 1) {
            // Sin compuerta AND: el termino ES directamente el net
            // (verdadero o negado) de su unico literal.
            termSources.push_back(TermSource{std::nullopt, literals.front().variableIndex, literals.front().negated});
            continue;
        }

        ComponentPlacement placement;
        const qreal gateRow = rowBandOffset + static_cast<qreal>(g) * kRowSpacing;
        placement.position = QPointF(2 * kColumnSpacing, gateRow);
        components::PropertyMap overrides;
        overrides["inputCount"] = components::PropertyValue{static_cast<uint64_t>(literals.size())};
        uint64_t invertMask = 0;
        const uint32_t andId = target.addComponent("gates.and", overrides, placement);
        for (std::size_t k = 0; k < literals.size(); ++k) {
            const KarnaughLiteral& literal = literals[k];
            const PinRef inputPin{andId, static_cast<uint16_t>(k)};
            if (literal.negated && options.useInvertMask) {
                shared.trueNetFor(literal.variableIndex).push_back(NetPin{inputPin, gateRow});
                invertMask |= (uint64_t{1} << k);
            } else if (literal.negated) {
                shared.negatedNetFor(literal.variableIndex).push_back(NetPin{inputPin, gateRow});
            } else {
                shared.trueNetFor(literal.variableIndex).push_back(NetPin{inputPin, gateRow});
            }
        }
        if (invertMask != 0) {
            target.setProperty(andId, "invertMask", components::PropertyValue{invertMask});
        }
        termSources.push_back(TermSource{PinRef{andId, static_cast<uint16_t>(literals.size())}, -1, false});
    }

    // Ayuda a "consumir" un termino (agregarle un pin destino a su red,
    // resolviendo el caso de 1-literal contra trueNet/negatedNet recien
    // arriba) sin duplicar esta rama en cada lugar que combina terminos.
    const auto consumeTerm = [&](const TermSource& term, PinRef consumer, qreal consumerRow) {
        if (term.ownNet.has_value()) {
            // El propio pin fuente ya esta en ninguna parte todavia -- se
            // agrega junto con el consumidor para que wireNet() los una.
            return;
        }
        if (term.literalNegated) {
            shared.negatedNetFor(term.literalVariableIndex).push_back(NetPin{consumer, consumerRow});
        } else {
            shared.trueNetFor(term.literalVariableIndex).push_back(NetPin{consumer, consumerRow});
        }
    };

    // Columna 3: combina los terminos. Sin terminos -> constante 0 directo
    // a la salida; un termino -> se cablea directo (sin gates.or); 2+ ->
    // una gates.or.
    const qreal outputRow = rowBandOffset + (result.selectedGroups.empty()
                                                  ? 0.0
                                                  : (static_cast<qreal>(result.selectedGroups.size() - 1) / 2.0) *
                                                        kRowSpacing);
    components::PropertyMap outputOverrides;
    outputOverrides["label"] = components::PropertyValue{outputName.toStdString()};
    ComponentPlacement outputPlacement;
    outputPlacement.position = QPointF(4 * kColumnSpacing, outputRow);
    const uint32_t outputId = target.addComponent("wiring.output", outputOverrides, outputPlacement);
    const PinRef outputPin{outputId, 0};

    if (termSources.empty()) {
        ComponentPlacement placement;
        placement.position = QPointF(3 * kColumnSpacing, outputRow);
        components::PropertyMap overrides;
        overrides["value"] = components::PropertyValue{std::string("0")};
        const uint32_t constId = target.addComponent("wiring.constant", overrides, placement);
        target.addWire(PinRef{constId, 0}, outputPin);
    } else if (termSources.size() == 1) {
        const TermSource& term = termSources.front();
        if (term.ownNet.has_value()) {
            target.addWire(*term.ownNet, outputPin);
        } else {
            consumeTerm(term, outputPin, outputRow);
        }
    } else {
        ComponentPlacement placement;
        placement.position = QPointF(3 * kColumnSpacing, outputRow);
        components::PropertyMap overrides;
        overrides["inputCount"] = components::PropertyValue{static_cast<uint64_t>(termSources.size())};
        const uint32_t orId = target.addComponent("gates.or", overrides, placement);
        for (std::size_t k = 0; k < termSources.size(); ++k) {
            const PinRef inputPin{orId, static_cast<uint16_t>(k)};
            const TermSource& term = termSources[k];
            if (term.ownNet.has_value()) {
                target.addWire(*term.ownNet, inputPin);
            } else {
                consumeTerm(term, inputPin, outputRow);
            }
        }
        target.addWire(PinRef{orId, static_cast<uint16_t>(termSources.size())}, outputPin);
    }
}

void validateResult(int variableCount, const KarnaughResult& result) {
    if (result.variableCount != variableCount) {
        throw std::invalid_argument(
            "synthesizeMultiOutputToCircuit: un OutputSpec::result.variableCount no coincide con variableCount");
    }
    for (const Implicant& group : result.selectedGroups) {
        for (const KarnaughLiteral& literal : group.literals(variableCount)) {
            if (literal.variableIndex < 0 || literal.variableIndex >= variableCount) {
                throw std::invalid_argument(
                    "synthesizeMultiOutputToCircuit: literal con variableIndex fuera de rango");
            }
        }
    }
}

} // namespace

void synthesizeMultiOutputToCircuit(CircuitDocument& target, int variableCount,
                                     const std::vector<QString>& variableNames, const std::vector<OutputSpec>& outputs,
                                     const SynthesisOptions& options) {
    if (static_cast<int>(variableNames.size()) != variableCount) {
        throw std::invalid_argument("synthesizeMultiOutputToCircuit: variableNames.size() no coincide con variableCount");
    }
    if (outputs.empty()) {
        throw std::invalid_argument("synthesizeMultiOutputToCircuit: outputs no puede estar vacio");
    }
    for (const OutputSpec& output : outputs) {
        validateResult(variableCount, output.result);
    }

    target.clear();
    SharedInputState shared(target, variableCount, variableNames);

    qreal rowBandOffset = 0.0;
    for (const OutputSpec& output : outputs) {
        synthesizeOutputColumns(target, output.result, output.name, rowBandOffset, shared, options);
        rowBandOffset += outputBandHeight(output.result) + kOutputBandGap;
    }

    // Recien ahora, con todos los consumidores de TODAS las salidas ya
    // conocidos, se cablean las redes de cada variable (verdadera y, si se
    // creo, negada) -- ver el comentario de clase de SharedInputState.
    shared.wireAll(variableCount);
}

void synthesizeToCircuit(CircuitDocument& target, const KarnaughResult& result, const SynthesisOptions& options) {
    if (static_cast<int>(result.variableNames.size()) != result.variableCount) {
        throw std::invalid_argument("synthesizeToCircuit: variableNames.size() no coincide con variableCount");
    }
    synthesizeMultiOutputToCircuit(target, result.variableCount, result.variableNames,
                                    {OutputSpec{QStringLiteral("F"), result}}, options);
}

} // namespace digitalforge::formats
