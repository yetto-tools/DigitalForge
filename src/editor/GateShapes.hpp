#pragma once

#include <QPainterPath>
#include <QRectF>

#include <string>

namespace digitalforge::editor {

// Contornos de cuerpo de puerta estandar segun ANSI/IEEE Std 91 (convencion
// de esquematico de dominio publico, no copiada de ninguna herramienta de
// terceros): AND tiene forma de "D", OR/XOR/NOR/XNOR comparten la forma de
// escudo/bala, NOT es un triangulo. La negacion de salida se dibuja aparte
// como una pequena burbuja en la punta; XOR/XNOR agregan una segunda linea
// curva justo detras del borde posterior del cuerpo.
enum class GateShapeKind { And, Or, Not };

[[nodiscard]] inline GateShapeKind gateShapeKindForTypeId(const std::string& typeId) {
    if (typeId == "gates.and" || typeId == "gates.nand") {
        return GateShapeKind::And;
    }
    if (typeId == "gates.not" || typeId == "gates.buffer" || typeId == "gates.tristateBuffer" ||
        typeId == "gates.tristateInverter") {
        return GateShapeKind::Not; // buffer/triestado son el mismo triangulo, con o sin burbuja
    }
    return GateShapeKind::Or; // or/nor/xor/xnor
}

[[nodiscard]] inline bool gateHasOutputBubble(const std::string& typeId) {
    return typeId == "gates.not" || typeId == "gates.nand" || typeId == "gates.nor" || typeId == "gates.xnor" ||
           typeId == "gates.tristateInverter";
}

[[nodiscard]] inline bool gateHasExtraOrCurve(const std::string& typeId) {
    return typeId == "gates.xor" || typeId == "gates.xnor";
}

// Punto donde realmente termina el contorno dibujado del cuerpo en su lado
// de salida, para conectarle el terminal/burbuja de salida. Or y Not dibujan
// ambos hasta rect.right(), pero la curva "D" de And es un semicirculo cuyo
// radio es la mitad de la altura de rect, no la mitad de su ancho - cuando
// rect es mucho mas ancho que alto (el caso habitual, ya que width_ es fijo
// mientras que height_ se reduce para ajustarse a una cantidad pequena de
// pines) el punto mas a la derecha de esa curva queda bastante antes de
// rect.right(), dejando un hueco visible entre el cuerpo de la puerta y su
// pin.
[[nodiscard]] inline qreal gateBodyRightEdge(GateShapeKind kind, QRectF rect) {
    if (kind == GateShapeKind::And) {
        return rect.left() + rect.width() / 2.0 + rect.height() / 2.0;
    }
    return rect.right();
}

[[nodiscard]] inline QPainterPath buildGateShapePath(GateShapeKind kind, QRectF rect) {
    QPainterPath path;
    switch (kind) {
        case GateShapeKind::And: {
            const qreal midX = rect.left() + rect.width() / 2.0;
            path.moveTo(rect.left(), rect.top());
            path.lineTo(midX, rect.top());
            path.arcTo(QRectF(midX - rect.height() / 2.0, rect.top(), rect.height(), rect.height()), 90, -180);
            path.lineTo(rect.left(), rect.bottom());
            path.closeSubpath();
            break;
        }
        case GateShapeKind::Or: {
            const qreal midY = rect.top() + rect.height() / 2.0;
            path.moveTo(rect.left(), rect.top());
            path.quadTo(rect.left() + rect.width() * 0.55, rect.top(), rect.right(), midY);
            path.quadTo(rect.left() + rect.width() * 0.55, rect.bottom(), rect.left(), rect.bottom());
            path.quadTo(rect.left() + rect.width() * 0.22, midY, rect.left(), rect.top());
            path.closeSubpath();
            break;
        }
        case GateShapeKind::Not: {
            path.moveTo(rect.left(), rect.top());
            path.lineTo(rect.right(), rect.top() + rect.height() / 2.0);
            path.lineTo(rect.left(), rect.bottom());
            path.closeSubpath();
            break;
        }
    }
    return path;
}

// La linea curva adicional que XOR/XNOR dibujan justo afuera (a la izquierda)
// del borde posterior del cuerpo de OR, sin conectarse a el.
[[nodiscard]] inline QPainterPath buildOrExtraCurve(QRectF rect, qreal offset) {
    QPainterPath path;
    const qreal midY = rect.top() + rect.height() / 2.0;
    const qreal x = rect.left() - offset;
    path.moveTo(x, rect.top());
    path.quadTo(x + rect.width() * 0.22, midY, x, rect.bottom());
    return path;
}

} // namespace digitalforge::editor
