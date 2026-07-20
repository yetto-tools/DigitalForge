#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/Gate.hpp"
#include "core/GateType.hpp"
#include "core/LogicValue.hpp"

using digitalforge::core::evaluateCombinationalGate;
using digitalforge::core::GateType;
using digitalforge::core::LogicValue;

namespace {
LogicValue eval(GateType type, std::vector<LogicValue> inputs) {
    return evaluateCombinationalGate(type, inputs);
}
} // namespace

TEST_CASE("2-input AND full truth table", "[gates][and]") {
    CHECK(eval(GateType::And, {LogicValue::Zero, LogicValue::Zero}) == LogicValue::Zero);
    CHECK(eval(GateType::And, {LogicValue::Zero, LogicValue::One}) == LogicValue::Zero);
    CHECK(eval(GateType::And, {LogicValue::One, LogicValue::Zero}) == LogicValue::Zero);
    CHECK(eval(GateType::And, {LogicValue::One, LogicValue::One}) == LogicValue::One);
}

TEST_CASE("2-input OR full truth table", "[gates][or]") {
    CHECK(eval(GateType::Or, {LogicValue::Zero, LogicValue::Zero}) == LogicValue::Zero);
    CHECK(eval(GateType::Or, {LogicValue::Zero, LogicValue::One}) == LogicValue::One);
    CHECK(eval(GateType::Or, {LogicValue::One, LogicValue::Zero}) == LogicValue::One);
    CHECK(eval(GateType::Or, {LogicValue::One, LogicValue::One}) == LogicValue::One);
}

TEST_CASE("NOT and Buffer", "[gates][not][buffer]") {
    CHECK(eval(GateType::Not, {LogicValue::Zero}) == LogicValue::One);
    CHECK(eval(GateType::Not, {LogicValue::One}) == LogicValue::Zero);
    CHECK(eval(GateType::Buffer, {LogicValue::Zero}) == LogicValue::Zero);
    CHECK(eval(GateType::Buffer, {LogicValue::One}) == LogicValue::One);
    CHECK(eval(GateType::Buffer, {LogicValue::HighImpedance}) == LogicValue::Unknown);
}

TEST_CASE("3-input NAND (74LS10 cell)", "[gates][nand]") {
    CHECK(eval(GateType::Nand, {LogicValue::One, LogicValue::One, LogicValue::One}) == LogicValue::Zero);
    CHECK(eval(GateType::Nand, {LogicValue::One, LogicValue::Zero, LogicValue::One}) == LogicValue::One);
}

TEST_CASE("4-input NAND (74LS20 cell)", "[gates][nand]") {
    const std::vector<LogicValue> allOne(4, LogicValue::One);
    CHECK(eval(GateType::Nand, allOne) == LogicValue::Zero);
    std::vector<LogicValue> oneZero(4, LogicValue::One);
    oneZero[2] = LogicValue::Zero;
    CHECK(eval(GateType::Nand, oneZero) == LogicValue::One);
}

TEST_CASE("8-input NAND (74LS30 cell)", "[gates][nand]") {
    std::vector<LogicValue> allOne(8, LogicValue::One);
    CHECK(eval(GateType::Nand, allOne) == LogicValue::Zero);
    allOne[7] = LogicValue::Zero;
    CHECK(eval(GateType::Nand, allOne) == LogicValue::One);
}

TEST_CASE("NOR and XOR/XNOR", "[gates][nor][xor][xnor]") {
    CHECK(eval(GateType::Nor, {LogicValue::Zero, LogicValue::Zero}) == LogicValue::One);
    CHECK(eval(GateType::Nor, {LogicValue::One, LogicValue::Zero}) == LogicValue::Zero);
    CHECK(eval(GateType::Xor, {LogicValue::One, LogicValue::Zero}) == LogicValue::One);
    CHECK(eval(GateType::Xor, {LogicValue::One, LogicValue::One}) == LogicValue::Zero);
    CHECK(eval(GateType::Xnor, {LogicValue::One, LogicValue::One}) == LogicValue::One);
    CHECK(eval(GateType::Xnor, {LogicValue::One, LogicValue::Zero}) == LogicValue::Zero);
}

TEST_CASE("Unknown propagates through AND unless dominated by a Zero input", "[gates][unknown]") {
    CHECK(eval(GateType::And, {LogicValue::One, LogicValue::Unknown}) == LogicValue::Unknown);
    CHECK(eval(GateType::And, {LogicValue::Zero, LogicValue::Unknown}) == LogicValue::Zero);
}

TEST_CASE("HighImpedance at a gate input degrades to Unknown unless dominated", "[gates][highz]") {
    CHECK(eval(GateType::Or, {LogicValue::Zero, LogicValue::HighImpedance}) == LogicValue::Unknown);
    CHECK(eval(GateType::Or, {LogicValue::One, LogicValue::HighImpedance}) == LogicValue::One);
}

TEST_CASE("Error at a gate input propagates unless dominated", "[gates][error]") {
    CHECK(eval(GateType::And, {LogicValue::One, LogicValue::Error}) == LogicValue::Error);
    CHECK(eval(GateType::And, {LogicValue::Zero, LogicValue::Error}) == LogicValue::Zero);
}

TEST_CASE("isValidInputCount rejects wrong arities", "[gates][validation]") {
    using digitalforge::core::isValidInputCount;
    CHECK(isValidInputCount(GateType::Not, 1));
    CHECK_FALSE(isValidInputCount(GateType::Not, 2));
    CHECK(isValidInputCount(GateType::And, 2));
    CHECK(isValidInputCount(GateType::And, 8));
    CHECK_FALSE(isValidInputCount(GateType::And, 1));
    CHECK(isValidInputCount(GateType::InputPin, 0));
    CHECK_FALSE(isValidInputCount(GateType::InputPin, 1));
}
