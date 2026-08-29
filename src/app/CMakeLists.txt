set(DIGITALFORGE_APP_PLATFORM_SOURCES "")
if(WIN32)
    list(APPEND DIGITALFORGE_APP_PLATFORM_SOURCES ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()

add_executable(DigitalForge WIN32
    main.cpp
    MainWindow.cpp
    AppSettings.cpp
    DefaultLayout.cpp
    UpdateChecker.cpp
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
    ${CMAKE_SOURCE_DIR}/src/editor/WireRouting.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/PlacementTool.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitScene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitView.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitAutoLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/CircuitLayoutGraph.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/TruthTable.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/KarnaughMap.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/KarnaughDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/TruthTableDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/FlipFlopExcitation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/ExcitationTableDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/WaveformRecorder.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AutoHideStrip.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ComponentPalette.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ProjectTree.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/PropertyInspector.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SimulationToolbar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TruthTablePanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/KarnaughMapView.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/TruthTableView.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ExcitationTableView.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/WaveformPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DiagnosticsPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/IconFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/PreferencesDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MiniMapView.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ZoomControl.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ProjectManifestSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ProjectSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/LockFile.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/LogisimImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/KarnaughSynthesizer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/KarnaughSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/TruthTableSerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ExcitationTableSerializer.cpp
)

target_link_libraries(DigitalForge PRIVATE digitalforge_components Qt6::Widgets Qt6::Network)

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)

    set(DIGITALFORGE_APP_ID "io.github.yetto_tools.DigitalForge")

    install(TARGETS DigitalForge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/${DIGITALFORGE_APP_ID}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/${DIGITALFORGE_APP_ID}.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

    foreach(DIGITALFORGE_ICON_SIZE 16 32 48 128 256)
        install(FILES ${CMAKE_SOURCE_DIR}/resources/app_${DIGITALFORGE_ICON_SIZE}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${DIGITALFORGE_ICON_SIZE}x${DIGITALFORGE_ICON_SIZE}/apps
            RENAME ${DIGITALFORGE_APP_ID}.png)
    endforeach()
endif()
