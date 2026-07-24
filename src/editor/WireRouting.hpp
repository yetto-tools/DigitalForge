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
[[nodiscard]] std::vector<QPointF> moveWireCorner(const std::vector<QPointF>& points, std::size_t cornerIndex,
                                                  QPointF cursorPos);

} // namespace digitalforge::editor
