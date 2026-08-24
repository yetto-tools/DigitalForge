#pragma once

#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <string>

namespace digitalforge::editor {

// Contornos de cuerpo para bloques funcionales MSI (Plexers/Aritmetica/
// Memoria/Subcircuitos) - a diferencia de GateShapes.hpp (compuertas
// basicas AND/OR/NOT con forma distintiva propia de cada funcion), estos
// bloques son mas grandes y con muchos pines, asi que ANSI/IEEE Std 91 los
// trata como cajas rectangulares o trapezoidales genericas con su nombre
// adentro, no con un simbolo distintivo por tipo. FanInTrapezoid es "muchos
// a uno" (ancho del lado de las entradas, angosto del lado de la salida:
// multiplexor, codificador de prioridad); FanOutTrapezoid es su espejo,
// "uno a muchos" (decodificador, demultiplexor).
enum class MsiShapeKind { Rectangle, FanInTrapezoid, FanOutTrapezoid };

[[nodiscard]] inline QPainterPath buildMsiShapePath(MsiShapeKind kind, QRectF rect) {
    QPainterPath path;
    if (kind == MsiShapeKind::Rectangle) {
        path.addRect(rect);
        return path;
    }
    // El lado angosto se achica a mitad de la altura del rectangulo
    // (relativo al centro vertical), no a un valor fijo - asi el trapecio
    // sigue viendose proporcionado sin importar cuantos pines (y por lo
    // tanto que alto) tenga la instancia.
    const qreal narrowInset = rect.height() * 0.28;
    const bool narrowOnLeft = kind == MsiShapeKind::FanOutTrapezoid;
    const qreal leftTopY = narrowOnLeft ? rect.top() + narrowInset : rect.top();
    const qreal leftBottomY = narrowOnLeft ? rect.bottom() - narrowInset : rect.bottom();
    const qreal rightTopY = narrowOnLeft ? rect.top() : rect.top() + narrowInset;
    const qreal rightBottomY = narrowOnLeft ? rect.bottom() : rect.bottom() - narrowInset;

    path.moveTo(rect.left(), leftTopY);
    path.lineTo(rect.right(), rightTopY);
    path.lineTo(rect.right(), rightBottomY);
    path.lineTo(rect.left(), leftBottomY);
    path.closeSubpath();
    return path;
}

// Muesca triangular chica que indica una entrada de reloj disparada por
// flanco (biestables D/JK, registro) - convencion ANSI/IEEE estandar del
// simbolo ">" apuntando hacia adentro del cuerpo, sobre el borde izquierdo
// a la altura `y` (la del pin CLK real en el lienzo, o un valor fijo en el
// icono chico donde no hay pines reales que consultar).
[[nodiscard]] inline QPainterPath buildClockTrianglePath(QRectF rect, qreal y, qreal size) {
    QPainterPath path;
    path.moveTo(rect.left(), y - size / 2.0);
    path.lineTo(rect.left() + size, y);
    path.lineTo(rect.left(), y + size / 2.0);
    path.closeSubpath();
    return path;
}

// Sigla corta para el centro del cuerpo de un bloque memory.* (ver
// ComponentItem::paintMemory()) o su icono chico de paleta (ver
// ui::IconFactory::componentIcon()) - el displayName completo ("Flip-Flop
// D") no entra ni en el cuerpo de 48px ni en un icono de paleta. Cadena
// vacia para cualquier typeId que no sea uno de los cinco tipos memory.*
// conocidos (los llamadores caen de vuelta al displayName completo en ese
// caso).
[[nodiscard]] inline QString memoryCenterLabel(const std::string& typeId) {
    if (typeId == "memory.srLatch") return QStringLiteral("SR");
    if (typeId == "memory.dFlipFlop") return QStringLiteral("D");
    if (typeId == "memory.jkFlipFlop") return QStringLiteral("JK");
    if (typeId == "memory.tFlipFlop") return QStringLiteral("T");
    if (typeId == "memory.register") return QStringLiteral("REG");
    return QString();
}

// Sigla corta para el centro del cuerpo de un bloque plexers.* (ver
// ComponentItem::paintPlexer()) o su icono chico de paleta - mismo motivo
// que memoryCenterLabel() de arriba: el displayName completo
// ("Codificador de prioridad") no entra en un cuerpo angosto ni en un
// icono de paleta.
[[nodiscard]] inline QString plexerCenterLabel(const std::string& typeId) {
    if (typeId == "plexers.decoder") return QStringLiteral("DEC");
    if (typeId == "plexers.multiplexer") return QStringLiteral("MUX");
    if (typeId == "plexers.demultiplexer") return QStringLiteral("DEMUX");
    if (typeId == "plexers.priorityEncoder") return QStringLiteral("PRI");
    return QString();
}

// Sigla corta para el centro del cuerpo de un bloque arithmetic.* (ver
// ComponentItem::paintArithmetic()) - mismo motivo que plexerCenterLabel() de
// arriba: el displayName completo ("Restador") no entra en el cuerpo angosto
// una vez que este tambien rotula sus pines (A0/B0/Sum0/Cin/etc.) a los
// costados.
[[nodiscard]] inline QString arithmeticCenterLabel(const std::string& typeId) {
    if (typeId == "arithmetic.adder") return QStringLiteral("ADD");
    if (typeId == "arithmetic.subtractor") return QStringLiteral("SUB");
    if (typeId == "arithmetic.comparator") return QStringLiteral("CMP");
    return QString();
}

} // namespace digitalforge::editor
