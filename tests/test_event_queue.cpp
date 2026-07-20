#include <catch2/catch_test_macros.hpp>

#include "core/EventQueue.hpp"

using digitalforge::core::EventQueue;
using digitalforge::core::LogicValue;
using digitalforge::core::SimulationEvent;

TEST_CASE("EventQueue starts empty", "[event-queue]") {
    EventQueue queue;
    CHECK(queue.empty());
    CHECK(queue.size() == 0);
}

TEST_CASE("EventQueue orders events by timestamp", "[event-queue][order]") {
    EventQueue queue;
    queue.push(SimulationEvent{.timestamp = 5, .componentId = 1, .inputPin = 0, .value = LogicValue::One});
    queue.push(SimulationEvent{.timestamp = 1, .componentId = 2, .inputPin = 0, .value = LogicValue::Zero});
    queue.push(SimulationEvent{.timestamp = 3, .componentId = 3, .inputPin = 0, .value = LogicValue::One});

    CHECK(queue.pop().componentId == 2);
    CHECK(queue.pop().componentId == 3);
    CHECK(queue.pop().componentId == 1);
    CHECK(queue.empty());
}

TEST_CASE("EventQueue preserves FIFO order among equal timestamps", "[event-queue][order]") {
    EventQueue queue;
    queue.push(SimulationEvent{.timestamp = 10, .componentId = 100, .inputPin = 0, .value = LogicValue::One});
    queue.push(SimulationEvent{.timestamp = 10, .componentId = 101, .inputPin = 0, .value = LogicValue::One});
    queue.push(SimulationEvent{.timestamp = 10, .componentId = 102, .inputPin = 0, .value = LogicValue::One});

    CHECK(queue.pop().componentId == 100);
    CHECK(queue.pop().componentId == 101);
    CHECK(queue.pop().componentId == 102);
}

TEST_CASE("EventQueue clear resets state", "[event-queue]") {
    EventQueue queue;
    queue.push(SimulationEvent{.timestamp = 0, .componentId = 0, .inputPin = 0, .value = LogicValue::Zero});
    queue.clear();
    CHECK(queue.empty());
    CHECK(queue.size() == 0);
}
