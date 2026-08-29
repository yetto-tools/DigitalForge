cmake_minimum_required(VERSION 3.24)

project(DigitalForge
    VERSION 0.1.5
    DESCRIPTION "Multiplatform digital logic editor and simulator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

option(DIGITALFORGE_BUILD_GUI "Build the Qt6 Widgets desktop application" OFF)
option(DIGITALFORGE_BUILD_TESTS "Build the Catch2 unit test suite" ON)
option(DIGITALFORGE_BUILD_BENCHMARK "Build the headless benchmark executable" ON)
option(DIGITALFORGE_ENABLE_SANITIZERS "Enable ASan/UBSan (GCC/Clang, non-Windows)" OFF)

add_library(digitalforge_warnings INTERFACE)
if(MSVC)
    target_compile_options(digitalforge_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(digitalforge_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_library(digitalforge_sanitizers INTERFACE)
if(DIGITALFORGE_ENABLE_SANITIZERS AND NOT WIN32 AND NOT MSVC)
    target_compile_options(digitalforge_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(digitalforge_sanitizers INTERFACE -fsanitize=address,undefined)
endif()

add_library(digitalforge_core STATIC
    src/core/Simulator.cpp
    src/core/Sha256.cpp
)
target_include_directories(digitalforge_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(digitalforge_core PUBLIC digitalforge_warnings digitalforge_sanitizers)

include(${CMAKE_SOURCE_DIR}/cmake/Json.cmake)

add_library(digitalforge_components STATIC
    src/components/ComponentInstance.cpp
    src/components/ComponentRegistry.cpp
    src/components/BasicComponentLibrary.cpp
    src/components/ComponentFingerprints.cpp
    src/components/JsonComponentLoader.cpp
)
target_link_libraries(digitalforge_components PUBLIC digitalforge_core nlohmann_json::nlohmann_json)

if(DIGITALFORGE_BUILD_TESTS OR DIGITALFORGE_BUILD_BENCHMARK)
    enable_testing()
endif()

if(DIGITALFORGE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(DIGITALFORGE_BUILD_BENCHMARK)
    add_subdirectory(benchmark)
endif()

if(DIGITALFORGE_BUILD_GUI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
    add_subdirectory(src/app)

    # CircuitDocument/ProjectSerializer/Project don't use any QWidget/QGraphics
    # class, so their save/load round-trip is tested independently of the
    # QtWidgets desktop app - no QApplication/display needed, only a
    # QCoreApplication (see tests_qt's own main()). They still link
    # Qt6::Widgets because Project pulls in QUndoGroup, which in this Qt
    # build lives in the Widgets module rather than Core.
    if(DIGITALFORGE_BUILD_TESTS)
        add_subdirectory(tests_qt)
    endif()
endif()
