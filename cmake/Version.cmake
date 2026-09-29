set(WORKPANE_PRODUCT_NAME "Workpane")
set(WORKPANE_ORGANIZATION_NAME "Workpane")
set(WORKPANE_BUNDLE_IDENTIFIER "com.workpane.app")

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
