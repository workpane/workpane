# The Lua SDK, the bundled plugins, the fonts and the images are staged where the running layout reads them: inside the bundle on macOS and in the shared data directory beside the executable elsewhere.
if(APPLE)
    set(WORKPANE_STAGED_RESOURCES "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Workpane.app/Contents/Resources")
else()
    set(WORKPANE_STAGED_RESOURCES "${CMAKE_BINARY_DIR}/share/workpane")
endif()

file(GLOB_RECURSE WORKPANE_RESOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/lua/*"
    "${CMAKE_SOURCE_DIR}/plugins/*"
    "${CMAKE_SOURCE_DIR}/assets/fonts/*"
    "${CMAKE_SOURCE_DIR}/extras/images/logo.png"
)

set(WORKPANE_RESOURCES_STAMP "${CMAKE_BINARY_DIR}/resources.stamp")
set(WORKPANE_RESOURCES_LIST "${CMAKE_BINARY_DIR}/resources.list")

# The list of resources is written only when it changes, so a file that is removed or renamed stages the resources again although no file left is newer.
list(JOIN WORKPANE_RESOURCE_FILES "\n" workpaneResourceList)
set(workpaneWrittenList "")

if(EXISTS "${WORKPANE_RESOURCES_LIST}")
    file(READ "${WORKPANE_RESOURCES_LIST}" workpaneWrittenList)
endif()

if(NOT workpaneWrittenList STREQUAL workpaneResourceList)
    file(WRITE "${WORKPANE_RESOURCES_LIST}" "${workpaneResourceList}")
endif()

add_custom_command(
    OUTPUT "${WORKPANE_RESOURCES_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${WORKPANE_STAGED_RESOURCES}/lua" "${WORKPANE_STAGED_RESOURCES}/plugins" "${WORKPANE_STAGED_RESOURCES}/fonts" "${WORKPANE_STAGED_RESOURCES}/images"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WORKPANE_STAGED_RESOURCES}/images"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/lua" "${WORKPANE_STAGED_RESOURCES}/lua"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/plugins" "${WORKPANE_STAGED_RESOURCES}/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/fonts" "${WORKPANE_STAGED_RESOURCES}/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/extras/images/logo.png" "${WORKPANE_STAGED_RESOURCES}/images/logo.png"
    COMMAND ${CMAKE_COMMAND} -E touch "${WORKPANE_RESOURCES_STAMP}"
    DEPENDS ${WORKPANE_RESOURCE_FILES} "${WORKPANE_RESOURCES_LIST}"
    COMMENT "Staging the Workpane resources"
    VERBATIM
)

add_custom_target(WorkpaneResources ALL DEPENDS "${WORKPANE_RESOURCES_STAMP}")
