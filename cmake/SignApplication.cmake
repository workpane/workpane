find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
set(options --force --sign "${IDENTITY}")

if(IDENTITY STREQUAL "-")
    list(APPEND options --timestamp=none)
else()
    list(APPEND options --options runtime --timestamp)
endif()

file(GLOB libraries LIST_DIRECTORIES false "${BUNDLE}/Contents/Frameworks/*.dylib")

foreach(binary IN LISTS libraries ITEMS "${BUNDLE}/Contents/MacOS/WorkpaneAskpass" "${BUNDLE}/Contents/MacOS/WorkpaneTerminal")
    execute_process(COMMAND "${CODESIGN_EXECUTABLE}" ${options} "${binary}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()

if(NOT IDENTITY STREQUAL "-")
    list(APPEND options --entitlements "${ENTITLEMENTS}")
endif()

execute_process(COMMAND "${CODESIGN_EXECUTABLE}" ${options} "${BUNDLE}" COMMAND_ERROR_IS_FATAL ANY)
