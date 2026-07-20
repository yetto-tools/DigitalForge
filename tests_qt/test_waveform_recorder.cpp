// Ejercita editor::WaveformRecorder (Fase D) directamente sobre
// CircuitDocument, sin pasar por ui::WaveformPanel/WaveformCanvas (QWidgets -
// la suite corre sobre QCoreApplication, no QApplication, ver
// test_project_serializer.cpp). No necesita su propio main(): comparte el
// CATCH_CONFIG_RUNNER definido ahi.

#include <catch2/catch_test_macros.hpp>

#include "editor/CircuitDocument.hpp"
#include "editor/WaveformRecorder.hpp"

using digitalforge::components::PropertyValue;
using digitalforge::core::LogicValue;
using digitalforge::editor::CircuitDocument;
using digitalforge::editor::PinRef;
using digitalforge::editor::WaveformRecorder;
using digitalforge::editor::WireEndpoint;

TEST_CASE("addWatch captures the current value immediately as the first sample", "[waveform]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    WaveformRecorder recorder(&doc);

    REQUIRE(recorder.addWatch(PinRef{in0, 0}, "IN0"));
    REQUIRE(recorder.watches().size() == 1);
    REQUIRE(recorder.watches()[0].samples.size() == 1);
    CHECK(recorder.watches()[0].samples[0].value == LogicValue::One);
}

TEST_CASE("addWatch rejects a duplicate endpoint and refuses past kMaxWatchedNets", "[waveform]") {
    CircuitDocument doc;
    std::vector<uint32_t> inputs;
    for (std::size_t i = 0; i < WaveformRecorder::kMaxWatchedNets + 1; ++i) {
        inputs.push_back(doc.addComponent("wiring.input"));
    }
    WaveformRecorder recorder(&doc);

    for (std::size_t i = 0; i < WaveformRecorder::kMaxWatchedNets; ++i) {
        CHECK(recorder.addWatch(PinRef{inputs[i], 0}, QString("IN%1").arg(i)));
    }
    CHECK(recorder.watches().size() == WaveformRecorder::kMaxWatchedNets);

    // Ya lleno: ni una red nueva ni un duplicado de una ya vigilada entran.
    CHECK_FALSE(recorder.addWatch(PinRef{inputs.back(), 0}, "overflow"));
    CHECK_FALSE(recorder.addWatch(PinRef{inputs[0], 0}, "dup"));
    CHECK(recorder.watches().size() == WaveformRecorder::kMaxWatchedNets);
}

TEST_CASE("sample() only appends a new sample when the watched value actually changes", "[waveform]") {
    CircuitDocument doc;
    // initialValue explicito: el valor por defecto ("Z") se resuelve segun
    // positiveLogicPolarity() (por defecto positiva -> Zero), lo que haria
    // que el primer setInputValue(Zero) de abajo fuera un no-cambio.
    const uint32_t in0 = doc.addComponent("wiring.input", {{"initialValue", PropertyValue{std::string("1")}}});
    WaveformRecorder recorder(&doc);
    REQUIRE(recorder.addWatch(PinRef{in0, 0}, "IN0"));
    REQUIRE(recorder.watches()[0].samples.size() == 1);
    CHECK(recorder.watches()[0].samples[0].value == LogicValue::One);

    doc.setInputValue(in0, LogicValue::Zero); // dispara simulationStepped() -> sample()
    REQUIRE(recorder.watches()[0].samples.size() == 2);
    CHECK(recorder.watches()[0].samples.back().value == LogicValue::Zero);

    doc.setInputValue(in0, LogicValue::Zero); // mismo valor: no debe agregar una muestra nueva
    CHECK(recorder.watches()[0].samples.size() == 2);

    doc.setInputValue(in0, LogicValue::One);
    REQUIRE(recorder.watches()[0].samples.size() == 3);
    CHECK(recorder.watches()[0].samples.back().value == LogicValue::One);
}

TEST_CASE("simulationRebuilt() clears every watch's samples but keeps the watch list itself", "[waveform]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input");
    WaveformRecorder recorder(&doc);
    REQUIRE(recorder.addWatch(PinRef{in0, 0}, "IN0"));
    doc.setInputValue(in0, LogicValue::One);
    REQUIRE(recorder.watches()[0].samples.size() == 2);

    doc.rebuildSimulation(); // emite simulationRebuilt()
    REQUIRE(recorder.watches().size() == 1); // sigue vigilando el mismo endpoint
    CHECK(recorder.watches()[0].samples.empty());
}

TEST_CASE("removeWatch/clearWatches drop watched nets", "[waveform]") {
    CircuitDocument doc;
    const uint32_t in0 = doc.addComponent("wiring.input");
    const uint32_t in1 = doc.addComponent("wiring.input");
    WaveformRecorder recorder(&doc);
    REQUIRE(recorder.addWatch(PinRef{in0, 0}, "IN0"));
    REQUIRE(recorder.addWatch(PinRef{in1, 0}, "IN1"));

    recorder.removeWatch(0);
    REQUIRE(recorder.watches().size() == 1);
    CHECK(recorder.watches()[0].label == "IN1");

    recorder.clearWatches();
    CHECK(recorder.watches().empty());
}

TEST_CASE("setDocument() discards every watch from the previous document", "[waveform]") {
    CircuitDocument docA;
    const uint32_t in0 = docA.addComponent("wiring.input");
    WaveformRecorder recorder(&docA);
    REQUIRE(recorder.addWatch(PinRef{in0, 0}, "IN0"));

    CircuitDocument docB;
    recorder.setDocument(&docB);
    CHECK(recorder.watches().empty());
}
