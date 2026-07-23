include(${CMAKE_SOURCE_DIR}/cmake/Catch2.cmake)

add_executable(digitalforge_tests
    test_logic_values.cpp
    test_basic_gates.cpp
    test_event_queue.cpp
    test_simulator.cpp
    test_components.cpp
    test_fingerprints.cpp
    test_json_component_loader.cpp
)
target_link_libraries(digitalforge_tests PRIVATE digitalforge_core digitalforge_components Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(digitalforge_tests)
