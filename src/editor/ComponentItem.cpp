#include "ComponentItem.hpp"

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPalette>
#include <QRadialGradient>
#include <QStyleOptionGraphicsItem>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

#include "CircuitDocument.hpp"
#include "CircuitScene.hpp"
#include "GateShapes.hpp"
#include "LogicColors.hpp"
#include "MsiShapes.hpp"
#include "PinItem.hpp"
#include "WireItem.hpp"
#include "components/BasicComponentLibrary.hpp"

namespace digitalforge::editor {

namespace {
qreal snapToGrid(qreal value, qreal grid) { return std::round(value / grid) * grid; }

// Texto del tooltip al pasar el mouse sobre el componente: nombre + (si la
// tiene) descripcion del tipo, mas el valor actual de cada propiedad
// *relevante para la simulacion* (descriptor.affectsSimulation) -- p. ej.
// "Cantidad de entradas"/"Valor inicial"/"Periodo (ms)", nunca propiedades
// puramente de configuracion visual (Etiqueta, Color del cuerpo, Ancho/Alto
// personalizado, Notas) que solo importan en ui::PropertyInspector. Las
// propiedades de texto vacias se omiten en vez de mostrar una linea sin
// valor util.
QString describeComponentForTooltip(const components::ComponentInstance& instance) {
    const components::ComponentDefinition& definition = instance.definition();
    QString html = QStringLiteral("<b>%1</b>").arg(QString::fromStdString(definition.displayName).toHtmlEscaped());
    if (!definition.description.empty()) {
        html += QStringLiteral("<br>%1").arg(QString::fromStdString(definition.description).toHtmlEscaped());
    }
    for (const components::PropertyDescriptor& descriptor : definition.properties) {
        if (!descriptor.affectsSimulation) {
            continue;
        }
        const components::PropertyValue& value = instance.property(descriptor.id);
        QString valueText;
        if (const auto* asBool = std::get_if<bool>(&value)) {
            valueText = *asBool ? QStringLiteral("si") : QStringLiteral("no");
        } else if (const auto* asInt = std::get_if<int64_t>(&value)) {
            valueText = QString::number(*asInt);
        } else if (const auto* asUint = std::get_if<uint64_t>(&value)) {
            valueText = QString::number(*asUint);
        } else if (const auto* asString = std::get_if<std::string>(&value)) {
            valueText = QString::fromStdString(*asString);
        }
        if (valueText.isEmpty()) {
            continue;
        }
        html += QStringLiteral("<br>%1: %2")
                    .arg(QString::fromStdString(descriptor.displayName).toHtmlEscaped(), valueText.toHtmlEscaped());
    }
    return html;
}

// Ubica, por nombre, el indice del PinTemplate real que corresponde a una
// entrada funcional de ComponentDefinition::physicalPinout (ver
// splitPhysicalPinout() mas abajo) - derivePins() no cambia su orden
// interno, physicalPinout solo describe donde va cada uno visualmente.
std::optional<uint16_t> findPinIndexByName(const std::vector<components::PinTemplate>& pins,
                                            const std::string& name) {
    for (uint16_t i = 0; i < pins.size(); ++i) {
        if (pins[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

// Divide la secuencia fisica real de un ic74ls.* (ComponentDefinition::
// physicalPinout, pin1->N) en las dos columnas visuales top-a-bottom: la
// izquierda ya viene en ese orden (pin1 arriba, bajando); la derecha se
// invierte, porque el DIP la numera de abajo hacia arriba pero se dibuja
// de arriba hacia abajo igual que la izquierda.
std::pair<std::vector<components::ComponentDefinition::PhysicalPin>,
          std::vector<components::ComponentDefinition::PhysicalPin>>
splitPhysicalPinout(const std::vector<components::ComponentDefinition::PhysicalPin>& physicalPinout) {
    const auto half = static_cast<std::ptrdiff_t>(physicalPinout.size() / 2);
    std::vector<components::ComponentDefinition::PhysicalPin> left(physicalPinout.begin(),
                                                                     physicalPinout.begin() + half);
    std::vector<components::ComponentDefinition::PhysicalPin> right(physicalPinout.begin() + half,
                                                                      physicalPinout.end());
    std::reverse(right.begin(), right.end());
    return {std::move(left), std::move(right)};
}

// Tono base para cada opcion "color" de io.led; paintLed deriva a partir de
// este tanto el matiz encendido como el apagado de la lente, de modo que un
// LED "verde" se vea verde en cualquiera de los dos estados, tal como ocurre
// con la lente plastica de un LED real.
QColor ledBaseColor(const std::string& colorName) {
    if (colorName == "green") return QColor(40, 220, 90);
    if (colorName == "yellow") return QColor(255, 215, 40);
    if (colorName == "blue") return QColor(60, 130, 255);
    if (colorName == "white") return QColor(235, 235, 235);
    return QColor(255, 50, 50); // rojo (por defecto)
}

// Color de relleno del cuerpo, personalizable por instancia via la
// propiedad "bodyColor" (io.led/io.seven_segment/wiring.input no la tienen -
// ver makeBodyColorProperty en BasicComponentLibrary.cpp - asi que siguen
// con su propio esquema de color fijo/dinamico sin pasar por aca).
QColor bodyFillColor(const components::ComponentInstance& instance, const QColor& fallback) {
    if (instance.definition().findProperty("bodyColor") == nullptr) {
        return fallback;
    }
    const QColor color(QString::fromStdString(std::get<std::string>(instance.property("bodyColor"))));
    return color.isValid() ? color : fallback;
}

// Duplica deliberadamente el mismo criterio claro/oscuro que
// ui::IconFactory::inkColor() (ese helper es privado a ese archivo, y
// incluirlo desde aca crearia una dependencia circular editor<->ui) - para
// el texto de la etiqueta de instancia, que se dibuja sobre el fondo del
// lienzo, no sobre un widget.
QColor labelInkColor() {
    const bool dark = QApplication::palette().color(QPalette::Base).lightness() < 128;
    return dark ? QColor(220, 220, 220) : QColor(40, 40, 40);
}

// Simbolo tipo BJT (wiring.transistor/transmissionGate): el/los control(es)
// entran derecho desde el borde izquierdo hasta una barra corta; cada
// terminal del canal (Source/Drain o A/Y, agrupados del lado derecho por
// rebuildPins() - ver el override de leftPins/rightPins ahi mismo) sale de
// la barra por una "pata" en angulo: una diagonal corta seguida de un
// tramo recto hasta su altura real - mismo lenguaje grafico universalmente
// reconocible del simbolo real de un transistor (base a la izquierda,
// colector/emisor en diagonal a la derecha, ver la imagen de referencia).
// Reemplaza dos intentos previos (canal+placa MOSFET, y luego diagonales
// simetricas a ambos lados) que a este tamano no se leian como un
// transistor - el defecto era agrupar por direccion (Source pegado a
// Gate, Drain solo), no la forma en si.
constexpr qreal kMosBarHalfHeight = 8.0;
constexpr qreal kMosDiagonalSplit = 4.0; // separacion entre los 2 puntos de union de las patas sobre la barra
constexpr qreal kMosLegDx = 8.0;         // extension horizontal de la diagonal antes del tramo recto

// Pata en angulo desde un punto de la barra (barX,barY) hasta un terminal
// del canal en el borde derecho, a su altura real `termY` - el primer
// tramo (diagonal) siempre calza justo con esa altura, el resto es un
// tramo recto horizontal normal. Devuelve el punto del quiebre (donde
// termina la diagonal), para poder apoyar ahi la punta de flecha.
QPointF paintMosLeg(QPainter* painter, qreal barX, qreal barY, qreal width, qreal termY, const QColor& ink) {
    const QPointF kink(barX + kMosLegDx, termY);
    painter->setPen(QPen(ink, 1.4));
    painter->drawLine(QPointF(barX, barY), kink);
    painter->drawLine(kink, QPointF(width, termY));
    return kink;
}

// Punta de flecha sobre la diagonal `from`->`to`, a mitad de camino - misma
// convencion NPN/PNP (apunta hacia afuera de la barra en NMOS, hacia
// adentro en PMOS) que el simbolo real de un transistor.
void paintMosArrow(QPainter* painter, QPointF from, QPointF to, bool pointingOutward, const QColor& ink) {
    QPointF dir = to - from;
    const qreal len = std::hypot(dir.x(), dir.y());
    if (len < 0.001) {
        return;
    }
    dir /= len;
    if (!pointingOutward) {
        dir = -dir;
    }
    const QPointF mid = from + (to - from) * 0.5;
    constexpr qreal arrowHalfLen = 3.0;
    constexpr qreal arrowHalfWidth = 2.5;
    const QPointF tip = mid + dir * arrowHalfLen;
    const QPointF back = mid - dir * arrowHalfLen;
    const QPointF normal(-dir.y(), dir.x());
    QPainterPath path;
    path.moveTo(tip);
    path.lineTo(back + normal * arrowHalfWidth);
    path.lineTo(back - normal * arrowHalfWidth);
    path.closeSubpath();
    painter->setBrush(ink);
    painter->setPen(Qt::NoPen);
    painter->drawPath(path);
}

// Un segmento de un display de 7 segmentos real es una barra hexagonal
// alargada con las puntas en angulo (no un simple rectangulo ni una linea de
// borde redondeado) - estas dos funciones construyen esa forma para una
// barra horizontal (a/d/g) y una vertical (b/c/e/f) respectivamente.
// `halfThickness` es el semiancho de la barra y `notch` cuanto se afila cada
// punta hacia el vertice.
QPainterPath buildHorizontalSegment(qreal x1, qreal x2, qreal y, qreal halfThickness, qreal notch) {
    QPainterPath path;
    path.moveTo(x1, y);
    path.lineTo(x1 + notch, y - halfThickness);
    path.lineTo(x2 - notch, y - halfThickness);
    path.lineTo(x2, y);
    path.lineTo(x2 - notch, y + halfThickness);
    path.lineTo(x1 + notch, y + halfThickness);
    path.closeSubpath();
    return path;
}

QPainterPath buildVerticalSegment(qreal x, qreal y1, qreal y2, qreal halfThickness, qreal notch) {
    QPainterPath path;
    path.moveTo(x, y1);
    path.lineTo(x + halfThickness, y1 + notch);
    path.lineTo(x + halfThickness, y2 - notch);
    path.lineTo(x, y2);
    path.lineTo(x - halfThickness, y2 - notch);
    path.lineTo(x - halfThickness, y1 + notch);
    path.closeSubpath();
    return path;
}

// Construye solo el contorno visual de Entrada/Salida. No conoce el
// documento, los pines ni la simulacion: recibe un rectangulo y devuelve un
// path, igual que los helpers de GateShapes.hpp usados por las compuertas.
QPainterPath buildPortShapePath(QRectF rect, bool pointsRight) {
    const qreal tipWidth = rect.height() * 0.4;
    QPainterPath path;
    if (pointsRight) {
        path.moveTo(rect.left(), rect.top());
        path.lineTo(rect.right() - tipWidth, rect.top());
        path.lineTo(rect.right(), rect.center().y());
        path.lineTo(rect.right() - tipWidth, rect.bottom());
        path.lineTo(rect.left(), rect.bottom());
    } else {
        path.moveTo(rect.left(), rect.center().y());
        path.lineTo(rect.left() + tipWidth, rect.top());
        path.lineTo(rect.right(), rect.top());
        path.lineTo(rect.right(), rect.bottom());
        path.lineTo(rect.left() + tipWidth, rect.bottom());
    }
    path.closeSubpath();
    return path;
}

// Estado visual de un segmento/punto de un display de 7 segmentos,
// independiente de si viene de io.seven_segment (7-8 pines crudos, un
// Error posible por segmento via conflicto de wiring) o io.hexDisplay (4
// bits decodificados a un digito 0-F, un unico Error posible para el
// digito entero si algun bit no es 0/1 limpio - ver paintHexDisplay()).
enum class SegmentState { Off, Lit, Error };

// Mismo layout que usaba paintSevenSegment antes de factorizarse: digito
// vertical (mas alto que ancho), margen izquierdo fijo para etiquetas de
// pin, margen derecho fijo para el punto decimal. io.hexDisplay comparte
// este mismo calculo (ver rebuildPins(): comparte width_/pinStubLength_)
// porque es visualmente el mismo dispositivo fisico.
QRectF computeSevenSegmentDigitRect(qreal width, qreal height) {
    constexpr qreal verticalMargin = 12.0;
    constexpr qreal leftMargin = 17.0;
    constexpr qreal rightMargin = 14.0; // lugar para el punto decimal
    constexpr qreal digitAspect = 0.5;  // ancho:alto
    const qreal digitHeight = height - verticalMargin * 2.0;
    const qreal digitWidth = digitHeight * digitAspect;
    const qreal availableWidth = width - leftMargin - rightMargin;
    const qreal digitLeft = leftMargin + std::max(0.0, (availableWidth - digitWidth) / 2.0);
    return QRectF(digitLeft, verticalMargin, digitWidth, digitHeight);
}

// Dibuja el digito de 7 segmentos en si (las 7 barras hexagonales +
// punto decimal opcional), coloreado segun `segments`/`dot` - la parte
// realmente no trivial de paintSevenSegment/paintHexDisplay, compartida
// entre ambas. No dibuja el fondo de la caja, los stubs de pin ni las
// etiquetas: eso sigue siendo especifico de cada llamador (nombres de pin
// distintos: a..g/dot vs bit0..bit3/dot).
void drawSevenSegmentDigit(QPainter* painter, QRectF digitRect, const std::array<SegmentState, 7>& segments,
                           std::optional<SegmentState> dot, QColor litColor = QColor(40, 220, 90)) {
    const qreal midY = digitRect.top() + digitRect.height() / 2.0;
    const qreal halfThickness = std::clamp(digitRect.width() * 0.12, 2.0, 4.0);
    const qreal insetX = std::max(halfThickness * 0.5, 1.0);
    const qreal insetY = std::max(halfThickness * 0.5, 1.0);
    const qreal horizontalRun = (digitRect.width() - 2.0 * insetX);
    const qreal notch = std::clamp(halfThickness, 1.0, horizontalRun * 0.3);

    const std::array<QPainterPath, 7> segmentPaths{
        buildHorizontalSegment(digitRect.left() + insetX, digitRect.right() - insetX, digitRect.top(), halfThickness,
                                notch),                                                                     // a
        buildVerticalSegment(digitRect.right(), digitRect.top() + insetY, midY - insetY, halfThickness, notch),  // b
        buildVerticalSegment(digitRect.right(), midY + insetY, digitRect.bottom() - insetY, halfThickness,
                              notch),                                                                        // c
        buildHorizontalSegment(digitRect.left() + insetX, digitRect.right() - insetX, digitRect.bottom(),
                                halfThickness, notch),                                                       // d
        buildVerticalSegment(digitRect.left(), midY + insetY, digitRect.bottom() - insetY, halfThickness, notch), // e
        buildVerticalSegment(digitRect.left(), digitRect.top() + insetY, midY - insetY, halfThickness, notch),   // f
        buildHorizontalSegment(digitRect.left() + insetX, digitRect.right() - insetX, midY, halfThickness, notch), // g
    };

    // `litColor` encendido (verde por defecto, personalizable via la
    // propiedad "color" en io.hexDisplay - io.seven_segment no tiene esa
    // propiedad y siempre usa el default) / gris apagado, como un display
    // LED real; rojo se reserva para un conflicto/ambiguedad real.
    const QColor kSegmentOff(90, 90, 90);
    const QColor kSegmentConflict(220, 30, 30);
    const auto colorFor = [&](SegmentState state) {
        switch (state) {
            case SegmentState::Lit:   return litColor;
            case SegmentState::Error: return kSegmentConflict;
            case SegmentState::Off:   return kSegmentOff;
        }
        return kSegmentOff;
    };

    for (std::size_t i = 0; i < segmentPaths.size(); ++i) {
        const QColor segmentColor = colorFor(segments[i]);
        painter->setPen(QPen(segmentColor.darker(140), 1.0));
        painter->setBrush(segmentColor);
        painter->drawPath(segmentPaths[i]);
    }

    if (dot.has_value()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(colorFor(*dot));
        painter->drawEllipse(QPointF(digitRect.right() + 6.0, digitRect.bottom()), 3.0, 3.0);
    }
}
} // namespace

ComponentItem::ComponentItem(CircuitDocument* document, uint32_t componentId, QGraphicsItem* parent)
    : QGraphicsItem(parent), document_(document), componentId_(componentId) {
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    rebuildPins();

    const components::ComponentInstance* instance = document_->component(componentId_);
    if (instance != nullptr && instance->typeId() == "wiring.input") {
        // El unico cuerpo de componente cuyo doble clic conmuta entre 0/1 -
        // un cursor de mano sugiere que es clicable, tal como lo haria un
        // boton real.
        setCursor(Qt::PointingHandCursor);
    }
}

void ComponentItem::rebuildPins() {
    prepareGeometryChange();
    for (PinItem* pin : pinItems_) {
        delete pin;
    }
    pinItems_.clear();
    pinLocalPositions_.clear();
    decorativeLeftPositions_.clear();
    decorativeRightPositions_.clear();

    const components::ComponentInstance* instance = document_->component(componentId_);
    if (instance == nullptr) {
        return;
    }
    setToolTip(describeComponentForTooltip(*instance));

    const auto& pins = instance->pins();

    // io.ledMatrix es el unico tipo cuyos pines no se acomodan en una lista
    // vertical de un solo lado: cada pin ES su propia celda de LED (ver
    // PinItem::paint()), asi que se posiciona directamente sobre la grilla
    // rows x cols en vez de apilarse en el borde izquierdo - apilar los
    // hasta 64 pines en una columna generaba una caja mucho mas alta que la
    // grilla en si, con un tramo enorme de espacio vacio debajo (el defecto
    // reportado). Retorna temprano: el resto de la funcion (leftPins/
    // rightPins, stubs) no aplica a este tipo.
    if (instance->typeId() == "io.ledMatrix") {
        const auto rows = std::get<uint64_t>(instance->property("rows"));
        const auto cols = std::get<uint64_t>(instance->property("cols"));
        constexpr qreal pitch = 16.0;
        constexpr qreal margin = 6.0;
        constexpr qreal cellRadius = pitch * 0.32;
        width_ = std::max<qreal>(32.0, static_cast<qreal>(cols) * pitch);
        height_ = std::max<qreal>(32.0, static_cast<qreal>(rows) * pitch + margin * 2.0);

        const auto customWidth = std::get<uint64_t>(instance->property("customWidth"));
        if (customWidth > 0) {
            width_ = static_cast<qreal>(customWidth);
        }
        const auto customHeight = std::get<uint64_t>(instance->property("customHeight"));
        if (customHeight > 0) {
            height_ = static_cast<qreal>(customHeight);
        }

        pinStubLength_ = 0.0;
        // Recalculado a partir de width_/height_ reales (no la constante
        // `pitch` de arriba) para que la grilla siga encajando si el
        // usuario fijo customWidth/customHeight.
        const qreal pitchX = width_ / static_cast<qreal>(cols);
        const qreal pitchY = (height_ - margin * 2.0) / static_cast<qreal>(rows);
        pinLocalPositions_.assign(pins.size(), QPointF{});
        for (uint64_t row = 0; row < rows; ++row) {
            for (uint64_t col = 0; col < cols; ++col) {
                const auto index = static_cast<std::size_t>(row * cols + col);
                const qreal cx = pitchX * (static_cast<qreal>(col) + 0.5);
                const qreal cy = margin + pitchY * (static_cast<qreal>(row) + 0.5);
                pinLocalPositions_[index] = QPointF(cx, cy);
            }
        }
        for (uint64_t row = 0; row < rows; ++row) {
            for (uint64_t col = 0; col < cols; ++col) {
                const auto index = static_cast<uint16_t>(row * cols + col);
                auto* pin = new PinItem(document_, componentId_, index, pins[index].direction, this, cellRadius);
                pin->setPos(pinLocalPositions_[index]);
                pinItems_.push_back(pin);
            }
        }
        return;
    }

    std::vector<uint16_t> leftPins;
    std::vector<uint16_t> rightPins;
    for (uint16_t i = 0; i < pins.size(); ++i) {
        if (pins[i].direction == core::PinDirection::Output) {
            rightPins.push_back(i);
        } else {
            leftPins.push_back(i);
        }
    }

    // wiring.transistor/transmissionGate necesitan el control(es) solo de
    // un lado y los dos terminales del canal juntos del otro - no
    // agrupados por direccion como el resto de los tipos (que aca dejaba a
    // Source/A pegado a Gate/N/P de un lado y a Drain/Y solo del otro, sin
    // ninguna forma que se leyera como un transistor real - el defecto
    // reportado, resuelto comparando contra el simbolo BJT de referencia:
    // Base sola de un lado, Colector/Emisor juntos del otro). PinDirection
    // no cambia (Source/A siguen siendo Input de verdad en el panel de
    // propiedades) - solo se anula donde rebuildPins() los dibuja, mismo
    // principio ya usado por ic74ls.*/physicalPinout para desacoplar
    // posicion visual de direccion electrica.
    if (instance->typeId() == "wiring.transistor") {
        leftPins = {1};     // Gate
        rightPins = {0, 2}; // Source, Drain
    } else if (instance->typeId() == "wiring.transmissionGate") {
        leftPins = {1, 2};  // N, P
        rightPins = {0, 3}; // A, Y
    }

    // io.hexDisplay comparte exactamente el mismo tratamiento de tamano que
    // io.seven_segment: es visualmente el mismo dispositivo fisico, solo
    // decodificado internamente (ver paintHexDisplay()).
    const bool isSevenSegmentType = instance->typeId() == "io.seven_segment";
    const bool isHexDisplay = instance->typeId() == "io.hexDisplay";
    const bool isSegmentDisplay = isSevenSegmentType || isHexDisplay;
    // wiring.transistor/transmissionGate necesitan mas separacion vertical
    // entre pines que el generico: con el pitch de 8px, el canal (entre las
    // alturas reales de Source/Drain o A/Y) quedaba tan corto que la placa
    // de compuerta (ver paintMosGatePlate(), con su propio piso minimo de
    // alto) terminaba casi tan alta como el canal mismo, sin espacio para
    // que ambos se lean como dos trazos claramente distintos (el defecto
    // reportado: "no se sabe que puede ser").
    const bool isMosLike = instance->typeId() == "wiring.transistor" || instance->typeId() == "wiring.transmissionGate";
    // Multiplos de 8 (igual que kGridSize), para que el ancho de la caja
    // siempre caiga sobre una linea de la grilla de fondo. 32 (en vez de 64)
    // para que el piso de 4 franjas de abajo (32 tambien) de una proporcion
    // cercana a 1:1 en el caso mas comun (1-2 pines) - a 64 de ancho quedaba
    // muy alargado a lo horizontal.
    // ic74ls.* necesita mas ancho que el generico: a diferencia de
    // Plexers/Aritmetica/Memoria (sin etiquetas de pin, fuera de alcance
    // por ahora - ver docs/component-status.md), paintIc74ls() si dibuja el
    // nombre de cada pin (entrada a la izquierda, salida a la derecha), y
    // 32px no alcanza para texto legible en ambos lados.
    const bool isIc74ls = instance->typeId().rfind("ic74ls.", 0) == 0;
    // Secuencia fisica real del DIP (ver ComponentDefinition::
    // physicalPinout), partida en las dos columnas visuales top-a-bottom:
    // izquierda tal cual (pin1 arriba), derecha invertida (el DIP la
    // numera de abajo hacia arriba). Mezcla pines reales (cableables) y
    // decorativos (solo serigrafia) en el orden fisico exacto - ya no se
    // agrupan por direccion como el resto de los tipos.
    const auto [leftSeq, rightSeq] =
        isIc74ls ? splitPhysicalPinout(instance->definition().physicalPinout)
                 : std::pair<std::vector<components::ComponentDefinition::PhysicalPin>,
                             std::vector<components::ComponentDefinition::PhysicalPin>>{};
    // El ancho fijo (52) alcanzaba para bcdDriver (bit0..bit3/a..g, nombres
    // cortos) pero varios de los 13 IC agregados despues tienen nombres de
    // pin bastante mas largos de un lado (p. ej. "Strobe", "CasGT"/"CasEQ"/
    // "CasLT", "G2A"/"G2B") - a 52px quedaban amontonados contra el centro,
    // pisando el rotulado de "Vcc"/"GND" (el defecto reportado). Se mide de
    // verdad con QFontMetricsF (misma fuente/tamano que usa paintIc74ls())
    // en vez de un valor fijo, igual criterio que ya usa paintIc74ls() para
    // las columnas de texto en si.
    qreal icWidth = 52.0;
    if (isIc74ls) {
        QFont pinFont;
        pinFont.setPointSizeF(6.0); // igual tamano que paintIc74ls()
        const QFontMetricsF pinFontMetrics(pinFont);
        qreal leftLabelWidth = 0.0;
        qreal rightLabelWidth = 0.0;
        for (const auto& entry : leftSeq) {
            leftLabelWidth =
                std::max(leftLabelWidth, pinFontMetrics.horizontalAdvance(QString::fromStdString(entry.label)));
        }
        for (const auto& entry : rightSeq) {
            rightLabelWidth =
                std::max(rightLabelWidth, pinFontMetrics.horizontalAdvance(QString::fromStdString(entry.label)));
        }
        // Insets del cuerpo (6) + margen de cada columna (2 c/u) + las dos
        // columnas medidas + hueco central reservado para "Vcc"/"GND"/el
        // numero de parte (22) - redondeado al proximo multiplo de
        // kGridSize, igual criterio que el resto de esta funcion.
        constexpr qreal centralGapReserve = 22.0;
        const qreal rawWidth = 6.0 + 2.0 + leftLabelWidth + centralGapReserve + rightLabelWidth + 2.0;
        icWidth = std::max(52.0, std::ceil(rawWidth / kGridSize) * kGridSize);
    }
    // isMosLike usa un poco mas de ancho que el generico: la "pata" en
    // angulo (diagonal corta + tramo recto, ver paintTransistor()) necesita
    // lugar para no verse amontonada contra el borde derecho.
    width_ = isSegmentDisplay ? 72.0 : isIc74ls ? icWidth : isMosLike ? 40.0 : 32.0;
    const std::size_t sideCount =
        isIc74ls ? std::max(leftSeq.size(), rightSeq.size()) : std::max(leftPins.size(), rightPins.size());
    // El espaciado vertical entre pines es un multiplo de kGridSize (no
    // necesariamente igual a kGridSize) - eso alcanza para garantizar que
    // height_ = pinPitch * heightSlots tambien caiga siempre sobre la
    // grilla, sin importar cuantos pines tenga cada tipo de componente.
    // ic74ls.* usa el doble (16 en vez de 8): con el pitch generico, las
    // hasta 7 etiquetas de pin de un lado (bit0..bit3/a..g) quedaban
    // demasiado juntas entre si, casi superpuestas (el defecto reportado).
    // isMosLike usa el mismo valor: el simbolo de transistor/compuerta de
    // transmision necesita mas separacion vertical entre pines para que la
    // barra de control y las diagonales (ver paintMosBarAndDiagonals()) no
    // queden apretadas.
    const qreal pinPitch = (isIc74ls || isMosLike) ? 12.0 : kGridSize;
    // Un componente de un solo pin (NOT/BUFFER, entrada/salida, LED, sonda)
    // solo necesita 2 "franjas" de pinPitch segun la formula de abajo, dando
    // una caja mas baja que ancha; se impone un piso de 4 franjas (32, igual
    // al ancho de arriba => proporcion 1:1 en el caso mas chico) para que no
    // se vea aplastado sin importar cuantos pines tenga. El 7-segmentos
    // queda afuera de este piso: su alto ya esta dimensionado aparte segun
    // su propia cantidad de pines (sin cambios).
    //
    // io.hexDisplay tiene menos pines propios (4-5: bit0..bit3 + dot
    // opcional) que io.seven_segment (7-8: a..g + dot) - si se dimensionara
    // segun su propia cantidad de pines (como hace seven_segment) el
    // digito saldria mas chico y con los segmentos desproporcionados (el
    // bug reportado: "ajustar el display hex a que se parezca al de 7
    // segmentos"), ya que drawSevenSegmentDigit() deriva el grosor de cada
    // segmento del tamano del digito. Se fija a 9 franjas (equivalente a
    // seven_segment con las 8 entradas a..g+dot) para que ambos rendericen
    // el mismo digito, del mismo tamano, sin importar su propia cantidad de
    // pines.
    const std::size_t heightSlots = isHexDisplay          ? std::size_t{9}
                                     : isSevenSegmentType  ? sideCount + 1
                                                           : std::max(sideCount + 1, std::size_t{4});
    height_ = pinPitch * static_cast<qreal>(heightSlots);
    if (isIc74ls) {
        height_ += 8.0; // un poco mas de aire arriba/abajo, a pedido explicito
    }

    // Anulacion opcional por instancia (propiedades "customWidth"/
    // "customHeight", presentes en todos los tipos - ver
    // makeCustomWidthProperty/makeCustomHeightProperty). 0 = automatico (el
    // valor recien calculado arriba se mantiene).
    const auto customWidth = std::get<uint64_t>(instance->property("customWidth"));
    if (customWidth > 0) {
        width_ = static_cast<qreal>(customWidth);
    }
    const auto customHeight = std::get<uint64_t>(instance->property("customHeight"));
    if (customHeight > 0) {
        height_ = static_cast<qreal>(customHeight);
    }

    // Los pines se distribuyen de forma uniforme a lo largo de toda la
    // altura en cada lado (no en pasos fijos de kGridSize), de modo que un
    // lado con menos pines que el otro los centra en lugar de agruparlos
    // cerca de la parte superior - esto es crucial para que el unico pin de
    // salida de una puerta se alinee con el centro vertical de su cuerpo.
    pinStubLength_ = isSegmentDisplay ? 10.0 : 0.0;

    pinLocalPositions_.assign(pins.size(), QPointF{});
    if (isIc74ls) {
        // Una franja por posicion fisica (no una por pin real + una por
        // decorativo aparte): reales y decorativos comparten la misma
        // columna, intercalados en el orden exacto del DIP.
        for (std::size_t i = 0; i < leftSeq.size(); ++i) {
            const qreal y = height_ / static_cast<qreal>(leftSeq.size() + 1) * static_cast<qreal>(i + 1);
            if (leftSeq[i].isFunctional) {
                if (const auto pinIndex = findPinIndexByName(pins, leftSeq[i].label)) {
                    pinLocalPositions_[*pinIndex] = QPointF(-pinStubLength_, y);
                }
            } else {
                decorativeLeftPositions_.push_back(QPointF(-pinStubLength_, y));
            }
        }
        for (std::size_t i = 0; i < rightSeq.size(); ++i) {
            const qreal y = height_ / static_cast<qreal>(rightSeq.size() + 1) * static_cast<qreal>(i + 1);
            if (rightSeq[i].isFunctional) {
                if (const auto pinIndex = findPinIndexByName(pins, rightSeq[i].label)) {
                    pinLocalPositions_[*pinIndex] = QPointF(width_, y);
                }
            } else {
                decorativeRightPositions_.push_back(QPointF(width_, y));
            }
        }
    } else {
        for (std::size_t i = 0; i < leftPins.size(); ++i) {
            const qreal y = height_ / static_cast<qreal>(leftPins.size() + 1) * static_cast<qreal>(i + 1);
            pinLocalPositions_[leftPins[i]] = QPointF(-pinStubLength_, y);
        }
        for (std::size_t i = 0; i < rightPins.size(); ++i) {
            const qreal y = height_ / static_cast<qreal>(rightPins.size() + 1) * static_cast<qreal>(i + 1);
            pinLocalPositions_[rightPins[i]] = QPointF(width_, y);
        }
    }

    for (uint16_t i = 0; i < pins.size(); ++i) {
        auto* pin = new PinItem(document_, componentId_, i, pins[i].direction, this);
        pin->setPos(pinLocalPositions_[i]);
        pinItems_.push_back(pin);
    }
}

QRectF ComponentItem::boundingRect() const {
    // Reserva siempre el espacio de la etiqueta de instancia (ver paint()),
    // este presente o no - evita tener que llamar a prepareGeometryChange()
    // cada vez que el usuario tipea/borra el texto de "label".
    constexpr qreal kLabelHeight = 14.0;
    return QRectF(-pinStubLength_, 0.0, width_ + pinStubLength_, height_ + kLabelHeight);
}

QPainterPath ComponentItem::shape() const {
    // Solo el cuerpo real - excluye la franja de la etiqueta debajo y el
    // margen del stub de pin que si incluye boundingRect() (ver el
    // comentario en el .hpp).
    QPainterPath path;
    path.addRect(0.0, 0.0, width_, height_);
    return path;
}

QPointF ComponentItem::pinScenePos(uint16_t pinIndex) const {
    if (pinIndex >= pinLocalPositions_.size()) {
        return scenePos();
    }
    return mapToScene(pinLocalPositions_[pinIndex]);
}

void ComponentItem::addAttachedWire(WireItem* wire) { attachedWires_.push_back(wire); }

void ComponentItem::removeAttachedWire(WireItem* wire) {
    attachedWires_.erase(std::remove(attachedWires_.begin(), attachedWires_.end(), wire), attachedWires_.end());
}

QVariant ComponentItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == QGraphicsItem::ItemPositionChange) {
        if (document_->isLiveSimulation()) {
            // No se permite reubicar componentes mientras la simulacion
            // esta en ejecucion: se rechaza el movimiento devolviendo la
            // posicion actual sin cambios.
            return pos();
        }
        auto* circuitScene = qobject_cast<CircuitScene*>(scene());
        if (circuitScene != nullptr && circuitScene->snapToGridEnabled()) {
            const QPointF proposed = value.toPointF();
            return QPointF(snapToGrid(proposed.x(), kGridSize), snapToGrid(proposed.y(), kGridSize));
        }
    } else if (change == QGraphicsItem::ItemPositionHasChanged) {
        for (WireItem* wire : attachedWires_) {
            wire->updateGeometry();
        }
    }
    return QGraphicsItem::itemChange(change, value);
}

void ComponentItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    if (instance == nullptr) {
        return;
    }
    const bool selected = (option->state & QStyle::State_Selected) != 0;
    const std::string& typeId = instance->typeId();
    if (typeId == "io.led") {
        paintLed(painter, selected);
    } else if (typeId == "io.seven_segment") {
        paintSevenSegment(painter, selected);
    } else if (typeId == "io.hexDisplay") {
        paintHexDisplay(painter, selected);
    } else if (typeId == "io.ledMatrix") {
        paintLedMatrix(painter, selected);
    } else if (typeId == "io.terminal") {
        paintTerminal(painter, selected);
    } else if (typeId == "wiring.input") {
        paintInput(painter, selected);
    } else if (typeId == "wiring.clock") {
        paintClock(painter, selected);
    } else if (typeId == "wiring.powerOnReset") {
        paintPowerOnReset(painter, selected);
    } else if (typeId == "wiring.output") {
        paintOutput(painter, selected);
    } else if (typeId == "wiring.pullResistor") {
        paintPullResistor(painter, selected);
    } else if (typeId == "wiring.doNotConnect") {
        paintDoNotConnect(painter, selected);
    } else if (typeId == "wiring.tunnel") {
        paintTunnel(painter, selected);
    } else if (typeId == "wiring.ground") {
        paintGround(painter, selected);
    } else if (typeId == "wiring.transistor") {
        paintTransistor(painter, selected);
    } else if (typeId == "wiring.transmissionGate") {
        paintTransmissionGate(painter, selected);
    } else if (typeId.rfind("gates.", 0) == 0) {
        paintGate(painter, selected);
    } else if (typeId.rfind("plexers.", 0) == 0) {
        paintPlexer(painter, selected);
    } else if (typeId.rfind("arithmetic.", 0) == 0) {
        paintArithmetic(painter, selected);
    } else if (typeId.rfind("memory.", 0) == 0) {
        paintMemory(painter, selected);
    } else if (typeId == "structural.subcircuit") {
        paintSubcircuit(painter, selected);
    } else if (typeId.rfind("ic74ls.", 0) == 0) {
        paintIc74ls(painter, selected);
    } else {
        paintGeneric(painter, selected);
    }

    // La etiqueta de instancia (propiedad "label", comun a todos los tipos)
    // se dibuja debajo de la caja, centrada - hasta ahora solo se usaba en
    // el inspector/JSON pese a que su descripcion ya decia "mostrado junto
    // al componente".
    const std::string& label = std::get<std::string>(instance->property("label"));
    if (!label.empty()) {
        QFont font = painter->font();
        font.setPointSizeF(7.0);
        painter->setFont(font);
        painter->setPen(labelInkColor());

        const QRectF labelRect(-pinStubLength_, height_ + 2.0, width_ + pinStubLength_, 12.0);
        // "labelRotation" (independiente de ComponentPlacement::
        // rotationDegrees, ver makeLabelRotationProperty()) se mide en
        // pantalla, no en el espacio local del item -- se resta la rotacion
        // propia del componente para contrarrestarla, de modo que con
        // labelRotation=0 (el default) la etiqueta se lea siempre horizontal
        // sin importar como haya quedado orientado el componente.
        const int componentRotation = document_->componentPlacement(componentId_).rotationDegrees;
        const int labelRotation = std::stoi(std::get<std::string>(instance->property("labelRotation")));
        const int netRotation = ((labelRotation - componentRotation) % 360 + 360) % 360;

        if (netRotation == 0) {
            painter->drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, QString::fromStdString(label));
        } else {
            painter->save();
            painter->translate(labelRect.center());
            painter->rotate(netRotation);
            painter->translate(-labelRect.center());
            painter->drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, QString::fromStdString(label));
            painter->restore();
        }
    }
}

void ComponentItem::paintGate(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const std::string& typeId = instance->typeId();
    const GateShapeKind kind = gateShapeKindForTypeId(typeId);
    const bool hasBubble = gateHasOutputBubble(typeId);
    const bool hasExtraCurve = gateHasExtraOrCurve(typeId);

    // Escalados a la mitad de sus valores originales (8/6/12/6), que estaban
    // pensados para un width_ de 64 - con width_=32 (mas cuadrado, menos
    // alargado) los valores viejos dejaban un bodyRect casi inexistente en
    // el peor caso (XNOR: burbuja + curva extra).
    constexpr qreal bubbleDiameter = 4.0;
    const qreal rightMargin = 3.0 + (hasBubble ? bubbleDiameter : 0.0);
    const qreal leftMargin = hasExtraCurve ? 6.0 : 3.0;
    const QRectF bodyRect(leftMargin, 6.0, width_ - leftMargin - rightMargin, height_ - 12.0);
    const QColor fillColor = bodyFillColor(*instance, QColor(235, 235, 235));

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.5));
    painter->setBrush(fillColor);
    painter->drawPath(buildGateShapePath(kind, bodyRect));

    if (hasExtraCurve) {
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(buildOrExtraCurve(bodyRect, 6.0));
    }

    const qreal outputY = height_ / 2.0; // las puertas siempre tienen exactamente un pin de salida, centrado
    const qreal bodyRightEdge = gateBodyRightEdge(kind, bodyRect);
    qreal stubStartX = bodyRightEdge;
    if (hasBubble) {
        const QPointF bubbleCenter(bodyRightEdge + bubbleDiameter / 2.0 + 1.0, outputY);
        painter->setPen(QPen(QColor(20, 20, 20), 1.5));
        painter->setBrush(fillColor);
        painter->drawEllipse(bubbleCenter, bubbleDiameter / 2.0, bubbleDiameter / 2.0);
        stubStartX = bubbleCenter.x() + bubbleDiameter / 2.0;
    }
    painter->setPen(QPen(QColor(20, 20, 20), 1.5));
    painter->drawLine(QPointF(stubStartX, outputY), QPointF(width_, outputY));

    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        if (instance->pins()[i].direction != core::PinDirection::Output) {
            const qreal y = pinLocalPositions_[i].y();
            painter->drawLine(QPointF(0.0, y), QPointF(bodyRect.left(), y));
        }
    }
}

void ComponentItem::paintInput(QPainter* painter, bool selected) {
    const core::LogicValue value = document_->pinValue(componentId_, 0);
    const QRectF bodyRect(3.0, 6.0, width_ - 7.0, height_ - 12.0);

    // Conserva exactamente el comportamiento visual anterior (valor, color
    // y glifo); solo drawRoundedRect se sustituye por el nuevo contorno.
    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.5));
    painter->setBrush(logicValueColor(value));
    painter->drawPath(buildPortShapePath(bodyRect, true));
    painter->drawLine(QPointF(bodyRect.right(), height_ / 2.0), QPointF(width_ - PinItem::kRadius, height_ / 2.0));

    QFont font = painter->font();
    font.setBold(true);
    font.setPointSizeF(12.0);
    painter->setFont(font);
    painter->setPen(value == core::LogicValue::Zero || value == core::LogicValue::Error ? Qt::white : Qt::black);
    // Centrado optico: la punta aporta ancho, pero mucho menos peso visual
    // que la zona rectangular. Se compensa hacia la izquierda y apenas
    // hacia arriba, en proporcion al alto para conservar el ajuste al
    // redimensionar el componente.
    const QRectF textRect = bodyRect.translated(-bodyRect.height() * 0.095, -bodyRect.height() * 0.045);
    painter->drawText(textRect, Qt::AlignCenter, logicValueGlyph(value));
}

void ComponentItem::paintClock(QPainter* painter, bool selected) {
    // Mismo dibujo que wiring.input (color/glifo 0/1 segun el valor
    // observado en vivo - lo que hace falta para "ver" el reloj parpadeando
    // solo mientras corre), mas una onda cuadrada de varios ciclos en la
    // franja vacia que paintInput() deja arriba del cuerpo (bodyRect
    // empieza en y=6, ver paintInput() - todo lo que hay entre y=0 y ese
    // punto queda sin usar). Un primer intento la dibujaba encima del
    // cuerpo, en la esquina superior izquierda: quedaba encimada con el
    // glifo grande de 0/1 del centro y era ilegible (el defecto reportado);
    // ademas de corta, se pidio "mas larga a lo horizontal". Dibujarla en
    // esta franja de arriba, con ancho casi completo, resuelve ambas cosas
    // a la vez.
    paintInput(painter, selected);

    constexpr qreal margin = 4.0;
    constexpr qreal waveTop = 1.0;
    constexpr qreal waveBottom = 5.0;
    constexpr int segments = 6; // 3 ciclos completos: alto-bajo-alto-bajo-alto-bajo
    const qreal left = margin;
    const qreal right = width_ - margin;
    const qreal segmentWidth = (right - left) / static_cast<qreal>(segments);

    QPainterPath wave;
    qreal x = left;
    wave.moveTo(x, waveBottom);
    for (int i = 0; i < segments; ++i) {
        const qreal y = (i % 2 == 0) ? waveTop : waveBottom;
        wave.lineTo(x, y);
        x += segmentWidth;
        wave.lineTo(x, y);
    }

    // Se dibuja sobre el fondo del lienzo (esta franja queda afuera del
    // cuerpo relleno de color), asi que usa el mismo criterio claro/oscuro
    // que la etiqueta de instancia (labelInkColor()) en vez del contraste
    // blanco/negro que usa paintInput() sobre su propio relleno.
    painter->setPen(QPen(labelInkColor(), 1.1));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(wave);
}

void ComponentItem::paintPowerOnReset(QPainter* painter, bool selected) {
    // Mismo tratamiento que paintClock (0/1 de paintInput + una insignia en
    // la franja vacia de arriba), pero un unico escalon (sube y se queda)
    // en vez de una onda repetida - un reinicio al encender es un pulso
    // unico, no periodico, y la insignia debe transmitir esa diferencia a
    // simple vista.
    paintInput(painter, selected);

    constexpr qreal margin = 4.0;
    constexpr qreal stepTop = 1.0;
    constexpr qreal stepBottom = 5.0;
    const qreal left = margin;
    const qreal right = width_ - margin;
    const qreal riseX = left + (right - left) * 0.3;

    QPainterPath step;
    step.moveTo(left, stepBottom);
    step.lineTo(riseX, stepBottom);
    step.lineTo(riseX, stepTop);
    step.lineTo(right, stepTop);

    painter->setPen(QPen(labelInkColor(), 1.1));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(step);
}

void ComponentItem::paintOutput(QPainter* painter, bool selected) {
    // Mismo tratamiento visual que paintInput (color/glifo segun el valor
    // observado), pero con la punta del contorno apuntando hacia la
    // izquierda (el pin de Salida es de entrada, en el lado izquierdo).
    const core::LogicValue value = document_->pinValue(componentId_, 0);
    const QRectF bodyRect(4.0, 6.0, width_ - 7.0, height_ - 12.0);

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.5));
    painter->setBrush(logicValueColor(value));
    painter->drawPath(buildPortShapePath(bodyRect, false));
    painter->drawLine(QPointF(PinItem::kRadius, height_ / 2.0), QPointF(bodyRect.left(), height_ / 2.0));

    QFont font = painter->font();
    font.setBold(true);
    font.setPointSizeF(12.0);
    painter->setFont(font);
    painter->setPen(value == core::LogicValue::Zero || value == core::LogicValue::Error ? Qt::white : Qt::black);
    const QRectF textRect = bodyRect.translated(bodyRect.height() * 0.095, -bodyRect.height() * 0.045);
    painter->drawText(textRect, Qt::AlignCenter, logicValueGlyph(value));
}

void ComponentItem::paintPullResistor(QPainter* painter, bool selected) {
    // Zigzag clasico de resistencia + una flecha indicando hacia que nivel
    // tira (arriba = pull-up/1, abajo = pull-down/0) - sin la flecha, el
    // icono no distinguiria las dos variantes a simple vista (la unica
    // diferencia real es la propiedad "value").
    const components::ComponentInstance* instance = document_->component(componentId_);
    const bool pullsHigh = std::get<std::string>(instance->property("value")) == "1";
    const qreal cy = height_ / 2.0;
    const QColor ink = labelInkColor();

    const QRectF bodyRect(8.0, cy - 5.0, width_ - 22.0, 10.0);
    painter->setPen(QPen(selected ? QColor(30, 90, 220) : ink, selected ? 1.6 : 1.3));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(0.0, cy), QPointF(bodyRect.left(), cy));
    painter->drawRect(bodyRect);

    QPainterPath zigzag;
    constexpr int teeth = 4;
    const qreal step = bodyRect.width() / static_cast<qreal>(teeth);
    zigzag.moveTo(bodyRect.left(), cy);
    for (int i = 0; i < teeth; ++i) {
        const qreal x = bodyRect.left() + step * (static_cast<qreal>(i) + 0.5);
        const qreal y = (i % 2 == 0) ? bodyRect.top() + 2.0 : bodyRect.bottom() - 2.0;
        zigzag.lineTo(x, y);
    }
    zigzag.lineTo(bodyRect.right(), cy);
    painter->drawPath(zigzag);
    painter->drawLine(QPointF(bodyRect.right(), cy), QPointF(width_, cy));

    const qreal arrowX = width_ - 12.0;
    const qreal tipY = pullsHigh ? 2.0 : height_ - 2.0;
    const qreal tailY = pullsHigh ? bodyRect.top() - 2.0 : bodyRect.bottom() + 2.0;
    painter->setPen(QPen(ink, 1.3));
    painter->drawLine(QPointF(arrowX, tailY), QPointF(arrowX, tipY));
    QPainterPath arrowHead;
    const qreal dir = pullsHigh ? 1.0 : -1.0;
    arrowHead.moveTo(arrowX, tipY);
    arrowHead.lineTo(arrowX - 2.5, tipY + dir * 4.0);
    arrowHead.lineTo(arrowX + 2.5, tipY + dir * 4.0);
    arrowHead.closeSubpath();
    painter->setBrush(ink);
    painter->setPen(Qt::NoPen);
    painter->drawPath(arrowHead);
}

void ComponentItem::paintDoNotConnect(QPainter* painter, bool selected) {
    // Sin pines: marca puramente documental de "este lugar queda
    // intencionalmente sin conexion" - un circulo tachado, misma
    // convencion que una senal de prohibido.
    const QRectF bodyRect(4.0, 4.0, width_ - 8.0, height_ - 8.0);
    const qreal radius = std::min(bodyRect.width(), bodyRect.height()) / 2.0 - 2.0;
    const QPointF center = bodyRect.center();

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : labelInkColor(), selected ? 1.8 : 1.6));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(center, radius, radius);
    const qreal diag = radius * 0.7;
    painter->drawLine(QPointF(center.x() - diag, center.y() - diag), QPointF(center.x() + diag, center.y() + diag));
}

void ComponentItem::paintTunnel(QPainter* painter, bool selected) {
    // No es un driver ni un sink real (la conexion la hace el union-find de
    // CircuitDocument::rebuildSimulation(), emparejando la propiedad
    // "label" con otros wiring.tunnel iguales) - se dibuja como un cable
    // corto que termina en un circulo hueco punteado, para distinguirlo a
    // simple vista de un pin real (Input/Output/Constant, relleno solido).
    // La etiqueta debajo del cuerpo (mecanismo comun a todos los tipos) ya
    // muestra con que otros tuneles esta conectado.
    const qreal cy = height_ / 2.0;
    const QColor ink = labelInkColor();
    painter->setPen(QPen(selected ? QColor(30, 90, 220) : ink, selected ? 1.6 : 1.4));
    painter->drawLine(QPointF(6.0, cy), QPointF(width_ - 6.0, cy));
    painter->setPen(QPen(ink, 1.4, Qt::DashLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPointF(width_ - 6.0, cy), 4.0, 4.0);
}

void ComponentItem::paintGround(QPainter* painter, bool selected) {
    // Simbolo real de tierra logica (IEC): un tallo con barras horizontales
    // decrecientes. Este proyecto ubica el unico pin de un componente-
    // fuente siempre sobre el borde derecho (ver paintInput/paintClock),
    // no el borde superior/inferior que usa el simbolo real - se dibuja
    // rotado 90 grados (tallo horizontal hacia el pin, barras verticales
    // decreciendo en altura a medida que se alejan de el) para calzar con
    // esa convencion sin perder el reconocimiento visual.
    const qreal cy = height_ / 2.0;
    const qreal leadStartX = width_ - 14.0;
    painter->setPen(QPen(selected ? QColor(30, 90, 220) : labelInkColor(), selected ? 1.8 : 1.6));
    painter->drawLine(QPointF(leadStartX, cy), QPointF(width_, cy));

    const std::array<qreal, 3> barX{leadStartX, leadStartX - 4.0, leadStartX - 8.0};
    const std::array<qreal, 3> barHalfHeight{7.0, 5.0, 3.0};
    for (std::size_t i = 0; i < barX.size(); ++i) {
        painter->drawLine(QPointF(barX[i], cy - barHalfHeight[i]), QPointF(barX[i], cy + barHalfHeight[i]));
    }
}

void ComponentItem::paintTransistor(QPainter* painter, bool selected) {
    // Simbolo tipo BJT (ver el comentario de paintMosLeg()): Gate (pin 1,
    // el unico pin izquierdo tras el override de rebuildPins() - siempre
    // cae exactamente centrado) entra derecho hasta la barra; Source(pin
    // 0)/Drain(pin 2) salen de ella por su propia pata en angulo hacia el
    // borde derecho. La flecha sobre la pata de Source sigue la misma
    // convencion NPN/PNP de un transistor real: apunta hacia afuera en
    // NMOS (conduce con Gate=1, como NPN), hacia adentro en PMOS (conduce
    // con Gate=0, como PNP) - ver wiring.transistor::buildSimulation().
    const components::ComponentInstance* instance = document_->component(componentId_);
    const bool isPmos = std::get<std::string>(instance->property("type")) == "PMOS";
    const QColor ink = selected ? QColor(30, 90, 220) : labelInkColor();
    const qreal barX = width_ * 0.4;
    const qreal gateY = pinLocalPositions_[1].y();
    const qreal sourceY = pinLocalPositions_[0].y();
    const qreal drainY = pinLocalPositions_[2].y();
    const qreal sourceBarY = gateY - kMosDiagonalSplit;
    const qreal drainBarY = gateY + kMosDiagonalSplit;

    painter->setPen(QPen(ink, 1.4));
    painter->drawLine(QPointF(barX, gateY - kMosBarHalfHeight), QPointF(barX, gateY + kMosBarHalfHeight));
    painter->drawLine(QPointF(0.0, gateY), QPointF(barX, gateY));
    const QPointF sourceKink = paintMosLeg(painter, barX, sourceBarY, width_, sourceY, ink);
    paintMosLeg(painter, barX, drainBarY, width_, drainY, ink);

    paintMosArrow(painter, QPointF(barX, sourceBarY), sourceKink, !isPmos, ink);
}

void ComponentItem::paintTransmissionGate(QPainter* painter, bool selected) {
    // Misma barra+patas que paintTransistor, con dos controles (N/pin 1,
    // habilita en alto, sin burbuja; P/pin 2, habilita en bajo, con
    // burbuja) entrando derecho a la barra en vez de uno solo - y A(pin
    // 0)/Y(pin 3) saliendo por su propia pata en angulo, igual que Source/
    // Drain del transistor. Sin flecha (a diferencia del transistor): no
    // hay una unica polaridad que marcar, el par siempre necesita las dos
    // senales complementarias - ver
    // wiring.transmissionGate::buildSimulation().
    const QColor ink = selected ? QColor(30, 90, 220) : labelInkColor();
    const qreal barX = width_ * 0.4;
    const qreal aY = pinLocalPositions_[0].y();
    const qreal nY = pinLocalPositions_[1].y();
    const qreal pY = pinLocalPositions_[2].y();
    const qreal yY = pinLocalPositions_[3].y();
    const qreal barCenterY = (nY + pY) / 2.0;
    const qreal barTop = std::min({nY, pY, barCenterY - kMosBarHalfHeight});
    const qreal barBottom = std::max({nY, pY, barCenterY + kMosBarHalfHeight});

    painter->setPen(QPen(ink, 1.4));
    painter->drawLine(QPointF(barX, barTop), QPointF(barX, barBottom));
    painter->drawLine(QPointF(0.0, nY), QPointF(barX, nY));
    const qreal pLeadEnd = barX - 6.0;
    painter->drawLine(QPointF(0.0, pY), QPointF(pLeadEnd, pY));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPointF(barX - 3.0, pY), 2.2, 2.2);

    paintMosLeg(painter, barX, barCenterY - kMosDiagonalSplit, width_, aY, ink);
    paintMosLeg(painter, barX, barCenterY + kMosDiagonalSplit, width_, yY, ink);
}

void ComponentItem::paintGeneric(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    // boundingRect() incluye el margen de la etiqueta de instancia (ver
    // paint()) - la caja en si sigue siendo (0,0,width_,height_).
    const QRectF bodyRect(0.0, 0.0, width_, height_);

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.0));
    painter->setBrush(bodyFillColor(*instance, QColor(235, 235, 235)));
    painter->drawRoundedRect(bodyRect, 6.0, 6.0);

    painter->setPen(Qt::black);
    painter->drawText(bodyRect, Qt::AlignCenter, QString::fromStdString(instance->definition().displayName));
}

void ComponentItem::paintLed(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const core::LogicValue value = document_->pinValue(componentId_, 0);
    const bool lit = components::ledIsLit(*instance, value);
    const bool conflict = value == core::LogicValue::Error;

    const QColor baseColor = conflict ? QColor(220, 30, 30) : ledBaseColor(std::get<std::string>(instance->property("color")));
    const QColor lensColor = (lit || conflict) ? baseColor : baseColor.darker(380);

    // Visto directamente desde arriba: un bisel de plastico oscuro detras de
    // una lente abombada, con un brillo radial desplazado hacia la esquina
    // superior izquierda para que se lea como brillante. Es un circulo
    // perfecto sin importar la relacion de aspecto de la caja del componente
    // - la caja es mas ancha que alta (un unico pin a la izquierda, ninguno
    // a la derecha), por lo que dimensionar directamente a partir de
    // boundingRect() dibujaria una elipse en su lugar.
    const qreal diameter = std::min(width_, height_) * 0.76;
    const QRectF bezelRect(width_ / 2.0 - diameter / 2.0, height_ / 2.0 - diameter / 2.0, diameter, diameter);

    // El pin se encuentra en el borde izquierdo de la caja (x local = 0); el
    // bisel esta centrado en la caja y es mas estrecho que ella, asi que sin
    // esta linea de enlace habria un hueco visible entre el pin/cable y el
    // cuerpo del LED.
    painter->setPen(QPen(QColor(54, 54, 54), 1.5));
    painter->drawLine(QPointF(0.0, height_ / 2.0), QPointF(bezelRect.left(), height_ / 2.0));

    // El bisel (no la lente, cuyo color ya esta ocupado por el estado
    // encendido/apagado) es lo que marca la seleccion -- mismo tratamiento
    // azul+trazo mas grueso que el resto de los tipos.
    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(25, 25, 25), selected ? 2.6 : 2.0));
    painter->setBrush(QColor(55, 55, 55));
    painter->drawEllipse(bezelRect);

    const QRectF lensRect = bezelRect.adjusted(bezelRect.width() * 0.14, bezelRect.height() * 0.14,
                                                -bezelRect.width() * 0.14, -bezelRect.height() * 0.14);
    QRadialGradient gradient(lensRect.center() - QPointF(lensRect.width() * 0.22, lensRect.height() * 0.22),
                              lensRect.width() * 0.8);
    gradient.setColorAt(0.0, lensColor.lighter(lit || conflict ? 170 : 130));
    gradient.setColorAt(1.0, lensColor);
    painter->setPen(QPen(lensColor.darker(160), 1.0));
    painter->setBrush(gradient);
    painter->drawEllipse(lensRect);
}

void ComponentItem::paintSevenSegment(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const QRectF digitRect = computeSevenSegmentDigitRect(width_, height_);

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : Qt::black, selected ? 2.0 : 0.85));
    painter->setBrush(QColor(54, 54, 54));
    // boundingRect() incluye el tramo del stub de los pines (x negativo) -
    // el fondo de la caja en si sigue siendo (0,0,width_,height_).
    painter->drawRoundedRect(QRectF(0.0, 0.0, width_, height_), 6.0, 6.0);

    // Un tramo recto entre cada pin y el borde de la caja ("stub"), para que
    // no queden pegados contra el display - mismo criterio visual que un
    // esquematico real.
    painter->setPen(QPen(QColor(54, 54, 54).lighter(160), 1.2));
    for (const QPointF& pinPos : pinLocalPositions_) {
        painter->drawLine(pinPos, QPointF(0.0, pinPos.y()));
    }

    // Nombre de cada pin (a..g, dot) en el margen izquierdo, a la altura de
    // su propio pin - todos los pines de este componente caen del lado
    // izquierdo (son de entrada), asi que alcanza con el nombre sin marcador
    // de direccion.
    QFont pinFont = painter->font();
    pinFont.setPointSizeF(7.0);
    painter->setFont(pinFont);
    painter->setPen(QColor(210, 210, 210));
    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        const qreal y = pinLocalPositions_[i].y();
        painter->drawText(QRectF(4.0, y - 8.0, digitRect.left() - 6.0, 16.0), Qt::AlignVCenter | Qt::AlignLeft,
                           QString::fromStdString(instance->pins()[i].name));
    }

    std::array<SegmentState, 7> segmentStates{};
    for (std::size_t i = 0; i < segmentStates.size(); ++i) {
        const core::LogicValue value = document_->pinValue(componentId_, static_cast<uint16_t>(i));
        const bool lit = components::segmentIsLit(*instance, value);
        segmentStates[i] =
            value == core::LogicValue::Error ? SegmentState::Error : (lit ? SegmentState::Lit : SegmentState::Off);
    }
    std::optional<SegmentState> dotState;
    if (instance->pins().size() > 7) {
        const core::LogicValue dotValue = document_->pinValue(componentId_, 7);
        const bool dotLit = components::segmentIsLit(*instance, dotValue);
        dotState = dotValue == core::LogicValue::Error ? SegmentState::Error
                                                        : (dotLit ? SegmentState::Lit : SegmentState::Off);
    }
    drawSevenSegmentDigit(painter, digitRect, segmentStates, dotState);

    // Indicador chico de "anodo comun (A) / catodo comun (K)" en la esquina
    // superior derecha (la inferior derecha ya tiene el punto decimal - ver
    // drawSevenSegmentDigit()) - la propiedad "commonAnode" solo se veia
    // hasta ahora en el inspector de propiedades, sin ninguna marca en el
    // lienzo. K en vez de C para catodo (misma convencion que evita
    // confundirlo con capacitancia en electronica).
    const bool commonAnode = std::get<bool>(instance->property("commonAnode"));
    QFont badgeFont = painter->font();
    badgeFont.setPointSizeF(6.0);
    badgeFont.setBold(true);
    painter->setFont(badgeFont);
    painter->setPen(QColor(150, 150, 150));
    painter->drawText(QRectF(width_ - 13.0, 2.0, 11.0, 11.0), Qt::AlignCenter,
                       commonAnode ? QStringLiteral("A") : QStringLiteral("K"));
}

void ComponentItem::paintHexDisplay(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const QRectF digitRect = computeSevenSegmentDigitRect(width_, height_);

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : Qt::black, selected ? 2.0 : 0.85));
    painter->setBrush(QColor(54, 54, 54));
    painter->drawRoundedRect(QRectF(0.0, 0.0, width_, height_), 6.0, 6.0);

    painter->setPen(QPen(QColor(54, 54, 54).lighter(160), 1.2));
    for (const QPointF& pinPos : pinLocalPositions_) {
        painter->drawLine(pinPos, QPointF(0.0, pinPos.y()));
    }

    QFont pinFont = painter->font();
    pinFont.setPointSizeF(7.0);
    painter->setFont(pinFont);
    painter->setPen(QColor(210, 210, 210));
    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        const qreal y = pinLocalPositions_[i].y();
        painter->drawText(QRectF(4.0, y - 8.0, digitRect.left() - 6.0, 16.0), Qt::AlignVCenter | Qt::AlignLeft,
                           QString::fromStdString(instance->pins()[i].name));
    }

    const components::HexDisplayState state =
        components::hexDisplayState(*instance, document_->pinValue(componentId_, 0), document_->pinValue(componentId_, 1),
                                     document_->pinValue(componentId_, 2), document_->pinValue(componentId_, 3));
    // Sin conectar (HighImpedance/Unknown, p. ej. mientras se arrastra en el
    // lienzo) se dibuja apagado, igual que un segmento individual de
    // io.seven_segment sin driver -- solo un conflicto real
    // (LogicValue::Error) se marca en rojo. Antes cualquiera de los dos
    // casos caia en SegmentState::Error, dibujando los 7 segmentos
    // encendidos (se leia como un "8" fijo) apenas se colocaba o arrastraba
    // el componente sin cablear todavia (el defecto reportado).
    const SegmentState undrivenState = state.hasConflict ? SegmentState::Error : SegmentState::Off;
    std::array<SegmentState, 7> segmentStates{};
    for (std::size_t i = 0; i < segmentStates.size(); ++i) {
        segmentStates[i] = !state.valid ? undrivenState : (state.segments[i] ? SegmentState::Lit : SegmentState::Off);
    }
    std::optional<SegmentState> dotState;
    if (instance->pins().size() > 4) {
        const core::LogicValue dotValue = document_->pinValue(componentId_, 4);
        dotState = dotValue == core::LogicValue::Error ? SegmentState::Error
                   : dotValue == core::LogicValue::One  ? SegmentState::Lit
                                                         : SegmentState::Off;
    }
    const QColor litColor = ledBaseColor(std::get<std::string>(instance->property("color")));
    drawSevenSegmentDigit(painter, digitRect, segmentStates, dotState, litColor);
}

void ComponentItem::paintPlexer(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const std::string& typeId = instance->typeId();
    // "Uno a muchos" (decodificador/demultiplexor) vs "muchos a uno"
    // (multiplexor/codificador de prioridad) - ver el comentario de
    // MsiShapeKind en MsiShapes.hpp.
    const bool fanOut = typeId == "plexers.decoder" || typeId == "plexers.demultiplexer";
    const MsiShapeKind kind = fanOut ? MsiShapeKind::FanOutTrapezoid : MsiShapeKind::FanInTrapezoid;
    const QRectF bodyRect(3.0, 6.0, width_ - 6.0, height_ - 12.0);

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.5));
    painter->setBrush(bodyFillColor(*instance, QColor(235, 235, 235)));
    painter->drawPath(buildMsiShapePath(kind, bodyRect));

    painter->setPen(QPen(QColor(20, 20, 20), 1.5));
    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        const qreal y = pinLocalPositions_[i].y();
        const bool isOutput = instance->pins()[i].direction == core::PinDirection::Output;
        painter->drawLine(QPointF(isOutput ? width_ : 0.0, y),
                           QPointF(isOutput ? bodyRect.right() : bodyRect.left(), y));
    }

    painter->setPen(Qt::black);
    painter->drawText(bodyRect, Qt::AlignCenter, QString::fromStdString(instance->definition().displayName));
}

void ComponentItem::paintArithmetic(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const std::string& typeId = instance->typeId();
    const QRectF bodyRect(3.0, 6.0, width_ - 6.0, height_ - 12.0);

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.5));
    painter->setBrush(bodyFillColor(*instance, QColor(235, 235, 235)));
    painter->drawPath(buildMsiShapePath(MsiShapeKind::Rectangle, bodyRect));

    painter->setPen(QPen(QColor(20, 20, 20), 1.5));
    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        const qreal y = pinLocalPositions_[i].y();
        const bool isOutput = instance->pins()[i].direction == core::PinDirection::Output;
        painter->drawLine(QPointF(isOutput ? width_ : 0.0, y),
                           QPointF(isOutput ? bodyRect.right() : bodyRect.left(), y));
    }

    // Glifo vectorial chico (sin drawText - queda reservado para el nombre
    // centrado abajo): "+" sumador, "-" restador, "=" comparador.
    const QPointF glyphCenter(bodyRect.center().x(), bodyRect.top() + bodyRect.height() * 0.22);
    const qreal glyphHalf = std::min(bodyRect.width(), bodyRect.height()) * 0.14;
    painter->setPen(QPen(QColor(20, 20, 20), 1.6));
    if (typeId == "arithmetic.adder") {
        painter->drawLine(QPointF(glyphCenter.x() - glyphHalf, glyphCenter.y()),
                           QPointF(glyphCenter.x() + glyphHalf, glyphCenter.y()));
        painter->drawLine(QPointF(glyphCenter.x(), glyphCenter.y() - glyphHalf),
                           QPointF(glyphCenter.x(), glyphCenter.y() + glyphHalf));
    } else if (typeId == "arithmetic.subtractor") {
        painter->drawLine(QPointF(glyphCenter.x() - glyphHalf, glyphCenter.y()),
                           QPointF(glyphCenter.x() + glyphHalf, glyphCenter.y()));
    } else { // arithmetic.comparator
        painter->drawLine(QPointF(glyphCenter.x() - glyphHalf, glyphCenter.y() - glyphHalf * 0.5),
                           QPointF(glyphCenter.x() + glyphHalf, glyphCenter.y() - glyphHalf * 0.5));
        painter->drawLine(QPointF(glyphCenter.x() - glyphHalf, glyphCenter.y() + glyphHalf * 0.5),
                           QPointF(glyphCenter.x() + glyphHalf, glyphCenter.y() + glyphHalf * 0.5));
    }

    painter->setPen(Qt::black);
    painter->drawText(bodyRect, Qt::AlignCenter, QString::fromStdString(instance->definition().displayName));
}

void ComponentItem::paintMemory(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const std::string& typeId = instance->typeId();
    const QRectF bodyRect(3.0, 6.0, width_ - 6.0, height_ - 12.0);

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.5));
    painter->setBrush(bodyFillColor(*instance, QColor(235, 235, 235)));
    painter->drawPath(buildMsiShapePath(MsiShapeKind::Rectangle, bodyRect));

    painter->setPen(QPen(QColor(20, 20, 20), 1.5));
    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        const qreal y = pinLocalPositions_[i].y();
        const bool isOutput = instance->pins()[i].direction == core::PinDirection::Output;
        painter->drawLine(QPointF(isOutput ? width_ : 0.0, y),
                           QPointF(isOutput ? bodyRect.right() : bodyRect.left(), y));
    }

    // Muesca de reloj: solo los tipos disparados por flanco (no el latch SR,
    // que es de nivel) - ver el comentario de buildClockTrianglePath.
    const bool edgeTriggered =
        typeId == "memory.dFlipFlop" || typeId == "memory.jkFlipFlop" || typeId == "memory.register";
    if (edgeTriggered) {
        for (std::size_t i = 0; i < instance->pins().size(); ++i) {
            if (instance->pins()[i].name == "CLK") {
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(20, 20, 20));
                painter->drawPath(buildClockTrianglePath(bodyRect, pinLocalPositions_[i].y(), 6.0));
                break;
            }
        }
    }

    painter->setPen(Qt::black);
    painter->drawText(bodyRect, Qt::AlignCenter, QString::fromStdString(instance->definition().displayName));
}

void ComponentItem::paintSubcircuit(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const QRectF bodyRect(3.0, 6.0, width_ - 6.0, height_ - 12.0);

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.5));
    painter->setBrush(bodyFillColor(*instance, QColor(235, 235, 235)));
    painter->drawRect(bodyRect);
    // Doble contorno: motivo de "documento embebido" (una caja dentro de
    // otra), para distinguirlo de un bloque MSI comun de una sola linea.
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(bodyRect.adjusted(3.0, 3.0, -3.0, -3.0));

    painter->setPen(QPen(QColor(20, 20, 20), 1.5));
    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        const qreal y = pinLocalPositions_[i].y();
        const bool isOutput = instance->pins()[i].direction == core::PinDirection::Output;
        painter->drawLine(QPointF(isOutput ? width_ : 0.0, y),
                           QPointF(isOutput ? bodyRect.right() : bodyRect.left(), y));
    }

    painter->setPen(Qt::black);
    painter->drawText(bodyRect, Qt::AlignCenter, QString::fromStdString(instance->definition().displayName));
}

void ComponentItem::paintIc74ls(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const QRectF bodyRect(3.0, 6.0, width_ - 6.0, height_ - 12.0);
    const QColor borderColor = selected ? QColor(30, 90, 220) : QColor(20, 20, 20);

    painter->setPen(QPen(borderColor, selected ? 2.0 : 1.5));
    painter->setBrush(bodyFillColor(*instance, QColor(43, 43, 43)));
    painter->drawRoundedRect(bodyRect, 3.0, 3.0);

    // Muesca semicircular sobre el borde superior (indicador de "pin 1" de
    // un IC DIP real): un recorte concavo, no un bulto que sobresale. El
    // rect de la muesca queda centrado justo sobre la linea del borde
    // superior (mitad afuera, mitad adentro) - primero se "borra" ese tramo
    // del borde con un chord relleno del mismo color de fondo, y despues se
    // dibuja solo el arco (la mitad de adentro) como el trazo del recorte.
    const QRectF notchRect(bodyRect.center().x() - 6.0, bodyRect.top() - 5.0, 12.0, 10.0);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bodyFillColor(*instance, QColor(43, 43, 43)));
    painter->drawChord(notchRect, 180 * 16, 180 * 16);
    painter->setPen(QPen(borderColor, 1.2));
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(notchRect, 180 * 16, 180 * 16);

    // Secuencia fisica real del DIP (ver ComponentDefinition::
    // physicalPinout), partida en columnas visuales - mismo criterio que
    // rebuildPins() (que ya la uso para calcular pinLocalPositions_/
    // decorativeLeftPositions_/decorativeRightPositions_). Recalcularla
    // aca solo cuesta separar y filtrar hasta 16 nombres, y evita tener que
    // guardar los nombres decorativos como miembros del item.
    const auto [leftSeq, rightSeq] = splitPhysicalPinout(instance->definition().physicalPinout);
    std::vector<std::string> decorativeLeftNames;
    for (const auto& entry : leftSeq) {
        if (!entry.isFunctional) {
            decorativeLeftNames.push_back(entry.label);
        }
    }
    std::vector<std::string> decorativeRightNames;
    for (const auto& entry : rightSeq) {
        if (!entry.isFunctional) {
            decorativeRightNames.push_back(entry.label);
        }
    }

    // El lado de cada pin real ya no se deduce de su direccion (entrada/
    // salida) - lo dice pinLocalPositions_, calculado en rebuildPins()
    // segun la posicion fisica real del DIP (que intercala entradas y
    // salidas segun el chip).
    painter->setPen(QPen(QColor(54, 54, 54).lighter(160), 1.2));
    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        const QPointF& localPos = pinLocalPositions_[i];
        const bool isRight = localPos.x() > width_ / 2.0;
        painter->drawLine(QPointF(isRight ? width_ : 0.0, localPos.y()),
                           QPointF(isRight ? bodyRect.right() : bodyRect.left(), localPos.y()));
    }

    // Pines decorativos (ver ComponentDefinition::PhysicalPin): la misma
    // linea de pin que los reales, pero punteada y sin PinItem/punto de
    // conexion - a simple vista se nota que no son cableables (existen
    // solo para que el cuerpo tenga la misma cantidad y posicion de pines
    // que el chip DIP real, ver rebuildPins()).
    painter->setPen(QPen(QColor(54, 54, 54).lighter(130), 1.0, Qt::DashLine));
    for (const QPointF& pos : decorativeLeftPositions_) {
        painter->drawLine(QPointF(0.0, pos.y()), QPointF(bodyRect.left(), pos.y()));
    }
    for (const QPointF& pos : decorativeRightPositions_) {
        painter->drawLine(QPointF(bodyRect.right(), pos.y()), QPointF(width_, pos.y()));
    }

    // Nombre de cada pin, igual criterio que paintSevenSegment/
    // paintHexDisplay - claro sobre el cuerpo oscuro, en vez de negro sobre
    // gris claro. El ancho de cada columna se mide de verdad
    // (QFontMetricsF), no un valor fijo comun a ambas.
    QFont pinFont = painter->font();
    pinFont.setPointSizeF(6.0); // reducido de 7.0 a pedido explicito
    painter->setFont(pinFont);
    const QFontMetricsF pinFontMetrics(pinFont);
    constexpr qreal labelMargin = 2.0;
    qreal leftLabelWidth = 0.0;
    qreal rightLabelWidth = 0.0;
    for (const auto& entry : leftSeq) {
        leftLabelWidth =
            std::max(leftLabelWidth, pinFontMetrics.horizontalAdvance(QString::fromStdString(entry.label)));
    }
    for (const auto& entry : rightSeq) {
        rightLabelWidth =
            std::max(rightLabelWidth, pinFontMetrics.horizontalAdvance(QString::fromStdString(entry.label)));
    }

    painter->setPen(QColor(210, 210, 210));
    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        const QPointF& localPos = pinLocalPositions_[i];
        const bool isRight = localPos.x() > width_ / 2.0;
        const QString name = QString::fromStdString(instance->pins()[i].name);
        if (isRight) {
            painter->drawText(QRectF(bodyRect.right() - labelMargin - rightLabelWidth, localPos.y() - 8.0,
                                      rightLabelWidth, 16.0),
                               Qt::AlignVCenter | Qt::AlignRight, name);
        } else {
            painter->drawText(QRectF(bodyRect.left() + labelMargin, localPos.y() - 8.0, leftLabelWidth, 16.0),
                               Qt::AlignVCenter | Qt::AlignLeft, name);
        }
    }

    // Etiquetas de los pines decorativos (incluye VCC/GND, ahora un pin
    // decorativo mas en su posicion fisica real - ya no un rotulo especial
    // centrado en el hueco), en un gris mas apagado que los reales
    // (210,210,210) para que se note a simple vista que no son cableables.
    painter->setPen(QColor(120, 120, 120));
    for (std::size_t i = 0; i < decorativeLeftNames.size(); ++i) {
        const qreal y = decorativeLeftPositions_[i].y();
        painter->drawText(QRectF(bodyRect.left() + labelMargin, y - 8.0, leftLabelWidth, 16.0),
                           Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(decorativeLeftNames[i]));
    }
    for (std::size_t i = 0; i < decorativeRightNames.size(); ++i) {
        const qreal y = decorativeRightPositions_[i].y();
        painter->drawText(QRectF(bodyRect.right() - labelMargin - rightLabelWidth, y - 8.0, rightLabelWidth, 16.0),
                           Qt::AlignVCenter | Qt::AlignRight, QString::fromStdString(decorativeRightNames[i]));
    }

    // Hueco central libre de etiquetas de pin (entre las dos columnas) -
    // usado por el numero de parte real de mas abajo.
    const qreal gapLeft = bodyRect.left() + labelMargin + leftLabelWidth + labelMargin;
    const qreal gapRight = bodyRect.right() - labelMargin - rightLabelWidth - labelMargin;
    const qreal gapWidth = gapRight - gapLeft;

    // Numero de parte real (p. ej. "7400", o "7447"/"7448" segun la
    // propiedad "variant" para bcdDriver), para poder distinguir a simple
    // vista que chip es sin abrir el inspector de propiedades. El texto no
    // entra horizontal en el hueco angosto que queda entre las dos columnas
    // de pines, asi que se dibuja rotado 90 grados, aprovechando el alto
    // del cuerpo (vertical, mayor que el hueco horizontal) - centrado tanto
    // en el hueco (horizontal) como en el cuerpo (vertical).
    QString partNumber;
    if (instance->definition().findProperty("variant") != nullptr) {
        // Unico tipo (ic74ls.bcdDriver) cuyo numero de parte depende de una
        // propiedad en vez de ser fijo - ver ComponentDefinition::partNumber.
        const QString variantSuffix =
            QString::fromStdString(std::get<std::string>(instance->property("variant"))).right(2);
        partNumber = QStringLiteral("74") + variantSuffix; // "7447"/"7448" 
    } else if (!instance->definition().partNumber.empty()) {
        partNumber = QString::fromStdString(instance->definition().partNumber);
    }
    if (!partNumber.isEmpty()) {
        QFont partFont = painter->font();
        partFont.setPointSizeF(6.0);
        partFont.setBold(true);
        const QFontMetricsF partFontMetrics(partFont);
        const qreal partTextWidth = partFontMetrics.horizontalAdvance(partNumber);
        constexpr qreal verticalMargin = 4.0; // un poco de aire arriba/abajo del cuerpo
        const qreal availableHeight = bodyRect.height() - 2 * verticalMargin;
        if (gapWidth > 4.0 && availableHeight >= partTextWidth) {
            painter->save();
            painter->translate((gapLeft + gapRight) / 2.0, bodyRect.center().y());
            painter->rotate(-90);
            painter->setFont(partFont);
            painter->setPen(QColor(150, 150, 150));
            painter->drawText(QRectF(-availableHeight / 2.0, -gapWidth / 2.0, availableHeight, gapWidth),
                               Qt::AlignCenter, partNumber);
            painter->restore();
        }
    }
}

void ComponentItem::paintLedMatrix(QPainter* painter, bool selected) {
    // Solo el fondo: cada celda la dibuja su propio PinItem (posicionado
    // exactamente sobre la grilla en rebuildPins(), coloreado con el mismo
    // criterio rojo encendido/gris apagado en PinItem::paint()) - el pin ES
    // la celda, asi que no hay nada mas que dibujar aca sin duplicar.
    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.2));
    painter->setBrush(QColor(40, 40, 40));
    painter->drawRoundedRect(QRectF(0.0, 0.0, width_, height_), 6.0, 6.0);
}

void ComponentItem::paintTerminal(QPainter* painter, bool selected) {
    const components::ComponentInstance* instance = document_->component(componentId_);
    const QRectF bodyRect(3.0, 6.0, width_ - 6.0, height_ - 12.0);

    painter->setPen(QPen(selected ? QColor(30, 90, 220) : QColor(20, 20, 20), selected ? 2.0 : 1.5));
    painter->setBrush(QColor(15, 30, 15));
    painter->drawRoundedRect(bodyRect, 4.0, 4.0);

    painter->setPen(QPen(QColor(20, 20, 20), 1.2));
    for (std::size_t i = 0; i < instance->pins().size(); ++i) {
        const qreal y = pinLocalPositions_[i].y();
        painter->drawLine(QPointF(0.0, y), QPointF(bodyRect.left(), y));
    }

    std::array<core::LogicValue, 8> bits{};
    for (std::size_t i = 0; i < bits.size(); ++i) {
        bits[i] = document_->pinValue(componentId_, static_cast<uint16_t>(i));
    }
    const std::optional<char> character = components::terminalCharacter(*instance, bits);

    QFont font = painter->font();
    font.setBold(true);
    font.setPointSizeF(14.0);
    painter->setFont(font);
    painter->setPen(QColor(80, 255, 120));
    painter->drawText(bodyRect, Qt::AlignCenter, character.has_value() ? QString(QChar(*character)) : QStringLiteral("?"));
}

} // namespace digitalforge::editor
