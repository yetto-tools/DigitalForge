// Ejercita formats::importLogisimCircFile con circuitos .circ chicos
// embebidos como strings (escritos a un archivo temporal, igual que el
// resto de los tests de formats/). No necesita su propio main(): comparte
// el CATCH_CONFIG_RUNNER definido en test_project_serializer.cpp (ver
// tests_qt/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include "editor/CircuitDocument.hpp"
#include "formats/LogisimImporter.hpp"

using digitalforge::core::LogicValue;
using digitalforge::editor::CircuitDocument;
namespace formats = digitalforge::formats;

namespace {

constexpr const char* kTempPath = "test_logisim_importer_temp.circ";

void writeCircFile(const char* contents) {
    std::ofstream file(kTempPath, std::ios::trunc);
    file << contents;
}

// Una sola compuerta NAND de 2 entradas: A, B -> NAND -> Y. Geometria de
// pines verificada contra un archivo de Logisim real (ver comentario de
// cabecera de LogisimImporter.cpp): compuerta de 2 entradas, tamano
// "Narrow" (size=30), orientacion este -> entrada0 en (loc.x-40,loc.y-10),
// entrada1 en (loc.x-40,loc.y+10), salida en loc.
constexpr const char* kSingleNandCirc = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<project source="4.1.0" version="1.0">
<lib desc="#Wiring" name="0"/>
<lib desc="#Gates" name="1"/>
<main name="main"/>
<circuit name="main">
<wire from="(100,100)" to="(160,90)"/>
<wire from="(100,150)" to="(160,110)"/>
<wire from="(200,100)" to="(220,100)"/>
<comp lib="0" loc="(100,100)" name="Pin">
<a name="tristate" val="false"/>
<a name="label" val="A"/>
</comp>
<comp lib="0" loc="(100,150)" name="Pin">
<a name="tristate" val="false"/>
<a name="label" val="B"/>
</comp>
<comp lib="1" loc="(200,100)" name="NAND Gate">
<a name="size" val="30"/>
<a name="inputs" val="2"/>
</comp>
<comp lib="0" loc="(220,100)" name="Pin">
<a name="facing" val="west"/>
<a name="type" val="output"/>
<a name="label" val="Y"/>
</comp>
</circuit>
</project>
)XML";

// Una entrada A se deriva (branch) hacia dos compuertas NAND distintas --
// dos <wire> separados que arrancan del mismo punto (100,100), lo que en
// DigitalForge debe convertirse en un unico Junction con 3 cables en
// estrella (A, NAND1.in0, NAND2.in0), no en dos cables pin-a-pin sueltos.
constexpr const char* kBranchingCirc = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<project source="4.1.0" version="1.0">
<lib desc="#Wiring" name="0"/>
<lib desc="#Gates" name="1"/>
<main name="main"/>
<circuit name="main">
<wire from="(100,100)" to="(160,80)"/>
<wire from="(100,100)" to="(160,150)"/>
<wire from="(100,200)" to="(160,100)"/>
<wire from="(100,250)" to="(160,170)"/>
<wire from="(200,90)" to="(220,90)"/>
<wire from="(200,160)" to="(220,160)"/>
<comp lib="0" loc="(100,100)" name="Pin">
<a name="tristate" val="false"/>
<a name="label" val="A"/>
</comp>
<comp lib="0" loc="(100,200)" name="Pin">
<a name="tristate" val="false"/>
<a name="label" val="B"/>
</comp>
<comp lib="0" loc="(100,250)" name="Pin">
<a name="tristate" val="false"/>
<a name="label" val="C"/>
</comp>
<comp lib="1" loc="(200,90)" name="NAND Gate">
<a name="size" val="30"/>
<a name="inputs" val="2"/>
</comp>
<comp lib="1" loc="(200,160)" name="NAND Gate">
<a name="size" val="30"/>
<a name="inputs" val="2"/>
</comp>
<comp lib="0" loc="(220,90)" name="Pin">
<a name="facing" val="west"/>
<a name="type" val="output"/>
<a name="label" val="Y1"/>
</comp>
<comp lib="0" loc="(220,160)" name="Pin">
<a name="facing" val="west"/>
<a name="type" val="output"/>
<a name="label" val="Y2"/>
</comp>
</circuit>
</project>
)XML";

// Un multiplexor de la biblioteca "#Plexers" -- todavia sin equivalente en
// DigitalForge (ver plan de fases 1-2 en curso).
constexpr const char* kUnsupportedComponentCirc = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<project source="4.1.0" version="1.0">
<lib desc="#Wiring" name="0"/>
<lib desc="#Plexers" name="2"/>
<main name="main"/>
<circuit name="main">
<comp lib="2" loc="(100,100)" name="Multiplexer">
<a name="select" val="1"/>
</comp>
</circuit>
</project>
)XML";

} // namespace

TEST_CASE("A single NAND gate imports with correct connectivity", "[logisim][import]") {
    writeCircFile(kSingleNandCirc);
    CircuitDocument document;
    formats::importLogisimCircFile(document, kTempPath);
    std::remove(kTempPath);

    REQUIRE(document.componentIds().size() == 4); // A, B, NAND, Y
    CHECK(document.wireIds().size() == 3);
    CHECK(document.junctionIds().empty());

    // A y B arrancan en Z -> Zero (positiveLogicPolarity por defecto);
    // NAND(0,0) = 1.
    uint32_t outputId = 0;
    bool found = false;
    for (const uint32_t id : document.componentIds()) {
        if (document.component(id)->typeId() == "wiring.output") {
            outputId = id;
            found = true;
        }
    }
    REQUIRE(found);
    CHECK(document.pinValue(outputId, 0) == LogicValue::One);
}

TEST_CASE("A wire branching to two gates imports as a single Junction, not two loose wires",
          "[logisim][import][junction]") {
    writeCircFile(kBranchingCirc);
    CircuitDocument document;
    formats::importLogisimCircFile(document, kTempPath);
    std::remove(kTempPath);

    REQUIRE(document.junctionIds().size() == 1);

    uint32_t y1 = 0, y2 = 0;
    int outputsFound = 0;
    for (const uint32_t id : document.componentIds()) {
        if (document.component(id)->typeId() == "wiring.output") {
            if (outputsFound == 0) {
                y1 = id;
            } else {
                y2 = id;
            }
            ++outputsFound;
        }
    }
    REQUIRE(outputsFound == 2);
    // A/B/C en Zero por defecto -> ambas NAND(0,0) = 1.
    CHECK(document.pinValue(y1, 0) == LogicValue::One);
    CHECK(document.pinValue(y2, 0) == LogicValue::One);
}

TEST_CASE("Importing a circuit with an unsupported Logisim component type fails clearly",
          "[logisim][import][validation]") {
    writeCircFile(kUnsupportedComponentCirc);
    CircuitDocument document;
    CHECK_THROWS_AS(formats::importLogisimCircFile(document, kTempPath), std::invalid_argument);
    std::remove(kTempPath);
}

TEST_CASE("importLogisimCircFile throws for a missing file", "[logisim][import][validation]") {
    CircuitDocument document;
    CHECK_THROWS_AS(formats::importLogisimCircFile(document, "this_file_does_not_exist.circ"), std::runtime_error);
}
