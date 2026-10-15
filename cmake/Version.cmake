set(WORKPANE_PRODUCT_NAME "Workpane")
set(WORKPANE_ORGANIZATION_NAME "Workpane")
set(WORKPANE_BUNDLE_IDENTIFIER "com.workpane.app")
set(WORKPANE_BUILD_NUMBER 1)

if(UNIX AND NOT APPLE)
    set(WORKPANE_DATA_DIRECTORY "workpane")
else()
    set(WORKPANE_DATA_DIRECTORY "Workpane")
endif()

# The debug flag is decided by the configuration being built, so the generator must build exactly one configuration per directory.
if(CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "Workpane requires a single configuration generator such as Ninja")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(WORKPANE_DEBUG_BUILD true)
else()
    set(WORKPANE_DEBUG_BUILD false)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/BuildInfo.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h"
    @ONLY
)
