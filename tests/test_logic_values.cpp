#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/LogicValue.hpp"

using digitalforge::core::LogicValue;
using digitalforge::core::logicAnd2;
using digitalforge::core::logicNand2;
using digitalforge::core::logicNor2;
using digitalforge::core::logicNot;
using digitalforge::core::logicOr2;
using digitalforge::core::logicXnor2;
using digitalforge::core::logicXor2;
using digitalforge::core::resolveNetValue;

TEST_CASE("NOT truth table over all five values", "[logic]") {
    CHECK(logicNot(LogicValue::Zero) == LogicValue::One);
    CHECK(logicNot(LogicValue::One) == LogicValue::Zero);
    CHECK(logicNot(LogicValue::Unknown) == LogicValue::Unknown);
    CHECK(logicNot(LogicValue::HighImpedance) == LogicValue::Unknown);
    CHECK(logicNot(LogicValue::Error) == LogicValue::Error);
}

TEST_CASE("AND truth table (0/1)", "[logic]") {
    CHECK(logicAnd2(LogicValue::Zero, LogicValue::Zero) == LogicValue::Zero);
    CHECK(logicAnd2(LogicValue::Zero, LogicValue::One) == LogicValue::Zero);
    CHECK(logicAnd2(LogicValue::One, LogicValue::Zero) == LogicValue::Zero);
    CHECK(logicAnd2(LogicValue::One, LogicValue::One) == LogicValue::One);
}

TEST_CASE("OR truth table (0/1)", "[logic]") {
    CHECK(logicOr2(LogicValue::Zero, LogicValue::Zero) == LogicValue::Zero);
    CHECK(logicOr2(LogicValue::Zero, LogicValue::One) == LogicValue::One);
    CHECK(logicOr2(LogicValue::One, LogicValue::Zero) == LogicValue::One);
    CHECK(logicOr2(LogicValue::One, LogicValue::One) == LogicValue::One);
}

TEST_CASE("NAND and NOR are the negation of AND/OR", "[logic]") {
    CHECK(logicNand2(LogicValue::One, LogicValue::One) == LogicValue::Zero);
    CHECK(logicNand2(LogicValue::Zero, LogicValue::One) == LogicValue::One);
    CHECK(logicNor2(LogicValue::Zero, LogicValue::Zero) == LogicValue::One);
    CHECK(logicNor2(LogicValue::One, LogicValue::Zero) == LogicValue::Zero);
}

TEST_CASE("XOR/XNOR truth table (0/1)", "[logic]") {
    CHECK(logicXor2(LogicValue::Zero, LogicValue::Zero) == LogicValue::Zero);
    CHECK(logicXor2(LogicValue::Zero, LogicValue::One) == LogicValue::One);
    CHECK(logicXor2(LogicValue::One, LogicValue::One) == LogicValue::Zero);
    CHECK(logicXnor2(LogicValue::One, LogicValue::One) == LogicValue::One);
    CHECK(logicXnor2(LogicValue::Zero, LogicValue::One) == LogicValue::Zero);
}

TEST_CASE("Zero dominates AND regardless of the other operand", "[logic][unknown]") {
    CHECK(logicAnd2(LogicValue::Zero, LogicValue::Unknown) == LogicValue::Zero);
    CHECK(logicAnd2(LogicValue::Zero, LogicValue::HighImpedance) == LogicValue::Zero);
    CHECK(logicAnd2(LogicValue::Zero, LogicValue::Error) == LogicValue::Zero);
}

TEST_CASE("One dominates OR regardless of the other operand", "[logic][unknown]") {
    CHECK(logicOr2(LogicValue::One, LogicValue::Unknown) == LogicValue::One);
    CHECK(logicOr2(LogicValue::One, LogicValue::HighImpedance) == LogicValue::One);
    CHECK(logicOr2(LogicValue::One, LogicValue::Error) == LogicValue::One);
}

TEST_CASE("Unknown and HighImpedance propagate through non-dominated AND/OR", "[logic][unknown]") {
    CHECK(logicAnd2(LogicValue::One, LogicValue::Unknown) == LogicValue::Unknown);
    CHECK(logicAnd2(LogicValue::One, LogicValue::HighImpedance) == LogicValue::Unknown);
    CHECK(logicOr2(LogicValue::Zero, LogicValue::Unknown) == LogicValue::Unknown);
    CHECK(logicOr2(LogicValue::Zero, LogicValue::HighImpedance) == LogicValue::Unknown);
}

TEST_CASE("Error propagates unless dominated", "[logic][error]") {
    CHECK(logicAnd2(LogicValue::One, LogicValue::Error) == LogicValue::Error);
    CHECK(logicOr2(LogicValue::Zero, LogicValue::Error) == LogicValue::Error);
    CHECK(logicXor2(LogicValue::Zero, LogicValue::Error) == LogicValue::Error);
    CHECK(logicNot(LogicValue::Error) == LogicValue::Error);
}

TEST_CASE("resolveNetValue: undriven net floats", "[net]") {
    std::vector<LogicValue> drivers{};
    CHECK(resolveNetValue(drivers) == LogicValue::HighImpedance);

    std::vector<LogicValue> allZ{LogicValue::HighImpedance, LogicValue::HighImpedance};
    CHECK(resolveNetValue(allZ) == LogicValue::HighImpedance);
}

TEST_CASE("resolveNetValue: single active driver wins", "[net]") {
    std::vector<LogicValue> drivers{LogicValue::HighImpedance, LogicValue::One, LogicValue::HighImpedance};
    CHECK(resolveNetValue(drivers) == LogicValue::One);
}

TEST_CASE("resolveNetValue: agreeing drivers do not conflict", "[net]") {
    std::vector<LogicValue> drivers{LogicValue::Zero, LogicValue::Zero};
    CHECK(resolveNetValue(drivers) == LogicValue::Zero);
}

TEST_CASE("resolveNetValue: disagreeing drivers produce Error", "[net][conflict]") {
    std::vector<LogicValue> drivers{LogicValue::Zero, LogicValue::One};
    CHECK(resolveNetValue(drivers) == LogicValue::Error);
}
