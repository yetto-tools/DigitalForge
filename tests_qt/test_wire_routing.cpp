#include <catch2/catch_test_macros.hpp>

#include <QPainterPath>
#include <QPointF>
#include <cmath>
#include <vector>

#include "editor/WireRouting.hpp"

using digitalforge::editor::buildWirePath;
using digitalforge::editor::kWireAlignTolerance;
using digitalforge::editor::moveWireCorner;
using digitalforge::editor::moveWireSegment;
using digitalforge::editor::simplifyOrthogonalPolyline;
using digitalforge::editor::wireVertices;

namespace {

std::vector<QPointF> pathVertices(const QPainterPath& path) {
    std::vector<QPointF> vertices;
    vertices.reserve(static_cast<std::size_t>(path.elementCount()));
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        vertices.emplace_back(element.x, element.y);
    }
    return vertices;
}

// Todo tramo de un cable debe ser estrictamente horizontal o vertical: una
// diagonal seria un error de trazado visible.
bool allSegmentsOrthogonal(const std::vector<QPointF>& vertices) {
    for (std::size_t i = 0; i + 1 < vertices.size(); ++i) {
        const bool horizontal = std::abs(vertices[i].y() - vertices[i + 1].y()) <= kWireAlignTolerance;
        const bool vertical = std::abs(vertices[i].x() - vertices[i + 1].x()) <= kWireAlignTolerance;
        if (!horizontal && !vertical) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("buildWirePath needs at least two points", "[editor][wireRouting]") {
    CHECK(buildWirePath({}).isEmpty());
    CHECK(buildWirePath({QPointF(10.0, 10.0)}).isEmpty());
    CHECK_FALSE(buildWirePath({QPointF(0.0, 0.0), QPointF(50.0, 50.0)}).isEmpty());
}

TEST_CASE("wireVertices describes exactly what buildWirePath draws", "[editor][wireRouting]") {
    // El hit-testing razona sobre wireVertices(); si se apartara de la ruta
    // dibujada, un cable seria agarrable donde no se ve y viceversa.
    const std::vector<std::vector<QPointF>> cases{
        {{0.0, 0.0}, {200.0, 80.0}},                              // codo comun
        {{0.0, 0.0}, {200.0, 1.0}},                               // casi alineados en y
        {{0.0, 0.0}, {1.0, 200.0}},                               // casi alineados en x
        {{0.0, 0.0}, {60.0, 0.0}, {60.0, 40.0}, {120.0, 40.0}},   // polilinea ya ortogonal
    };
    for (const std::vector<QPointF>& points : cases) {
        const std::vector<QPointF> vertices = wireVertices(points);
        const std::vector<QPointF> drawn = pathVertices(buildWirePath(points));
        REQUIRE(drawn.size() == vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            CHECK(drawn[i] == vertices[i]);
        }
        CHECK(allSegmentsOrthogonal(vertices));
    }
}

TEST_CASE("A diagonal pair becomes a single horizontal-first elbow", "[editor][wireRouting]") {
    // Una sola forma posible, siempre la misma: nada de codo en Z centrado que
    // pueda "saltar" de lado, ni desvios por obstaculos.
    const std::vector<QPointF> vertices = wireVertices({{0.0, 0.0}, {200.0, 80.0}});
    REQUIRE(vertices.size() == 3);
    CHECK(vertices[0] == QPointF(0.0, 0.0));
    CHECK(vertices[1] == QPointF(200.0, 0.0)); // horizontal primero
    CHECK(vertices[2] == QPointF(200.0, 80.0));
}

TEST_CASE("Nearly aligned points are routed as a straight run", "[editor][wireRouting]") {
    // Dos pines a la misma altura salvo por menos de la tolerancia: el cable
    // debe leerse como una recta, no como un codo minusculo. Sin un quiebre
    // intermedio que correr, el cable igual termina EXACTAMENTE en el segundo
    // pin (el resto queda como una inclinacion sub-tolerancia).
    const QPointF pin(200.0, 100.0 + kWireAlignTolerance - 1.0);
    const std::vector<QPointF> vertices = wireVertices({{0.0, 100.0}, pin});
    REQUIRE(vertices.size() == 2);
    CHECK(vertices[1] == pin);
}

TEST_CASE("A wire always ends centred on its endpoint", "[editor][wireRouting]") {
    // Los pines no siempre caen sobre la grilla (los hay derivados de una
    // division, p. ej. y = -16.888) mientras que los quiebres si. Enderezar el
    // ultimo tramo sobre la y del quiebre dejaba el cable corto por ese resto y
    // entraba descentrado al pin: se corre el quiebre a la coordenada del pin,
    // nunca al reves.
    const QPointF pin(120.0, -16.888888888888886);
    const std::vector<QPointF> vertices = wireVertices({{0.0, -60.0}, {56.0, -16.0}, pin});

    CHECK(vertices.back() == pin); // termina justo en el centro del pin
    // ...y el tramo final quedo perfectamente horizontal, no inclinado.
    REQUIRE(vertices.size() >= 2);
    CHECK(vertices[vertices.size() - 2].y() == pin.y());
    CHECK(allSegmentsOrthogonal(vertices));
}

TEST_CASE("Wire geometry is a pure function of its own points", "[editor][wireRouting]") {
    // La regresion de "rutas raras que cambian solas": la forma no depende de
    // nada externo, asi que mover otro componente no puede alterarla. Mismos
    // puntos -> mismo trazado, siempre.
    const std::vector<QPointF> points{{0.0, 0.0}, {120.0, 64.0}};
    const std::vector<QPointF> first = wireVertices(points);
    const std::vector<QPointF> second = wireVertices(points);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i] == second[i]);
    }
}

TEST_CASE("moveWireSegment slides an interior segment perpendicular to itself", "[editor][wireRouting]") {
    // p0 --- p1
    //        |
    //        p2 --- p3   : se arrastra el tramo vertical p1-p2 hacia x=80.
    const std::vector<QPointF> points{{0.0, 0.0}, {50.0, 0.0}, {50.0, 50.0}, {120.0, 50.0}};
    const std::vector<QPointF> moved = moveWireSegment(points, 1, QPointF(80.0, 25.0));

    REQUIRE(moved.size() == 4);
    CHECK(moved.front() == points.front()); // los extremos no se mueven
    CHECK(moved.back() == points.back());
    CHECK(moved[1] == QPointF(80.0, 0.0)); // solo cambio la x del tramo vertical
    CHECK(moved[2] == QPointF(80.0, 50.0));
    CHECK(allSegmentsOrthogonal(moved));
}

TEST_CASE("moveWireSegment keeps a horizontal segment on its own axis", "[editor][wireRouting]") {
    // Un tramo horizontal solo puede cambiar de y: el desplazamiento lateral
    // del cursor se ignora, que es lo que conserva los angulos rectos.
    const std::vector<QPointF> points{{0.0, 0.0}, {0.0, 40.0}, {90.0, 40.0}, {90.0, 90.0}};
    const std::vector<QPointF> moved = moveWireSegment(points, 1, QPointF(500.0, 70.0));

    REQUIRE(moved.size() == 4);
    CHECK(moved[1] == QPointF(0.0, 70.0));
    CHECK(moved[2] == QPointF(90.0, 70.0));
    CHECK(allSegmentsOrthogonal(moved));
}

TEST_CASE("moveWireSegment anchors the endpoints with an absorbing vertex", "[editor][wireRouting]") {
    // Arrastrar el tramo pegado a un pin no puede despegar el cable del pin:
    // el extremo se queda y aparece un vertice nuevo que absorbe el quiebre.
    const std::vector<QPointF> points{{0.0, 0.0}, {100.0, 0.0}, {100.0, 50.0}};
    const std::vector<QPointF> moved = moveWireSegment(points, 0, QPointF(50.0, 20.0));

    CHECK(moved.front() == QPointF(0.0, 0.0)); // el pin sigue en su lugar
    CHECK(moved.back() == QPointF(100.0, 50.0));
    REQUIRE(moved.size() == 4);
    CHECK(moved[1] == QPointF(0.0, 20.0)); // vertice de absorcion sobre el pin
    CHECK(moved[2] == QPointF(100.0, 20.0));
    CHECK(allSegmentsOrthogonal(moved));
}

TEST_CASE("moveWireSegment absorbs on both sides for a single-segment wire", "[editor][wireRouting]") {
    // Un cable recto entre dos pines: arrastrar su unico tramo debe generar
    // los dos vertices de absorcion, uno por extremo.
    const std::vector<QPointF> points{{0.0, 0.0}, {100.0, 0.0}};
    const std::vector<QPointF> moved = moveWireSegment(points, 0, QPointF(50.0, 40.0));

    REQUIRE(moved.size() == 4);
    CHECK(moved.front() == QPointF(0.0, 0.0));
    CHECK(moved[1] == QPointF(0.0, 40.0));
    CHECK(moved[2] == QPointF(100.0, 40.0));
    CHECK(moved.back() == QPointF(100.0, 0.0));
    CHECK(allSegmentsOrthogonal(moved));
}

TEST_CASE("moveWireSegment ignores an out-of-range segment", "[editor][wireRouting]") {
    const std::vector<QPointF> points{{0.0, 0.0}, {100.0, 0.0}};
    CHECK(moveWireSegment(points, 5, QPointF(10.0, 10.0)) == points);
    CHECK(moveWireSegment({QPointF(0.0, 0.0)}, 0, QPointF(10.0, 10.0)).size() == 1);
}

TEST_CASE("moveWireCorner moves the corner and leaves both arms orthogonal", "[editor][wireRouting]") {
    const std::vector<QPointF> points{{0.0, 0.0}, {50.0, 0.0}, {50.0, 50.0}, {120.0, 50.0}};
    const std::vector<QPointF> moved = moveWireCorner(points, 1, QPointF(70.0, 30.0));

    CHECK(moved.front() == points.front());
    CHECK(moved.back() == points.back());
    // El trazado resultante sigue siendo enteramente ortogonal: wireVertices()
    // resuelve los brazos, sin necesidad de deslizar los vecinos a mano.
    CHECK(allSegmentsOrthogonal(wireVertices(moved)));
}

TEST_CASE("moveWireCorner refuses to move an endpoint", "[editor][wireRouting]") {
    // Los extremos los ancla su pin/union: no son esquinas arrastrables.
    const std::vector<QPointF> points{{0.0, 0.0}, {50.0, 0.0}, {50.0, 50.0}};
    CHECK(moveWireCorner(points, 0, QPointF(99.0, 99.0)) == points);
    CHECK(moveWireCorner(points, 2, QPointF(99.0, 99.0)) == points);
}

TEST_CASE("simplifyOrthogonalPolyline drops duplicates and collinear points", "[editor][wireRouting]") {
    const std::vector<QPointF> raw{
        {0.0, 0.0}, {0.0, 0.0}, {50.0, 0.0}, {100.0, 0.0}, {100.0, 80.0},
    };
    const std::vector<QPointF> simplified = simplifyOrthogonalPolyline(raw);
    // El duplicado inicial y el punto colineal (50,0) desaparecen; queda la
    // esquina real en (100,0).
    REQUIRE(simplified.size() == 3);
    CHECK(simplified.front() == QPointF(0.0, 0.0));
    CHECK(simplified[1] == QPointF(100.0, 0.0));
    CHECK(simplified.back() == QPointF(100.0, 80.0));
}

TEST_CASE("simplifyOrthogonalPolyline always keeps both endpoints", "[editor][wireRouting]") {
    // Los extremos son los que anclan el cable a sus pines: no se pueden
    // descartar por mas colineales que queden.
    const std::vector<QPointF> straight{{0.0, 0.0}, {40.0, 0.0}, {80.0, 0.0}};
    const std::vector<QPointF> simplified = simplifyOrthogonalPolyline(straight);
    REQUIRE(simplified.size() == 2);
    CHECK(simplified.front() == QPointF(0.0, 0.0));
    CHECK(simplified.back() == QPointF(80.0, 0.0));
}
