#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <vector>

// Ruteo ortogonal de cables, factorizado fuera de WireItem para que tanto el
// item de cable ya trazado como la vista previa interactiva de WireTool (y la
// futura deteccion geometrica) construyan exactamente la misma forma. Antes
// estas funciones eran locales al .cpp de WireItem y la vista previa dibujaba
// una diagonal recta que no coincidia con el cable final.
namespace digitalforge::editor {

// Umbral (en pixeles de escena) por debajo del cual dos puntos se tratan como
// si ya compartieran x/y, para no dibujar un codo residual visible cuando dos
// pines estan casi -pero no exactamente- alineados.
inline constexpr qreal kWireAlignTolerance = 3.0;

// True si el tramo vertical en `x` (recorriendo y entre yMin..yMax) cruza el
// rectangulo `rect`.
[[nodiscard]] bool verticalSegmentCrosses(qreal x, qreal yMin, qreal yMax, const QRectF& rect);

// Elige la x del tramo vertical del codo entre `from` y `to`: el punto medio
// natural si esta libre de `obstacles`, o el candidato libre mas cercano.
[[nodiscard]] qreal chooseClearMidX(QPointF from, QPointF to, const std::vector<QRectF>& obstacles);

// Agrega a `path` (que ya debe empezar en `from`) los quiebres en angulo recto
// hasta `to`. Se degenera a una linea recta cuando ya comparten x o y.
void appendElbow(QPainterPath& path, QPointF from, QPointF to, const std::vector<QRectF>& obstacles);

// Los vertices que appendElbow() realmente dibujaria para el tramo `from`->`to`
// (incluye `to`), para que el hit-testing razone sobre la ruta dibujada.
void appendElbowVertices(std::vector<QPointF>& vertices, QPointF from, QPointF to,
                         const std::vector<QRectF>& obstacles);

// Construye la polilinea ortogonal completa que pasa por `points` (>=2), con
// codos que esquivan `obstacles`. Devuelve un path vacio si hay menos de 2.
[[nodiscard]] QPainterPath buildOrthogonalPath(const std::vector<QPointF>& points,
                                               const std::vector<QRectF>& obstacles);

// Todos los vertices por los que pasa la ruta ortogonal que une `points`
// (>=2), incluidos los codos que appendElbowVertices() inserta para esquivar
// `obstacles`, ya simplificados con simplifyOrthogonalPolyline(). Es la version
// en vector de buildOrthogonalPath(): el editor la usa para saber donde estan
// las esquinas VISIBLES del cable (aunque no sean waypoints guardados) y
// poder agarrarlas.
[[nodiscard]] std::vector<QPointF> orthogonalVertices(const std::vector<QPointF>& points,
                                                      const std::vector<QRectF>& obstacles);

// Quita de una polilinea los vertices redundantes: duplicados consecutivos
// (dentro de kWireAlignTolerance) y puntos colineales (un vertice que cae
// sobre la recta entre su vecino anterior y el siguiente, es decir, no es una
// esquina real). Deja la minima secuencia de esquinas reales, de modo que
// arrastrar una esquina mueva un vertice significativo y no un punto
// degenerado.
[[nodiscard]] std::vector<QPointF> simplifyOrthogonalPolyline(const std::vector<QPointF>& vertices);

// Dibuja un cable YA EDITADO por el usuario, cuya lista de puntos (extremos +
// waypoints) ya describe la forma deseada: cada tramo se dibuja recto si sus
// extremos comparten x o y, y con un unico codo en L (horizontal primero) si
// no -- sin el codo en Z centrado ni el desvio por obstaculos de
// buildOrthogonalPath(). Asi, mover una esquina mueve exactamente esa esquina,
// sin que el trazado "salte" (el defecto reportado). Los cables sin waypoints
// siguen usando buildOrthogonalPath() (auto-ruteo con esquive).
[[nodiscard]] QPainterPath buildEditedWirePath(const std::vector<QPointF>& points);

} // namespace digitalforge::editor
