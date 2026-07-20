#include "IconFactory.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <cmath>

#include "editor/GateShapes.hpp"
#include "editor/MsiShapes.hpp"

namespace digitalforge::ui {

namespace {

constexpr int kSize = 20;
constexpr qreal kPi = 3.14159265358979323846;

// Los iconos se renderizan sobre un simple QImage en memoria en lugar de un
// QPixmap: QPixmap se respalda en una superficie nativa de la plataforma
// (GDI/Direct2D), que algunos entornos con sandbox no logran aprovisionar
// correctamente tan temprano (antes de que se muestre cualquier widget);
// QImage son datos de rasterizacion puros sin esa dependencia. El QImage se
// convierte a QPixmap solo al final, una unica vez, al envolverlo en un
// QIcon.
QImage canvas() {
    QImage img(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

// Ninguna funcion de icono cachea su QIcon - cada llamada vuelve a pintar
// desde cero y por lo tanto vuelve a leer el palette activo. Esto es lo que
// permite que ComponentPalette y SimulationToolbar regeneren sus iconos en
// caliente (volviendo a llamar a estas mismas funciones) cuando el usuario
// cambia el tema claro/oscuro del sistema, sin reiniciar la aplicacion.
bool isDarkPalette() { return QApplication::palette().color(QPalette::Base).lightness() < 128; }

// "Tinta": color de trazos/contornos que no tienen relleno propio (flechas de
// deshacer/rehacer, lupa de zoom...) y por lo tanto se dibujan directamente
// sobre el fondo del panel/barra de herramientas - deben invertirse con el
// tema para no desaparecer contra un fondo oscuro.
//
// "Papel": relleno neutro del cuerpo de un icono tipo "chip" (cuerpo de
// puerta, hoja de documento, etc.). A diferencia de la tinta, se mantiene
// siempre claro sin importar el tema: es el mismo criterio que ya usa
// ComponentItem::paintGate para las puertas colocadas en el lienzo (cuerpo
// gris claro fijo), que se distingue bien tanto sobre fondo claro como
// oscuro. Si "papel" tambien se oscureciera en tema oscuro, quedaria casi
// del mismo tono que el fondo del arbol de componentes y el icono se volveria
// ilegible - ese fue exactamente el bug reportado.
//
// Los acentos con significado propio (rojo de eliminar/LED, verde de
// ejecutar, carpeta amarilla...) se dejan fijos aparte de estas dos, ya que
// son legibles en ambos temas y cambiar su tono alteraria la identidad del
// icono.
QColor inkColor() { return isDarkPalette() ? QColor(225, 225, 225) : QColor(40, 40, 40); }
QColor paperColor() { return QColor(235, 235, 235); }

// Una flecha circular usada por deshacer/rehacer/reiniciar: un arco mas una
// pequena punta de flecha triangular en su extremo delantero. `sweepDegrees`
// > 0 dibuja en sentido horario.
void drawCircularArrow(QPainter& painter, qreal startDegrees, qreal sweepDegrees) {
    const QRectF rect(3.0, 3.0, kSize - 6.0, kSize - 6.0);
    painter.setPen(QPen(inkColor(), 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(rect, static_cast<int>(startDegrees * 16), static_cast<int>(sweepDegrees * 16));

    const qreal endDegrees = startDegrees + sweepDegrees;
    const qreal rad = endDegrees * kPi / 180.0;
    const QPointF center = rect.center();
    const qreal radius = rect.width() / 2.0;
    const QPointF tip(center.x() + radius * std::cos(rad), center.y() - radius * std::sin(rad));
    const qreal tangentRad = rad + (sweepDegrees > 0 ? -kPi / 2.0 : kPi / 2.0);
    const QPointF dir(std::cos(tangentRad), -std::sin(tangentRad));
    const QPointF normal(-dir.y(), dir.x());
    const qreal arrowSize = 3.5;
    const QPointF base = tip - dir * arrowSize;

    QPolygonF arrow;
    arrow << tip << (base + normal * arrowSize * 0.7) << (base - normal * arrowSize * 0.7);
    painter.setBrush(inkColor());
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(arrow);
}

} // namespace

namespace icons {

QIcon newDocument() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.moveTo(5, 2);
    path.lineTo(12, 2);
    path.lineTo(16, 6);
    path.lineTo(16, 18);
    path.lineTo(5, 18);
    path.closeSubpath();
    painter.setPen(QPen(inkColor(), 1.2));
    painter.setBrush(paperColor());
    painter.drawPath(path);
    painter.drawLine(QLineF(12, 2, 12, 6));
    painter.drawLine(QLineF(12, 6, 16, 6));
    return QIcon(QPixmap::fromImage(img));
}

QIcon newProject() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Mismo contorno de carpeta que open() (un proyecto es una carpeta) -
    // la insignia "+" es lo que distingue "crear una carpeta nueva" de
    // "abrir una ya existente".
    QPainterPath path;
    path.moveTo(2, 6);
    path.lineTo(8, 6);
    path.lineTo(10, 8);
    path.lineTo(18, 8);
    path.lineTo(17, 16);
    path.lineTo(3, 16);
    path.closeSubpath();
    painter.setPen(QPen(QColor(120, 90, 20), 1.2));
    painter.setBrush(QColor(250, 210, 120));
    painter.drawPath(path);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 150, 40));
    painter.drawEllipse(QPointF(15.5, 15.5), 4.5, 4.5);
    painter.setPen(QPen(Qt::white, 1.4));
    painter.drawLine(QLineF(15.5, 13.3, 15.5, 17.7));
    painter.drawLine(QLineF(13.3, 15.5, 17.7, 15.5));
    return QIcon(QPixmap::fromImage(img));
}

QIcon open() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.moveTo(2, 6);
    path.lineTo(8, 6);
    path.lineTo(10, 8);
    path.lineTo(18, 8);
    path.lineTo(17, 16);
    path.lineTo(3, 16);
    path.closeSubpath();
    painter.setPen(QPen(QColor(120, 90, 20), 1.2));
    painter.setBrush(QColor(250, 210, 120));
    painter.drawPath(path);
    return QIcon(QPixmap::fromImage(img));
}

QIcon save() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(inkColor(), 1.2));
    painter.setBrush(QColor(70, 80, 100));
    painter.drawRoundedRect(QRectF(3, 3, 14, 14), 2.0, 2.0);
    painter.setBrush(paperColor());
    painter.drawRect(QRectF(6, 3, 7, 5));
    painter.setBrush(inkColor());
    painter.drawRect(QRectF(12, 4, 2, 3));
    painter.setBrush(paperColor());
    painter.drawRect(QRectF(5, 12, 10, 5));
    return QIcon(QPixmap::fromImage(img));
}

QIcon undo() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawCircularArrow(painter, 200.0, 220.0);
    return QIcon(QPixmap::fromImage(img));
}

QIcon redo() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawCircularArrow(painter, -20.0, -220.0);
    return QIcon(QPixmap::fromImage(img));
}

QIcon run() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 150, 40));
    QPolygonF triangle;
    triangle << QPointF(5, 3) << QPointF(5, 17) << QPointF(17, 10);
    painter.drawPolygon(triangle);
    return QIcon(QPixmap::fromImage(img));
}

QIcon pause() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(160, 120, 20));
    painter.drawRect(QRectF(5, 3, 4, 14));
    painter.drawRect(QRectF(11, 3, 4, 14));
    return QIcon(QPixmap::fromImage(img));
}

QIcon step() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 90, 200));
    painter.drawRect(QRectF(3, 3, 3, 14));
    QPolygonF triangle;
    triangle << QPointF(8, 4) << QPointF(8, 16) << QPointF(17, 10);
    painter.drawPolygon(triangle);
    return QIcon(QPixmap::fromImage(img));
}

QIcon reset() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawCircularArrow(painter, 30.0, 300.0);
    return QIcon(QPixmap::fromImage(img));
}

QIcon polarity(bool positiveLogic) {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (positiveLogic) {
        painter.setPen(QPen(inkColor(), 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(6, 3, 8, 14), 3.0, 5.0);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(inkColor());
        painter.drawRect(QRectF(8.5, 3, 3, 14));
        QPolygonF flag;
        flag << QPointF(5.5, 6) << QPointF(8.5, 3) << QPointF(8.5, 7.5);
        painter.drawPolygon(flag);
    }
    return QIcon(QPixmap::fromImage(img));
}

QIcon zoomIn() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(inkColor(), 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(2, 2, 11, 11));
    painter.drawLine(QLineF(11, 11, 17, 17));
    painter.drawLine(QLineF(5, 7.5, 10, 7.5));
    painter.drawLine(QLineF(7.5, 5, 7.5, 10));
    return QIcon(QPixmap::fromImage(img));
}

QIcon zoomOut() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(inkColor(), 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(2, 2, 11, 11));
    painter.drawLine(QLineF(11, 11, 17, 17));
    painter.drawLine(QLineF(5, 7.5, 10, 7.5));
    return QIcon(QPixmap::fromImage(img));
}

QIcon deleteItem() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(170, 30, 30), 2.2));
    painter.drawLine(QLineF(5, 5, 15, 15));
    painter.drawLine(QLineF(15, 5, 5, 15));
    return QIcon(QPixmap::fromImage(img));
}

QIcon rotate() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawCircularArrow(painter, 20.0, 280.0);
    painter.setPen(QPen(inkColor(), 1.2));
    painter.setBrush(paperColor());
    painter.drawRect(QRectF(8, 8, 4, 4));
    return QIcon(QPixmap::fromImage(img));
}

// Estos dos reemplazan los botones nativos de flotar/cerrar de la barra de
// titulo de un QDockWidget (ver MainWindow::buildDockTitleBar): el estilo
// nativo de Windows no siempre entinta esos botones con un color que se
// distinga sobre una barra de titulo oscura, asi que se dibujan aqui con la
// misma tinta adaptable al tema que el resto del chrome.
QIcon dockFloat() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(inkColor(), 1.4));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(4, 7, 10, 9));
    painter.drawLine(QLineF(10, 8, 16, 2));
    painter.drawLine(QLineF(12, 2, 16, 2));
    painter.drawLine(QLineF(16, 2, 16, 6));
    return QIcon(QPixmap::fromImage(img));
}

QIcon dockClose() {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(inkColor(), 1.6));
    painter.drawLine(QLineF(6, 6, 14, 14));
    painter.drawLine(QLineF(14, 6, 6, 14));
    return QIcon(QPixmap::fromImage(img));
}

// Chinche de oficina: cabeza achatada (la chapa metalica) + cuerpo que se
// afina hasta la punta, silueta solida (sin contorno) para que se lea bien
// a 20x20. `pinned` inclina todo el dibujo ~40 grados alrededor del centro
// del icono para la variante "auto-hide" (misma idea que el pin inclinado
// de Visual Studio). Un unico color solido (inkColor(), ya adaptado al tema
// claro/oscuro activo - ver isDarkPalette()) en vez de los colores fijos
// de una chinche real, para no romper el estilo monocromo del resto de los
// iconos de esta clase.
QIcon dockPin(bool pinned) {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(10, 10);
    if (!pinned) {
        painter.rotate(40);
    }
    painter.translate(-10, -10);

    painter.setPen(Qt::NoPen);
    painter.setBrush(inkColor());
    painter.drawRoundedRect(QRectF(5, 3, 10, 6), 2.5, 2.5);
    QPainterPath needle;
    needle.moveTo(7, 8.5);
    needle.lineTo(13, 8.5);
    needle.lineTo(10, 17);
    needle.closeSubpath();
    painter.drawPath(needle);

    return QIcon(QPixmap::fromImage(img));
}

} // namespace icons

QIcon componentIcon(const components::ComponentDefinition& definition) {
    QImage img = canvas();
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const std::string& typeId = definition.typeId;

    if (typeId.rfind("gates.", 0) == 0) {
        const editor::GateShapeKind kind = editor::gateShapeKindForTypeId(typeId);
        const bool hasBubble = editor::gateHasOutputBubble(typeId);
        const QRectF rect(2.0, 3.0, hasBubble ? 12.0 : 15.0, 14.0);
        painter.setPen(QPen(inkColor(), 1.2));
        painter.setBrush(paperColor());
        painter.drawPath(editor::buildGateShapePath(kind, rect));
        if (editor::gateHasExtraOrCurve(typeId)) {
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(editor::buildOrExtraCurve(rect, 4.0));
        }
        if (hasBubble) {
            painter.setBrush(paperColor());
            painter.drawEllipse(QPointF(rect.right() + 2.5, rect.center().y()), 2.5, 2.5);
        }
    } else if (typeId == "wiring.input") {
        painter.setPen(QPen(inkColor(), 1.4));
        painter.drawLine(QLineF(2, 10, 12, 10));
        painter.setBrush(QColor(90, 90, 200));
        painter.drawEllipse(QPointF(15, 10), 3, 3);
    } else if (typeId == "wiring.clock") {
        // Onda cuadrada (linea escalonada 0-1-0-1, el simbolo universal de
        // reloj en cualquier esquematico/Logisim) rematada con el mismo par
        // "linea de pin + punto azul" que wiring.input: el reloj tambien es
        // un driver de una unica net (en esencia, un input que se alterna
        // solo), asi que el punto lo emparenta visualmente con el resto de
        // wiring.* en vez de quedar como una onda suelta sin pin (el icono
        // anterior).
        painter.setPen(QPen(inkColor(), 1.4));
        painter.setBrush(Qt::NoBrush);
        QPainterPath wave;
        wave.moveTo(2, 13);
        wave.lineTo(2, 7);
        wave.lineTo(7, 7);
        wave.lineTo(7, 13);
        wave.lineTo(12, 13);
        wave.lineTo(12, 7);
        painter.drawPath(wave);
        painter.drawLine(QLineF(12, 7, 15, 7));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(90, 90, 200));
        painter.drawEllipse(QPointF(17, 7), 2.2, 2.2);
    } else if (typeId == "wiring.output") {
        painter.setPen(QPen(inkColor(), 1.4));
        painter.setBrush(QColor(200, 90, 90));
        painter.drawEllipse(QPointF(5, 10), 3, 3);
        painter.drawLine(QLineF(8, 10, 18, 10));
    } else if (typeId == "wiring.constant") {
        // Una fuente fija se dibuja como un pequeno cuadrado relleno (a
        // diferencia de los circulos usados para los pines de entrada/salida)
        // con una linea recta que lo atraviesa, representando un nivel de CC
        // constante. Se evita deliberadamente QPainter::drawText: renderizar
        // texto sobre un QPixmap fuera de pantalla tan temprano (antes de que
        // se muestre el primer widget) provoca fallos en algunos entornos
        // (el backend de fuentes/DirectWrite aun no se ha inicializado), por
        // lo que cada icono aqui se construye en su lugar a partir de
        // primitivas vectoriales simples.
        painter.setPen(QPen(inkColor(), 1.2));
        painter.setBrush(paperColor());
        painter.drawRect(QRectF(3, 4, 14, 12));
        painter.drawLine(QLineF(5, 10, 15, 10));
    } else if (typeId == "wiring.pullResistor") {
        // Mismo zigzag de resistencia + flecha que
        // editor::ComponentItem::paintPullResistor(), reducido a 20x20 -
        // por defecto pull-up (flecha hacia arriba, valor "1").
        painter.setPen(QPen(inkColor(), 1.3));
        painter.drawLine(QLineF(1, 10, 5, 10));
        QPainterPath zigzag;
        zigzag.moveTo(5, 10);
        zigzag.lineTo(7, 6);
        zigzag.lineTo(10, 14);
        zigzag.lineTo(13, 6);
        zigzag.lineTo(15, 10);
        painter.drawPath(zigzag);
        painter.drawLine(QLineF(15, 10, 19, 10));
        painter.drawLine(QLineF(10, 5, 10, 1));
        QPainterPath arrowHead;
        arrowHead.moveTo(10, 1);
        arrowHead.lineTo(8, 4);
        arrowHead.lineTo(12, 4);
        arrowHead.closeSubpath();
        painter.setBrush(inkColor());
        painter.setPen(Qt::NoPen);
        painter.drawPath(arrowHead);
    } else if (typeId == "wiring.powerOnReset") {
        // Mismo par "linea de pin + punto azul" que wiring.input/clock, con
        // un unico escalon (sube y se queda) en vez de una onda repetida -
        // un reinicio al encender es un pulso unico, no periodico.
        painter.setPen(QPen(inkColor(), 1.4));
        painter.setBrush(Qt::NoBrush);
        QPainterPath step;
        step.moveTo(2, 13);
        step.lineTo(8, 13);
        step.lineTo(8, 7);
        step.lineTo(12, 7);
        painter.drawPath(step);
        painter.drawLine(QLineF(12, 7, 15, 7));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(90, 90, 200));
        painter.drawEllipse(QPointF(17, 7), 2.2, 2.2);
    } else if (typeId == "wiring.doNotConnect") {
        // Circulo tachado (senal de "prohibido") - sin pines, puramente
        // documental.
        painter.setPen(QPen(inkColor(), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(10, 10), 7, 7);
        painter.drawLine(QLineF(5, 5, 15, 15));
    } else if (typeId == "wiring.tunnel") {
        // Cable corto + circulo hueco punteado (a diferencia del circulo
        // solido de Input/Output/Constant) - no es un pin real, la conexion
        // la hace CircuitDocument por etiqueta compartida.
        painter.setPen(QPen(inkColor(), 1.4));
        painter.drawLine(QLineF(2, 10, 14, 10));
        painter.setPen(QPen(inkColor(), 1.4, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(15, 10), 3, 3);
    } else if (typeId == "wiring.ground") {
        // Mismo simbolo de tierra (IEC) que
        // editor::ComponentItem::paintGround(), rotado 90 grados (tallo
        // horizontal hacia el pin, barras verticales decrecientes).
        painter.setPen(QPen(inkColor(), 1.4));
        painter.drawLine(QLineF(12, 10, 19, 10));
        painter.drawLine(QLineF(12, 5, 12, 15));
        painter.drawLine(QLineF(9, 7, 9, 13));
        painter.drawLine(QLineF(6, 8, 6, 12));
    } else if (typeId == "wiring.transistor") {
        // Mismo simbolo tipo BJT (Gate a la izquierda + patas en angulo a
        // Source/Drain a la derecha + flecha NPN/PNP) que
        // editor::ComponentItem::paintTransistor(), reducido a 20x20 - por
        // defecto NMOS (flecha hacia afuera).
        painter.setPen(QPen(inkColor(), 1.3));
        painter.drawLine(QLineF(8, 5, 8, 15));    // barra
        painter.drawLine(QLineF(1, 10, 8, 10));   // Gate
        painter.drawLine(QLineF(8, 8, 13, 4));    // diagonal a Source
        painter.drawLine(QLineF(13, 4, 19, 4));
        painter.drawLine(QLineF(8, 12, 13, 16));  // diagonal a Drain
        painter.drawLine(QLineF(13, 16, 19, 16));
        QPainterPath arrow;
        arrow.moveTo(12.0, 4.4);
        arrow.lineTo(9.2, 5.6);
        arrow.lineTo(10.9, 7.3);
        arrow.closeSubpath();
        painter.setBrush(inkColor());
        painter.setPen(Qt::NoPen);
        painter.drawPath(arrow);
    } else if (typeId == "wiring.transmissionGate") {
        // Misma barra + patas que wiring.transistor, con dos cables de
        // control (N sin burbuja, P con burbuja) en vez de uno - ver
        // editor::ComponentItem::paintTransmissionGate().
        painter.setPen(QPen(inkColor(), 1.3));
        painter.drawLine(QLineF(8, 4, 8, 16));   // barra (mas alta: cubre N y P)
        painter.drawLine(QLineF(1, 7, 8, 7));    // N
        painter.drawLine(QLineF(1, 13, 5, 13));  // P (hasta la burbuja)
        painter.drawEllipse(QPointF(6.5, 13), 1.7, 1.7);
        painter.drawLine(QLineF(8, 8, 13, 4));   // diagonal a A
        painter.drawLine(QLineF(13, 4, 19, 4));
        painter.drawLine(QLineF(8, 12, 13, 16)); // diagonal a Y
        painter.drawLine(QLineF(13, 16, 19, 16));
    } else if (typeId == "io.led") {
        painter.setPen(QPen(inkColor(), 1.2));
        painter.setBrush(QColor(255, 70, 70));
        painter.drawEllipse(QRectF(4, 4, 12, 12));
    } else if (typeId == "io.seven_segment") {
        painter.setPen(QPen(QColor(200, 30, 30), 2.2));
        painter.drawLine(QLineF(6, 4, 14, 4));
        painter.drawLine(QLineF(14, 4, 14, 10));
        painter.drawLine(QLineF(14, 10, 14, 16));
        painter.drawLine(QLineF(6, 16, 14, 16));
        painter.drawLine(QLineF(6, 10, 6, 16));
        painter.drawLine(QLineF(6, 4, 6, 10));
        painter.drawLine(QLineF(6, 10, 14, 10));
    } else if (typeId == "io.hexDisplay") {
        // Mismo path que io.seven_segment: visualmente es el mismo
        // dispositivo fisico, solo decodificado internamente (ver
        // editor::ComponentItem::paintHexDisplay()).
        painter.setPen(QPen(QColor(200, 30, 30), 2.2));
        painter.drawLine(QLineF(6, 4, 14, 4));
        painter.drawLine(QLineF(14, 4, 14, 10));
        painter.drawLine(QLineF(14, 10, 14, 16));
        painter.drawLine(QLineF(6, 16, 14, 16));
        painter.drawLine(QLineF(6, 10, 6, 16));
        painter.drawLine(QLineF(6, 4, 6, 10));
        painter.drawLine(QLineF(6, 10, 14, 10));
    } else if (typeId == "io.ledMatrix") {
        painter.setPen(QPen(inkColor(), 1.0));
        painter.setBrush(QColor(255, 70, 70));
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                painter.drawEllipse(QPointF(5.0 + col * 5.0, 5.0 + row * 5.0), 1.6, 1.6);
            }
        }
    } else if (typeId == "io.terminal") {
        painter.setPen(QPen(inkColor(), 1.2));
        painter.setBrush(QColor(20, 30, 20));
        painter.drawRoundedRect(QRectF(2, 4, 16, 12), 1.5, 1.5);
        painter.setPen(QPen(QColor(80, 220, 120), 1.6));
        painter.drawLine(QLineF(5, 13, 8, 13));
        painter.drawLine(QLineF(5, 13, 5, 8));
        painter.drawLine(QLineF(5, 8, 8, 8));
    } else if (typeId == "debug.probe") {
        painter.setPen(QPen(inkColor(), 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(4, 4, 12, 12));
        painter.drawLine(QLineF(10, 1, 10, 5));
        painter.drawLine(QLineF(10, 15, 10, 19));
    } else if (typeId == "plexers.multiplexer" || typeId == "plexers.priorityEncoder" ||
               typeId == "plexers.decoder" || typeId == "plexers.demultiplexer") {
        const bool fanOut = typeId == "plexers.decoder" || typeId == "plexers.demultiplexer";
        const editor::MsiShapeKind kind = fanOut ? editor::MsiShapeKind::FanOutTrapezoid : editor::MsiShapeKind::FanInTrapezoid;
        painter.setPen(QPen(inkColor(), 1.2));
        painter.setBrush(paperColor());
        painter.drawPath(editor::buildMsiShapePath(kind, QRectF(2, 3, 16, 14)));
    } else if (typeId == "arithmetic.adder" || typeId == "arithmetic.subtractor" || typeId == "arithmetic.comparator") {
        const QRectF rect(2, 3, 16, 14);
        painter.setPen(QPen(inkColor(), 1.2));
        painter.setBrush(paperColor());
        painter.drawPath(editor::buildMsiShapePath(editor::MsiShapeKind::Rectangle, rect));
        painter.setPen(QPen(inkColor(), 1.6));
        const QPointF center = rect.center();
        if (typeId == "arithmetic.adder") {
            painter.drawLine(QLineF(center.x() - 3, center.y(), center.x() + 3, center.y()));
            painter.drawLine(QLineF(center.x(), center.y() - 3, center.x(), center.y() + 3));
        } else if (typeId == "arithmetic.subtractor") {
            painter.drawLine(QLineF(center.x() - 3, center.y(), center.x() + 3, center.y()));
        } else {
            painter.drawLine(QLineF(center.x() - 3, center.y() - 1.5, center.x() + 3, center.y() - 1.5));
            painter.drawLine(QLineF(center.x() - 3, center.y() + 1.5, center.x() + 3, center.y() + 1.5));
        }
    } else if (typeId.rfind("memory.", 0) == 0) {
        const QRectF rect(2, 3, 16, 14);
        painter.setPen(QPen(inkColor(), 1.2));
        painter.setBrush(paperColor());
        painter.drawPath(editor::buildMsiShapePath(editor::MsiShapeKind::Rectangle, rect));
        const bool edgeTriggered = typeId == "memory.dFlipFlop" || typeId == "memory.jkFlipFlop" || typeId == "memory.register";
        if (edgeTriggered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(inkColor());
            painter.drawPath(editor::buildClockTrianglePath(rect, rect.bottom() - 3.0, 4.0));
        }
    } else if (typeId == "structural.subcircuit") {
        const QRectF rect(2, 3, 16, 14);
        painter.setPen(QPen(inkColor(), 1.2));
        painter.setBrush(paperColor());
        painter.drawRect(rect);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect.adjusted(2.5, 2.5, -2.5, -2.5));
    } else if (typeId.rfind("ic74ls.", 0) == 0) {
        // Cuerpo oscuro de plastico + muesca semicircular concava de "pin 1"
        // (recorte, no un bulto) - mismo criterio que
        // editor::ComponentItem::paintIc74ls().
        const QRectF rect(3, 4, 14, 12);
        painter.setPen(QPen(inkColor(), 1.2));
        painter.setBrush(QColor(43, 43, 43));
        painter.drawRoundedRect(rect, 1.5, 1.5);
        const QRectF notchRect(rect.center().x() - 2.5, rect.top() - 2.0, 5, 4);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(43, 43, 43));
        painter.drawChord(notchRect, 180 * 16, 180 * 16);
        painter.setPen(QPen(inkColor(), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(notchRect, 180 * 16, 180 * 16);
    } else {
        painter.setPen(QPen(inkColor(), 1.0));
        painter.setBrush(paperColor());
        painter.drawRoundedRect(QRectF(3, 3, 14, 14), 2.0, 2.0);
    }

    return QIcon(QPixmap::fromImage(img));
}

} // namespace digitalforge::ui
