qt_add_library(
    WorkpaneDonateObjects
    OBJECT
    DonatePlugin.cpp
    DonatePlugin.h
    DonateTranslations.h
    DonateView.cpp
    DonateView.h
)
qt_add_resources(WorkpaneDonateObjects "donate-assets" PREFIX "/workpane/donate" FILES assets/profile.png)
target_include_directories(WorkpaneDonateObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(WorkpaneDonateObjects PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets WorkpaneUi)
set_target_properties(WorkpaneDonateObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)

workpane_set_warnings(WorkpaneDonateObjects)
workpane_enable_coverage(WorkpaneDonateObjects)
workpane_enable_sanitizers(WorkpaneDonateObjects)

qt_add_library(WorkpaneDonatePlugin SHARED)
target_link_libraries(WorkpaneDonatePlugin PRIVATE WorkpaneDonateObjects)
set_target_properties(WorkpaneDonatePlugin PROPERTIES OUTPUT_NAME workpane-donate)

workpane_set_warnings(WorkpaneDonatePlugin)
workpane_enable_coverage(WorkpaneDonatePlugin)
workpane_enable_sanitizers(WorkpaneDonatePlugin)

if(NOT APPLE)
    install(TARGETS WorkpaneDonatePlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
