qt_add_library(
    WorkpaneBrowserObjects
    OBJECT
    BrowserBookmarksView.cpp
    BrowserBookmarksView.h
    BrowserPlugin.cpp
    BrowserPlugin.h
    BrowserTranslations.h
    BrowserView.cpp
    BrowserView.h
)
target_include_directories(WorkpaneBrowserObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(WorkpaneBrowserObjects PUBLIC Qt6::Core Qt6::Gui Qt6::WebEngineCore Qt6::WebEngineWidgets Qt6::Widgets WorkpaneUi)
set_target_properties(WorkpaneBrowserObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)
workpane_set_warnings(WorkpaneBrowserObjects)
workpane_enable_coverage(WorkpaneBrowserObjects)
workpane_enable_sanitizers(WorkpaneBrowserObjects)

qt_add_library(WorkpaneBrowserPlugin SHARED)
target_link_libraries(WorkpaneBrowserPlugin PRIVATE WorkpaneBrowserObjects)
set_target_properties(WorkpaneBrowserPlugin PROPERTIES OUTPUT_NAME workpane-browser)
workpane_set_warnings(WorkpaneBrowserPlugin)
workpane_enable_coverage(WorkpaneBrowserPlugin)
workpane_enable_sanitizers(WorkpaneBrowserPlugin)

if(NOT APPLE)
    install(TARGETS WorkpaneBrowserPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
