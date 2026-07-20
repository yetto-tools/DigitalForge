#include "LogisimImporter.hpp"

#include <QFile>
#include <QPointF>
#include <QXmlStreamReader>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "editor/CircuitDocument.hpp"

namespace digitalforge::formats {

using editor::CircuitDocument;
using editor::ComponentPlacement;
using editor::PinRef;
using editor::WireEndpoint;

namespace {

// Un punto entero en el espacio de coordenadas de Logisim (siempre enteros,
// multiplos de 10 en la practica). Sirve tanto para extremos de <wire> como
// para posiciones de pin derivadas de la geometria de cada tipo de
// componente soportado.
using Point = std::pair<int, int>;

struct ParsedComponent {
    std::string typeId;                // ya resuelto a un typeId de DigitalForge
    Point loc;
    std::vector<Point> pinCoordinates; // mismo orden que ComponentInstance::pins()
    std::string label;                 // atributo "label" de Logisim, si tenia uno; vacio si no
};

struct UnsupportedComponent {
    std::string libDesc;
    std::string name;
};

struct ParseResult {
    std::map<std::string, std::string> libDescById;
    std::string mainCircuitName;
    std::vector<ParsedComponent> components;
    std::vector<UnsupportedComponent> unsupported;
    std::vector<std::pair<Point, Point>> wires;
};

Point parsePoint(const QString& s) {
    QString trimmed = s;
    trimmed.remove(QLatin1Char('(')).remove(QLatin1Char(')'));
    const QStringList parts = trimmed.split(QLatin1Char(','));
    if (parts.size() != 2) {
        throw std::runtime_error("importLogisimCircFile: punto malformado '" + s.toStdString() + "'");
    }
    return {parts[0].toInt(), parts[1].toInt()};
}

// Geometria de pines de una compuerta variadica de 2 entradas, tamano
// "Narrow" (atributo size="30"), orientacion por defecto (este) -- la unica
// combinacion verificada empiricamente hasta ahora (contrastando <comp
// loc="..."> contra los <wire> que terminan exactamente en cada uno de sus
// pines, en un archivo de referencia real; ver LogisimImporter.hpp).
// Devuelve {entrada0, entrada1, salida} en el mismo orden que
// makeVariadicGateDefinition::derivePins (In0, In1, Y). Como las 6
// compuertas soportadas (AND/OR/NAND/NOR/XOR/XNOR) son todas conmutativas
// en sus entradas, no importa cual extremo fisico de Logisim cae en el
// indice 0 o el 1.
std::vector<Point> narrow2InputGatePins(Point loc) {
    return {
        Point{loc.first - 40, loc.second - 10},
        Point{loc.first - 40, loc.second + 10},
        loc,
    };
}

const std::map<std::string, std::string>& gateTypeIdsByLogisimName() {
    static const std::map<std::string, std::string> table = {
        {"AND Gate", "gates.and"}, {"OR Gate", "gates.or"},   {"NAND Gate", "gates.nand"},
        {"NOR Gate", "gates.nor"}, {"XOR Gate", "gates.xor"}, {"XNOR Gate", "gates.xnor"},
    };
    return table;
}

Point dsuFind(std::map<Point, Point>& parent, Point x) {
    const auto it = parent.find(x);
    if (it == parent.end()) {
        parent[x] = x;
        return x;
    }
    if (it->second == x) {
        return x;
    }
    const Point root = dsuFind(parent, it->second);
    it->second = root;
    return root;
}

void dsuUnion(std::map<Point, Point>& parent, Point a, Point b) {
    const Point rootA = dsuFind(parent, a);
    const Point rootB = dsuFind(parent, b);
    if (!(rootA == rootB)) {
        parent[rootA] = rootB;
    }
}

ParseResult parseXml(const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error("importLogisimCircFile: no se pudo abrir '" + path + "'");
    }
    QXmlStreamReader xml(&file);

    ParseResult result;
    bool insideTargetCircuit = false;
    bool sawAnyCircuit = false;

    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QString elementName = xml.name().toString();

        if (elementName == QLatin1String("lib")) {
            const QString id = xml.attributes().value(QLatin1String("name")).toString();
            const QString desc = xml.attributes().value(QLatin1String("desc")).toString();
            result.libDescById[id.toStdString()] = desc.toStdString();
        } else if (elementName == QLatin1String("main")) {
            result.mainCircuitName = xml.attributes().value(QLatin1String("name")).toString().toStdString();
        } else if (elementName == QLatin1String("circuit")) {
            const std::string circuitName = xml.attributes().value(QLatin1String("name")).toString().toStdString();
            sawAnyCircuit = true;
            insideTargetCircuit = result.mainCircuitName.empty() || circuitName == result.mainCircuitName;
        } else if (elementName == QLatin1String("wire") && insideTargetCircuit) {
            const Point from = parsePoint(xml.attributes().value(QLatin1String("from")).toString());
            const Point to = parsePoint(xml.attributes().value(QLatin1String("to")).toString());
            result.wires.emplace_back(from, to);
        } else if (elementName == QLatin1String("comp") && insideTargetCircuit) {
            const std::string libId = xml.attributes().value(QLatin1String("lib")).toString().toStdString();
            const std::string name = xml.attributes().value(QLatin1String("name")).toString().toStdString();
            const Point loc = parsePoint(xml.attributes().value(QLatin1String("loc")).toString());

            std::map<std::string, std::string> attrs;
            while (true) {
                xml.readNext();
                if (xml.atEnd() || xml.hasError()) {
                    break;
                }
                if (xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QLatin1String("comp")) {
                    break;
                }
                if (xml.tokenType() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("a")) {
                    const std::string aName = xml.attributes().value(QLatin1String("name")).toString().toStdString();
                    const std::string aVal = xml.attributes().value(QLatin1String("val")).toString().toStdString();
                    attrs[aName] = aVal;
                }
            }

            const auto libIt = result.libDescById.find(libId);
            const std::string libDesc = libIt != result.libDescById.end() ? libIt->second : libId;

            if (libDesc == "#Wiring" && name == "Pin") {
                const bool isOutput = attrs.count("type") != 0 && attrs.at("type") == "output";
                ParsedComponent c;
                c.typeId = isOutput ? "wiring.output" : "wiring.input";
                c.loc = loc;
                c.pinCoordinates = {loc};
                if (attrs.count("label") != 0) {
                    c.label = attrs.at("label");
                }
                result.components.push_back(std::move(c));
            } else if (libDesc == "#Gates" && gateTypeIdsByLogisimName().count(name) != 0) {
                const std::string inputsAttr = attrs.count("inputs") != 0 ? attrs.at("inputs") : "2";
                const std::string sizeAttr = attrs.count("size") != 0 ? attrs.at("size") : "30";
                if (inputsAttr != "2" || sizeAttr != "30") {
                    result.unsupported.push_back(
                        UnsupportedComponent{libDesc, name + " (inputs=" + inputsAttr + ", size=" + sizeAttr + ")"});
                } else {
                    ParsedComponent c;
                    c.typeId = gateTypeIdsByLogisimName().at(name);
                    c.loc = loc;
                    c.pinCoordinates = narrow2InputGatePins(loc);
                    result.components.push_back(std::move(c));
                }
            } else if (libDesc == "#Base" && name == "Text") {
                // Anotacion de texto pura, sin pines ni rol electrico -- se
                // omite. No es "descartar logica en silencio" (el principio
                // que rige el resto de este importador): un comentario
                // visual no tiene ninguna logica que descartar.
            } else {
                result.unsupported.push_back(UnsupportedComponent{libDesc, name});
            }
        }
    }

    if (xml.hasError()) {
        throw std::runtime_error("importLogisimCircFile: XML invalido en '" + path + "': " +
                                  xml.errorString().toStdString());
    }
    if (!sawAnyCircuit) {
        throw std::runtime_error("importLogisimCircFile: '" + path + "' no contiene ningun <circuit>");
    }
    return result;
}

} // namespace

void importLogisimCircFile(CircuitDocument& document, const std::string& path) {
    ParseResult parsed = parseXml(path);

    if (!parsed.unsupported.empty()) {
        std::set<std::string> distinct;
        for (const UnsupportedComponent& u : parsed.unsupported) {
            distinct.insert(u.libDesc + "/" + u.name);
        }
        std::string message =
            "importLogisimCircFile: '" + path + "' usa componentes sin equivalente todavia en DigitalForge: ";
        bool first = true;
        for (const std::string& s : distinct) {
            if (!first) {
                message += ", ";
            }
            message += s;
            first = false;
        }
        throw std::invalid_argument(message);
    }

    document.clear();

    // Logisim graba coordenadas en una grilla de 10 unidades; la grilla de
    // DigitalForge (ComponentItem::kGridSize) es de 8 -- este factor hace
    // que todo multiplo de 10 caiga exacto sobre un multiplo de 8. Solo
    // afecta la posicion de colocacion de cada componente/punto de union;
    // el trazado real de cada cable lo decide DigitalForge por su cuenta
    // (ver comentario de cabecera).
    constexpr double kScale = 0.8;

    std::vector<uint32_t> componentIds;
    componentIds.reserve(parsed.components.size());
    for (const ParsedComponent& c : parsed.components) {
        ComponentPlacement placement;
        placement.position = QPointF(c.loc.first * kScale, c.loc.second * kScale);
        components::PropertyMap overrides;
        if (!c.label.empty()) {
            overrides["label"] = components::PropertyValue{c.label};
        }
        componentIds.push_back(document.addComponent(c.typeId, overrides, placement));
    }

    std::map<Point, Point> parent;
    for (const auto& [from, to] : parsed.wires) {
        dsuUnion(parent, from, to);
    }

    // Cada pin de cada componente se une (como su propio grupo, si hace
    // falta) al mismo DSU que los extremos de cable -- si un pin nunca
    // aparece como extremo de ningun <wire>, queda solo en su grupo y
    // simplemente no genera ningun cable.
    std::map<Point, std::vector<PinRef>> pinsByRoot;
    for (std::size_t i = 0; i < parsed.components.size(); ++i) {
        const uint32_t componentId = componentIds[i];
        const std::vector<Point>& coords = parsed.components[i].pinCoordinates;
        for (std::size_t pinIndex = 0; pinIndex < coords.size(); ++pinIndex) {
            const Point root = dsuFind(parent, coords[pinIndex]);
            pinsByRoot[root].push_back(PinRef{componentId, static_cast<uint16_t>(pinIndex)});
        }
    }

    for (const auto& [root, pins] : pinsByRoot) {
        if (pins.size() < 2) {
            continue; // pin sin nada conectado del otro lado
        }
        if (pins.size() == 2) {
            document.addWire(pins[0], pins[1]);
            continue;
        }
        // Derivacion real (3 o mas pines en la misma red eléctrica): un
        // punto de union con un cable en estrella hacia cada pin.
        const uint32_t junctionId = document.reserveJunctionId();
        document.addJunctionWithId(junctionId, QPointF(root.first * kScale, root.second * kScale));
        for (const PinRef& pin : pins) {
            document.addWire(pin, WireEndpoint::junction(junctionId));
        }
    }
}

} // namespace digitalforge::formats
