qt_add_library(
    WorkpaneLogsObjects
    OBJECT
    LogsPlugin.cpp
    LogsPlugin.h
    LogsTranslations.h
)
target_include_directories(WorkpaneLogsObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(WorkpaneLogsObjects PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets WorkpaneUi)
set_target_properties(WorkpaneLogsObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)

workpane_set_warnings(WorkpaneLogsObjects)
workpane_enable_coverage(WorkpaneLogsObjects)
workpane_enable_sanitizers(WorkpaneLogsObjects)

qt_add_library(WorkpaneLogsPlugin SHARED)
target_link_libraries(WorkpaneLogsPlugin PRIVATE WorkpaneLogsObjects)
set_target_properties(WorkpaneLogsPlugin PROPERTIES OUTPUT_NAME workpane-logs)

workpane_set_warnings(WorkpaneLogsPlugin)
workpane_enable_coverage(WorkpaneLogsPlugin)
workpane_enable_sanitizers(WorkpaneLogsPlugin)

if(NOT APPLE)
    install(TARGETS WorkpaneLogsPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
