set(DIGITALFORGE_APP_PLATFORM_SOURCES "")
if(WIN32)
    list(APPEND DIGITALFORGE_APP_PLATFORM_SOURCES ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()

add_executable(DigitalForge
    main.cpp
    MainWindow.cpp
    AppSettings.cpp
    ${CMAKE_SOURCE_DIR}/resources/app.qrc
    ${DIGITALFORGE_APP_PLATFORM_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/Project.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/UndoCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/PinItem.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/ComponentItem.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/WireItem.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/JunctionItem.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/SelectionTool.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/WireTool.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/PlacementTool.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitScene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitView.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/TruthTable.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/WaveformRecorder.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AutoHideStrip.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ComponentPalette.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProjectTree.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/PropertyInspector.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SimulationToolbar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TruthTablePanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/WaveformPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/IconFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/PreferencesDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MiniMapView.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ProjectManifestSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ProjectSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/LogisimImporter.cpp
)

target_link_libraries(DigitalForge PRIVATE digitalforge_components Qt6::Widgets)
