qt_add_library(
    WorkpaneSystemInformationObjects
    OBJECT
    SystemInformation.cpp
    SystemInformation.h
    SystemInformationPlugin.cpp
    SystemInformationPlugin.h
    SystemInformationTranslations.h
    SystemInformationView.cpp
    SystemInformationView.h
)
target_include_directories(WorkpaneSystemInformationObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(WorkpaneSystemInformationObjects PUBLIC Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Widgets WorkpaneUi lfreist-hwinfo::hwinfo)
set_target_properties(WorkpaneSystemInformationObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)

workpane_set_warnings(WorkpaneSystemInformationObjects)
workpane_enable_coverage(WorkpaneSystemInformationObjects)
workpane_enable_sanitizers(WorkpaneSystemInformationObjects)

qt_add_library(WorkpaneSystemInformationPlugin SHARED)
target_link_libraries(WorkpaneSystemInformationPlugin PRIVATE WorkpaneSystemInformationObjects)
set_target_properties(WorkpaneSystemInformationPlugin PROPERTIES OUTPUT_NAME workpane-system-information)
if(APPLE)
    set_target_properties(WorkpaneSystemInformationPlugin PROPERTIES BUILD_RPATH "@loader_path/../Frameworks")
elseif(UNIX)
    set_target_properties(WorkpaneSystemInformationPlugin PROPERTIES BUILD_RPATH "$ORIGIN/.." INSTALL_RPATH "$ORIGIN/../../lib;$ORIGIN/..")
endif()

workpane_set_warnings(WorkpaneSystemInformationPlugin)
workpane_enable_coverage(WorkpaneSystemInformationPlugin)
workpane_enable_sanitizers(WorkpaneSystemInformationPlugin)

if(NOT APPLE)
    install(TARGETS WorkpaneSystemInformationPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
