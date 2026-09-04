set(WORKPANE_UI_SOURCES
    ApplicationShortcuts.h
    Components.cpp
    Components.h
    FindBar.cpp
    FindBar.h
    Icons.cpp
    Icons.h
    TabBar.cpp
    TabBar.h
    Theme.cpp
    Theme.h
)

qt_add_library(WorkpaneUi STATIC ${WORKPANE_UI_SOURCES})
target_include_directories(WorkpaneUi PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(WorkpaneUi PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(
    WorkpaneUi
    PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
)

workpane_set_warnings(WorkpaneUi)
workpane_enable_coverage(WorkpaneUi)
workpane_enable_sanitizers(WorkpaneUi)
