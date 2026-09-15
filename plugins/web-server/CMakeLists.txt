qt_add_library(
    WorkpaneWebServerObjects
    OBJECT
    RequestLogModel.cpp
    RequestLogModel.h
    StaticFileResolver.cpp
    StaticFileResolver.h
    WebServerInstance.cpp
    WebServerInstance.h
    WebServerPlugin.cpp
    WebServerPlugin.h
    WebServerTranslations.h
    WebServerView.cpp
    WebServerView.h
)

target_include_directories(
    WorkpaneWebServerObjects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(
    WorkpaneWebServerObjects
    PUBLIC
        Qt6::Concurrent
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
        WorkpaneUi
)
set_target_properties(
    WorkpaneWebServerObjects
    PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
)

workpane_set_warnings(WorkpaneWebServerObjects)
workpane_enable_coverage(WorkpaneWebServerObjects)
workpane_enable_sanitizers(WorkpaneWebServerObjects)

qt_add_library(WorkpaneWebServerPlugin SHARED)
target_link_libraries(WorkpaneWebServerPlugin PRIVATE WorkpaneWebServerObjects)
set_target_properties(WorkpaneWebServerPlugin PROPERTIES OUTPUT_NAME workpane-web-server)

workpane_set_warnings(WorkpaneWebServerPlugin)
workpane_enable_coverage(WorkpaneWebServerPlugin)
workpane_enable_sanitizers(WorkpaneWebServerPlugin)

if(NOT APPLE)
    install(TARGETS WorkpaneWebServerPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
