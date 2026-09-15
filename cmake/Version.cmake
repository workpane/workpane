set(WORKPANE_PRODUCT_NAME "Workpane")
set(WORKPANE_ORGANIZATION_NAME "Workpane")
set(WORKPANE_ORGANIZATION_DOMAIN "workpane.app")
set(WORKPANE_BUNDLE_IDENTIFIER "com.workpane.app")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/BuildInfo.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h"
    @ONLY
)
