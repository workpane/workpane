qt_add_library(
    WorkpaneTerminalObjects
    OBJECT
    TerminalFailure.cpp
    TerminalFailure.h
    TerminalPlugin.cpp
    TerminalPlugin.h
    TerminalSettingsStore.cpp
    TerminalSettingsStore.h
    TerminalTranslations.h
    TerminalView.cpp
    TerminalView.h
    TerminalWorkspaceRepository.cpp
    TerminalWorkspaceRepository.h
    domain/MainTab.h
    domain/SlotLayoutState.h
    domain/Workspace.h
    ui/SessionDrag.h
    ui/ShelfSessionChip.cpp
    ui/ShelfSessionChip.h
    ui/TerminalPane.cpp
    ui/TerminalPane.h
    ui/WorkspaceView.cpp
    ui/WorkspaceView.h
    workspace/LayoutManager.cpp
    workspace/LayoutManager.h
    workspace/WorkspaceManager.cpp
    workspace/WorkspaceManager.h
)
target_include_directories(
    WorkpaneTerminalObjects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(
    WorkpaneTerminalObjects
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        WorkpaneUi
        WorkpaneTerminalEngine
)
set_target_properties(
    WorkpaneTerminalObjects
    PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
)

workpane_set_warnings(WorkpaneTerminalObjects)
workpane_enable_coverage(WorkpaneTerminalObjects)
workpane_enable_sanitizers(WorkpaneTerminalObjects)

qt_add_library(WorkpaneTerminalPlugin SHARED)
target_link_libraries(WorkpaneTerminalPlugin PRIVATE WorkpaneTerminalObjects)
set_target_properties(WorkpaneTerminalPlugin PROPERTIES OUTPUT_NAME workpane-terminal)

workpane_set_warnings(WorkpaneTerminalPlugin)
workpane_enable_coverage(WorkpaneTerminalPlugin)
workpane_enable_sanitizers(WorkpaneTerminalPlugin)

if(NOT APPLE)
    install(TARGETS WorkpaneTerminalPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
