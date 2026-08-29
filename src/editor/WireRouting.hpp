#pragma once

#include <QPainterPath>
#include <QPointF>
#include <cstddef>
#include <vector>

// Geometria ortogonal de cables, con el modelo de Proteus: la forma de un
// cable es una FUNCION PURA de sus puntos guardados (extremos + quiebres), sin
// busquedas ni esquive automatico de obstaculos. Antes habia dos motores con
// reglas distintas -- uno que esquivaba componentes mientras el cable no
// tuviera quiebres, y otro de codos simples apenas aparecia el primero -- asi
// que la forma cambiaba de reglas de golpe al editarla, y ademas un cable se
// re-ruteaba solo cuando algo se movia cerca. Todo lo de aca es deterministico:
// los mismos puntos dan siempre exactamente el mismo trazado.
namespace digitalforge::editor {

// Umbral (en pixeles de escena) por debajo del cual dos puntos se tratan como
// si ya compartieran x/y, para no dibujar un codo residual visible cuando dos
// pines estan casi -pero no exactamente- alineados. Igual a PinItem::kRadius,
// para que la correccion quede escondida dentro del propio punto del pin.
inline constexpr qreal kWireAlignTolerance = 3.0;

// Umbral (en pixeles de escena) para moveWireCorner(): que tan cerca tiene
// que pasar el cursor de la x/y de un vertice VECINO para que la esquina se
// enganche ahi en vez de quedar a una distancia residual. Mucho mas generoso
// que kWireAlignTolerance (que es para geometria ya asentada, casi exacta) a
// proposito: es el margen de error de un arrastre a mano, no el de datos ya
// guardados. Sin este enganche, alinear una esquina a ojo con el trazado de
// al lado quedaba torcido por unos pixeles casi siempre -- cada intento de
// "dejarlo derecho" quedaba un poco chueco y el cable se veia lleno de
// quiebres minusculos en vez de una sola linea recta.
inline constexpr qreal kCornerAlignSnapTolerance = 10.0;

// Distancia de `p` al segmento [a, b], y de paso (si se pide) el punto de ese
// segmento mas cercano a `p`. Compartida por el hit-testing de WireItem
// (nearestPointOnPath()) y por splitWireWaypoints() -- antes cada uno tenia su
// propia copia de esta proyeccion punto-segmento, con el riesgo de que
// divergieran en un futuro ajuste.
[[nodiscard]] qreal distanceToSegment(QPointF p, QPointF a, QPointF b, QPointF* projectionOut = nullptr);

// Los vertices por los que pasa el cable que une `points` (>=2, tipicamente
// [extremoA, quiebres..., extremoB]): recto donde dos puntos consecutivos ya
// comparten x o y, y un unico codo en L (horizontal primero) donde no. Es la
// lista sobre la que razona el hit-testing, y coincide exactamente con lo que
// dibuja buildWirePath().
[[nodiscard]] std::vector<QPointF> wireVertices(const std::vector<QPointF>& points);

// El trazado de wireVertices() como QPainterPath. Path vacio si hay menos de
// dos puntos.
[[nodiscard]] QPainterPath buildWirePath(const std::vector<QPointF>& points);

// Quita de una polilinea los vertices redundantes: duplicados consecutivos
// (dentro de kWireAlignTolerance) y puntos colineales (un vertice que cae
// sobre la recta entre su vecino anterior y el siguiente, es decir, no es una
// esquina real). Conserva siempre el primero y el ultimo (los extremos).
[[nodiscard]] std::vector<QPointF> simplifyOrthogonalPolyline(const std::vector<QPointF>& vertices);

// Desplaza el segmento `segmentIndex` (el que va de points[i] a points[i+1])
// hasta `cursorPos`, perpendicular a si mismo: un tramo horizontal solo cambia
// de y, uno vertical solo de x, de modo que los angulos rectos con sus vecinos
// se conservan solos. Es el gesto central del cableado estilo Proteus.
//
// points.front()/back() son los extremos, anclados a un pin o punto de union:
// no se pueden mover. Si el segmento arrastrado toca uno, el extremo se deja
// donde estaba y se inserta un vertice extra que absorbe el quiebre (el cable
// "se estira" desde el pin en vez de despegarse de el).
[[nodiscard]] std::vector<QPointF> moveWireSegment(const std::vector<QPointF>& points, std::size_t segmentIndex,
                                                   QPointF cursorPos);

// Lleva la esquina interior `cornerIndex` a `cursorPos`. Los brazos siguen
// ortogonales porque wireVertices() vuelve a resolver cada tramo; no hace falta
// deslizar los vecinos a mano. Devuelve `points` sin cambios si el indice es un
// extremo (esos no se mueven: los ancla su pin).
//
// Si `cursorPos` pasa a menos de kCornerAlignSnapTolerance de la x o la y de
// alguno de los dos vertices vecinos, se engancha exactamente ahi (en cada eje
// por separado, contra cualquiera de los dos vecinos). Sin este enganche,
// alinear una esquina "a ojo" con el resto del trazado casi nunca cae exacto:
// el resultado queda con esa inclinacion residual como un codo extra en vez
// de fundirse en un solo tramo recto.
[[nodiscard]] std::vector<QPointF> moveWireCorner(const std::vector<QPointF>& points, std::size_t cornerIndex,
                                                  QPointF cursorPos);

// Waypoints (sin extremos) para los dos tramos que resultan de partir un
// cable en `splitPoint` -- ver SplitWireCommand, que crea un punto de union
// ahi cuando otro cable se deriva sobre el cuerpo de este.
struct WireSplit {
    std::vector<QPointF> before; // del extremo A hasta el nuevo punto de union
    std::vector<QPointF> after;  // del nuevo punto de union hasta el extremo B
};

// `polyline` es el trazado YA RESUELTO del cable (ver wireVertices()/
// renderedPolyline()), es decir [extremoA, esquinas..., extremoB]. `splitPoint`
// tipicamente viene de proyectar el punto de corte sobre ese mismo trazado
// (WireItem::nearestPointOnPath()), asi que cae siempre sobre uno de sus
// segmentos. El resultado son los waypoints que hay que asignarle a cada
// mitad para que, juntas, dibujen exactamente el mismo trazado que tenia el
// cable original -- partirlo no debe deformar el trazado ya acomodado.
[[nodiscard]] WireSplit splitWireWaypoints(const std::vector<QPointF>& polyline, QPointF splitPoint);

} // namespace digitalforge::editor
