// Ejercita computeCircuitLayers() de forma headless (sin CircuitScene/Qt
// Widgets, ver el comentario de clase de CircuitLayoutGraph.hpp). No
// necesita su propio main(): comparte el CATCH_CONFIG_RUNNER definido en
// test_project_serializer.cpp (ver tests_qt/CMakeLists.txt).

#include <QPointF>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "editor/CircuitDocument.hpp"
#include "editor/CircuitLayoutGraph.hpp"

using digitalforge::editor::CircuitDocument;
using digitalforge::editor::computeCircuitLayers;
using digitalforge::editor::PinRef;

TEST_CASE("computeCircuitLayers assigns increasing layers along a simple chain",
          "[editor][circuitLayoutGraph]") {
    CircuitDocument doc;
    const uint32_t input = doc.addComponent("wiring.input");
    const uint32_t not0 = doc.addComponent("gates.not");
    const uint32_t not1 = doc.addComponent("gates.not");
    const uint32_t led = doc.addComponent("io.led");
    doc.addWire(PinRef{input, 0}, PinRef{not0, 0});
    doc.addWire(PinRef{not0, 1}, PinRef{not1, 0});
    doc.addWire(PinRef{not1, 1}, PinRef{led, 0});

    const std::map<uint32_t, int> layers = computeCircuitLayers(doc, std::nullopt);
    CHECK(layers.at(input) == 0);
    CHECK(layers.at(not0) == 1);
    CHECK(layers.at(not1) == 2);
    CHECK(layers.at(led) == 3);
}

TEST_CASE("computeCircuitLayers breaks a 2-node feedback loop instead of looping forever",
          "[editor][circuitLayoutGraph]") {
    // A.Y -> B.A y B.Y -> A.A: un lazo de realimentacion como el de un latch
    // armado a mano con compuertas cruzadas. El DFS blanco/gris/negro debe
    // descartar una de las dos aristas como back-edge para poder asignar
    // capas -- si no lo hiciera, el calculo de capas (Kahn) nunca terminaria
    // o dejaria nodos sin capa.
    CircuitDocument doc;
    const uint32_t a = doc.addComponent("gates.not");
    const uint32_t b = doc.addComponent("gates.not");
    doc.addWire(PinRef{a, 1}, PinRef{b, 0});
    doc.addWire(PinRef{b, 1}, PinRef{a, 0});

    const std::map<uint32_t, int> layers = computeCircuitLayers(doc, std::nullopt);
    REQUIRE(layers.size() == 2);
    // El componente creado primero (menor id) es el que el DFS visita
    // primero, asi que su arista de retorno (b -> a) es la que se descarta:
    // a queda sin predecesores (capa 0) y b depende de a (capa 1).
    CHECK(layers.at(a) == 0);
    CHECK(layers.at(b) == 1);
}

TEST_CASE("computeCircuitLayers handles a long series chain without overflowing the stack",
          "[editor][circuitLayoutGraph]") {
    // Regresion para un bug donde el DFS de ruptura de ciclos recursaba un
    // stack frame nativo por salto (std::function recursivo); una cadena de
    // dependencia larga (plausible via importacion o generacion automatica)
    // arriesgaba un desbordamiento de pila. Ahora es iterativo (pila
    // explicita), asi que el tamano de la cadena no importa.
    constexpr int kChainLength = 6000;
    CircuitDocument doc;
    std::vector<uint32_t> nots;
    nots.reserve(kChainLength);
    for (int i = 0; i < kChainLength; ++i) {
        nots.push_back(doc.addComponent("gates.not"));
    }
    for (int i = 0; i + 1 < kChainLength; ++i) {
        doc.addWire(PinRef{nots[static_cast<std::size_t>(i)], 1}, PinRef{nots[static_cast<std::size_t>(i + 1)], 0});
    }

    const std::map<uint32_t, int> layers = computeCircuitLayers(doc, std::nullopt);
    REQUIRE(layers.size() == static_cast<std::size_t>(kChainLength));
    for (int i = 0; i < kChainLength; ++i) {
        CHECK(layers.at(nots[static_cast<std::size_t>(i)]) == i);
    }
}
