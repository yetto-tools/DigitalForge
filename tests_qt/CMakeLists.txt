include(${CMAKE_SOURCE_DIR}/cmake/Catch2.cmake)

add_executable(digitalforge_qt_tests
    test_project_serializer.cpp
    test_project.cpp
    test_circuit_document.cpp
    test_logisim_importer.cpp
    test_subcircuit.cpp
    test_truth_table.cpp
    test_waveform_recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/UndoCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/Project.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/TruthTable.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/WaveformRecorder.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ProjectManifestSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ProjectSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/LogisimImporter.cpp
)
target_link_libraries(digitalforge_qt_tests PRIVATE digitalforge_components Qt6::Core Qt6::Widgets Catch2::Catch2)

include(Catch)
catch_discover_tests(digitalforge_qt_tests)

# SCRATCH investigation target (not part of the deliverable) - mimics
# MainWindow's documentAdded/activeDocumentChanged -> scenes_ wiring around
# Project::loadFromFile to see whether it throws only when a CircuitScene is
# listening, unlike the headless Project::loadFromFile test.
add_executable(scratch_repro
    scratch_repro.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/UndoCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/Project.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/PinItem.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/ComponentItem.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/WireItem.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/JunctionItem.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/SelectionTool.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/WireTool.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/PlacementTool.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitScene.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ProjectManifestSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ProjectSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/LogisimImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ComponentPalette.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProjectTree.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/PropertyInspector.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SimulationToolbar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/IconFactory.cpp
)
target_link_libraries(scratch_repro PRIVATE digitalforge_components Qt6::Widgets)
set_target_properties(scratch_repro PROPERTIES WIN32_EXECUTABLE FALSE)
