set(WORKPANE_TERMINAL_ENGINE_SOURCES
    GhosttyTerminalAdapter.cpp
    GhosttyHeaders.h
    GhosttyTerminalAdapter.h
    ShellProfile.cpp
    ShellProfile.h
    TerminalDimensions.h
    TerminalSession.cpp
    TerminalSession.h
    TerminalShortcuts.h
    TerminalThemeCatalog.cpp
    TerminalThemeCatalog.h
    platform/IPtyBackend.h
    ../domain/TerminalSessionState.h
    ../domain/TerminalTheme.h
    ../ui/TerminalWidget.cpp
    ../ui/TerminalWidget.h
)

set(WORKPANE_TERMINAL_ENGINE_PLATFORM_SOURCES ${WORKPANE_TERMINAL_PLATFORM_SOURCES})
list(TRANSFORM WORKPANE_TERMINAL_ENGINE_PLATFORM_SOURCES REPLACE "^terminal/" "")

qt_add_library(
    WorkpaneTerminalEngine
    STATIC
        ${WORKPANE_TERMINAL_ENGINE_SOURCES}
        ${WORKPANE_TERMINAL_ENGINE_PLATFORM_SOURCES}
)
target_include_directories(WorkpaneTerminalEngine PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/generated)
if(WIN32)
    target_compile_definitions(WorkpaneTerminalEngine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(
    WorkpaneTerminalEngine
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        WorkpaneUi
        ghostty-vt-static
        ${WORKPANE_TERMINAL_PLATFORM_LIBRARIES}
)
set_target_properties(
    WorkpaneTerminalEngine
    PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
)

workpane_set_warnings(WorkpaneTerminalEngine)
workpane_enable_coverage(WorkpaneTerminalEngine)
workpane_enable_sanitizers(WorkpaneTerminalEngine)
