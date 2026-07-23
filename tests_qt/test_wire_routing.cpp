#include <catch2/catch_test_macros.hpp>

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "editor/WireRouting.hpp"

using digitalforge::editor::appendElbow;
using digitalforge::editor::appendElbowVertices;
using digitalforge::editor::buildOrthogonalPath;
using digitalforge::editor::chooseClearMidX;
using digitalforge::editor::kWireAlignTolerance;
using digitalforge::editor::verticalSegmentCrosses;

namespace {

// Los vertices por los que realmente pasa `path`, para poder afirmar sobre la
// forma trazada y no solo sobre su bounding box.
std::vector<QPointF> pathVertices(const QPainterPath& path) {
    std::vector<QPointF> vertices;
    vertices.reserve(static_cast<std::size_t>(path.elementCount()));
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        vertices.emplace_back(element.x, element.y);
    }
    return vertices;
}

// True si algun tramo VERTICAL de la polilinea cruza `rect`. Es exactamente
// la garantia que da chooseClearMidX (ver el comentario del ultimo test).
bool anyVerticalSegmentCrosses(const std::vector<QPointF>& vertices, const QRectF& rect) {
    for (std::size_t i = 0; i + 1 < vertices.size(); ++i) {
        const QPointF a = vertices[i];
        const QPointF b = vertices[i + 1];
        if (std::abs(a.x() - b.x()) > 0.001) {
            continue; // tramo horizontal
        }
        if (verticalSegmentCrosses(a.x(), std::min(a.y(), b.y()), std::max(a.y(), b.y()), rect)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("verticalSegmentCrosses covers the rectangle including its edges", "[editor][wireRouting]") {
    const QRectF rect(100.0, 0.0, 50.0, 100.0); // x 100..150, y 0..100

    CHECK(verticalSegmentCrosses(120.0, 10.0, 20.0, rect));
    // Un tramo que atraviesa el rectangulo de lado a lado tambien cruza.
    CHECK(verticalSegmentCrosses(120.0, -50.0, 200.0, rect));

    // Los bordes cuentan como cruce: un cable pegado al borde de un
    // componente se lee como si pasara por encima.
    CHECK(verticalSegmentCrosses(100.0, 10.0, 20.0, rect));
    CHECK(verticalSegmentCrosses(150.0, 10.0, 20.0, rect));

    CHECK_FALSE(verticalSegmentCrosses(99.0, 10.0, 20.0, rect));
    CHECK_FALSE(verticalSegmentCrosses(151.0, 10.0, 20.0, rect));
    // Dentro del rango de x pero completamente por encima / por debajo.
    CHECK_FALSE(verticalSegmentCrosses(120.0, -50.0, -10.0, rect));
    CHECK_FALSE(verticalSegmentCrosses(120.0, 110.0, 200.0, rect));
}

TEST_CASE("chooseClearMidX keeps the natural midpoint when nothing blocks it", "[editor][wireRouting]") {
    const QPointF from(0.0, 0.0);
    const QPointF to(400.0, 60.0);

    CHECK(chooseClearMidX(from, to, {}) == 200.0);
    // Un obstaculo que no toca el tramo vertical no desvia nada.
    CHECK(chooseClearMidX(from, to, {QRectF(300.0, 200.0, 40.0, 40.0)}) == 200.0);
}

TEST_CASE("chooseClearMidX steps aside onto a clear column", "[editor][wireRouting]") {
    const QPointF from(0.0, 0.0);
    const QPointF to(400.0, 60.0);
    // Tapa el punto medio natural (200) a la altura que recorre el cable.
    const QRectF blocker(180.0, -20.0, 40.0, 120.0); // x 180..220
    const std::vector<QRectF> obstacles{blocker};

    const qreal midX = chooseClearMidX(from, to, obstacles);
    CHECK(midX != 200.0);
    CHECK_FALSE(verticalSegmentCrosses(midX, 0.0, 60.0, blocker));

    // Se aparta en multiplos de 16 (2x la grilla) y prueba primero hacia la
    // derecha, asi que el primer hueco libre es 200 + 2*16.
    CHECK(midX == 232.0);
    CHECK(std::fmod(midX - 200.0, 16.0) == 0.0);
}

TEST_CASE("chooseClearMidX falls back to the natural midpoint when boxed in", "[editor][wireRouting]") {
    const QPointF from(0.0, 0.0);
    const QPointF to(400.0, 60.0);
    // Mas ancho que el rango que explora la busqueda (24 pasos de 16px a cada
    // lado): no queda ninguna columna libre.
    const std::vector<QRectF> obstacles{QRectF(-2000.0, -100.0, 4000.0, 300.0)};

    CHECK(chooseClearMidX(from, to, obstacles) == 200.0);
}

TEST_CASE("Nearly aligned pins are routed without a visible residual elbow", "[editor][wireRouting]") {
    // Dos pines a la misma altura salvo por menos de la tolerancia: el cable
    // debe leerse como una recta, no como un codo minusculo.
    const QPointF from(0.0, 100.0);
    const QPointF to(200.0, 100.0 + kWireAlignTolerance - 1.0);

    const QPainterPath path = buildOrthogonalPath({from, to}, {});
    const std::vector<QPointF> vertices = pathVertices(path);
    REQUIRE(vertices.size() == 3);
    CHECK(vertices[0] == from);
    // El tramo largo va derecho a la altura de origen, y solo al final salta
    // el residuo vertical sub-tolerancia.
    CHECK(vertices[1] == QPointF(to.x(), from.y()));
    CHECK(vertices[2] == to);

    // Lo mismo en vertical: casi alineados en x.
    const QPointF vFrom(50.0, 0.0);
    const QPointF vTo(50.0 + kWireAlignTolerance - 1.0, 180.0);
    const std::vector<QPointF> vVertices = pathVertices(buildOrthogonalPath({vFrom, vTo}, {}));
    REQUIRE(vVertices.size() == 3);
    CHECK(vVertices[1] == QPointF(vFrom.x(), vTo.y()));
    CHECK(vVertices[2] == vTo);
}

TEST_CASE("A general segment becomes a three-part orthogonal elbow", "[editor][wireRouting]") {
    const QPointF from(0.0, 0.0);
    const QPointF to(200.0, 80.0);

    const std::vector<QPointF> vertices = pathVertices(buildOrthogonalPath({from, to}, {}));
    REQUIRE(vertices.size() == 4);
    CHECK(vertices[0] == from);
    CHECK(vertices[1] == QPointF(100.0, 0.0));  // horizontal hasta el punto medio
    CHECK(vertices[2] == QPointF(100.0, 80.0)); // vertical
    CHECK(vertices[3] == to);                    // horizontal hasta el destino

    // Cada tramo es estrictamente horizontal o vertical: nunca una diagonal.
    for (std::size_t i = 0; i + 1 < vertices.size(); ++i) {
        const bool horizontal = std::abs(vertices[i].y() - vertices[i + 1].y()) < 0.001;
        const bool vertical = std::abs(vertices[i].x() - vertices[i + 1].x()) < 0.001;
        CHECK((horizontal || vertical));
    }
}

TEST_CASE("buildOrthogonalPath needs at least two points", "[editor][wireRouting]") {
    CHECK(buildOrthogonalPath({}, {}).isEmpty());
    CHECK(buildOrthogonalPath({QPointF(10.0, 10.0)}, {}).isEmpty());
    CHECK_FALSE(buildOrthogonalPath({QPointF(0.0, 0.0), QPointF(50.0, 50.0)}, {}).isEmpty());
}

TEST_CASE("buildOrthogonalPath chains every waypoint of a multi-segment wire", "[editor][wireRouting]") {
    const std::vector<QPointF> points{QPointF(0.0, 0.0), QPointF(100.0, 60.0), QPointF(240.0, 60.0)};
    const std::vector<QPointF> vertices = pathVertices(buildOrthogonalPath(points, {}));

    // Empieza en el primero y termina en el ultimo, pasando por el intermedio.
    REQUIRE(vertices.size() >= 4);
    CHECK(vertices.front() == points[0]);
    CHECK(vertices.back() == points[2]);
    const bool visitsWaypoint =
        std::any_of(vertices.begin(), vertices.end(), [&](const QPointF& v) { return v == points[1]; });
    CHECK(visitsWaypoint);
}

TEST_CASE("appendElbowVertices reports exactly what appendElbow draws", "[editor][wireRouting]") {
    // El hit-testing razona sobre estos vertices; si se apartaran de la ruta
    // dibujada, un cable seria seleccionable donde no se ve y viceversa.
    const std::vector<QRectF> obstacles{QRectF(180.0, -20.0, 40.0, 120.0)};
    const std::vector<std::pair<QPointF, QPointF>> cases{
        {QPointF(0.0, 0.0), QPointF(400.0, 60.0)},   // codo con esquive
        {QPointF(0.0, 0.0), QPointF(200.0, 1.0)},     // casi alineados en y
        {QPointF(0.0, 0.0), QPointF(1.0, 200.0)},     // casi alineados en x
        {QPointF(0.0, 0.0), QPointF(200.0, 80.0)},    // codo comun
    };

    for (const auto& [from, to] : cases) {
        QPainterPath path;
        path.moveTo(from);
        appendElbow(path, from, to, obstacles);

        std::vector<QPointF> vertices;
        appendElbowVertices(vertices, from, to, obstacles);

        // El path incluye el moveTo inicial; la lista de vertices no.
        const std::vector<QPointF> drawn = pathVertices(path);
        REQUIRE(drawn.size() == vertices.size() + 1);
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            CHECK(drawn[i + 1] == vertices[i]);
        }
    }
}

TEST_CASE("Routing clears the obstacle on its vertical run, not on the horizontal ones",
          "[editor][wireRouting]") {
    // Esto fija el alcance REAL del esquive: chooseClearMidX solo busca una
    // columna libre para el tramo vertical del codo. Los dos tramos
    // horizontales (a la altura de origen y de destino) no se comprueban
    // contra los obstaculos, asi que un componente que este en el camino a
    // esas alturas sigue quedando cruzado. Es una limitacion conocida del
    // algoritmo actual, no un descuido del test.
    const QPointF from(0.0, 0.0);
    const QPointF to(400.0, 60.0);
    const QRectF blocker(180.0, -20.0, 40.0, 120.0);
    const std::vector<QRectF> obstacles{blocker};

    const QPainterPath path = buildOrthogonalPath({from, to}, obstacles);
    const std::vector<QPointF> vertices = pathVertices(path);

    // Lo que si garantiza el algoritmo:
    CHECK_FALSE(anyVerticalSegmentCrosses(vertices, blocker));

    // Lo que NO garantiza: el tramo horizontal de salida cruza el obstaculo,
    // porque va a y=0 desde x=0 hasta la columna elegida (232), atravesando
    // la franja x 180..220 del rectangulo.
    CHECK(path.intersects(blocker));
}
