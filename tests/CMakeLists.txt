find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

include(GoogleTest)

# The Windows offscreen platform plugin exposes no font database, so the native plugin runs the interface tests there.
set(WORKPANE_TEST_PLATFORM "offscreen")
if(WIN32)
    set(WORKPANE_TEST_PLATFORM "windows")
endif()

# A case chains many asynchronous conditions and each one owns the scaled wait budget, so the case budget stays far above that budget and only a stuck process is killed.
set(WORKPANE_TEST_TIMEOUT 300)
if(WORKPANE_ENABLE_SANITIZERS)
    set(WORKPANE_TEST_TIMEOUT 900)
endif()

set(WORKPANE_TEST_INCLUDE_DIRECTORIES
    ${googletest_SOURCE_DIR}/googletest/include
    ${googletest_SOURCE_DIR}/googlemock/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/generated
)

# The entry point carries the fixture servers every suite starts, so it is compiled once and linked by all of them.
qt_add_library(WorkpaneTestMain OBJECT TestMain.cpp)
target_include_directories(WorkpaneTestMain BEFORE PRIVATE ${WORKPANE_TEST_INCLUDE_DIRECTORIES})
target_link_libraries(WorkpaneTestMain PUBLIC GTest::gtest GTest::gmock Qt6::Test Qt6::Widgets)
workpane_set_warnings(WorkpaneTestMain)
workpane_enable_coverage(WorkpaneTestMain)
workpane_enable_sanitizers(WorkpaneTestMain)

function(workpane_add_test_suite target)
    qt_add_executable(${target} ${ARGN})
    target_include_directories(${target} BEFORE PRIVATE ${WORKPANE_TEST_INCLUDE_DIRECTORIES})
    target_link_libraries(${target} PRIVATE WorkpaneTestMain GTest::gtest GTest::gmock Qt6::Test)
    workpane_set_warnings(${target})
    workpane_enable_coverage(${target})
    workpane_enable_sanitizers(${target})
    # a suite that links Qt WebEngine needs seconds to answer the discovery listing on a cold runner.
    gtest_discover_tests(
        ${target}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 120
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${WORKPANE_TEST_PLATFORM};QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu;QTWEBENGINE_DISABLE_SANDBOX=1"
            TIMEOUT ${WORKPANE_TEST_TIMEOUT}
    )
endfunction()

workpane_add_test_suite(
    WorkpaneCoreTests
    core/AgentResourceTests.cpp
    core/CoreTests.cpp
)
target_link_libraries(WorkpaneCoreTests PRIVATE WorkpaneCore)
add_dependencies(WorkpaneCoreTests WorkpaneBrowserPlugin WorkpaneCodeEditorPlugin WorkpaneDonatePlugin WorkpaneLogsPlugin WorkpaneAiPlugin WorkpaneSystemInformationPlugin WorkpaneTerminalPlugin WorkpaneWebServerPlugin)
if(APPLE)
    set(WORKPANE_TEST_PLUGIN_DIRECTORY "${CMAKE_BINARY_DIR}/PlugIns")
else()
    set(WORKPANE_TEST_PLUGIN_DIRECTORY "$<TARGET_FILE_DIR:WorkpaneCoreTests>/plugins")
endif()
add_custom_target(
    WorkpaneTestPlugins ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WORKPANE_TEST_PLUGIN_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorkpaneLogsPlugin>"
            "${WORKPANE_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorkpaneSystemInformationPlugin>"
            "${WORKPANE_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorkpaneDonatePlugin>"
            "${WORKPANE_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorkpaneAiPlugin>"
            "${WORKPANE_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorkpaneBrowserPlugin>"
            "${WORKPANE_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorkpaneCodeEditorPlugin>"
            "${WORKPANE_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorkpaneTerminalPlugin>"
            "${WORKPANE_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorkpaneWebServerPlugin>"
            "${WORKPANE_TEST_PLUGIN_DIRECTORY}/"
    DEPENDS WorkpaneBrowserPlugin WorkpaneCodeEditorPlugin WorkpaneDonatePlugin WorkpaneLogsPlugin WorkpaneAiPlugin WorkpaneSystemInformationPlugin WorkpaneTerminalPlugin WorkpaneWebServerPlugin
)
add_dependencies(WorkpaneCoreTests WorkpaneTestPlugins)
if(APPLE)
    set(WORKPANE_TEST_HWINFO_DIRECTORY "${CMAKE_BINARY_DIR}/Frameworks")
else()
    set(WORKPANE_TEST_HWINFO_DIRECTORY "$<TARGET_FILE_DIR:WorkpaneCoreTests>")
endif()
add_custom_command(
    TARGET WorkpaneCoreTests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WORKPANE_TEST_HWINFO_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:hwinfo_battery>" "$<TARGET_FILE:hwinfo_cpu>" "$<TARGET_FILE:hwinfo_disk>" "$<TARGET_FILE:hwinfo_gpu>" "$<TARGET_FILE:hwinfo_mainboard>" "$<TARGET_FILE:hwinfo_os>" "$<TARGET_FILE:hwinfo_ram>" "$<TARGET_FILE:hwinfo_network>" "${WORKPANE_TEST_HWINFO_DIRECTORY}/"
)

workpane_add_test_suite(
    WorkpaneTerminalTests
    terminal/TerminalTests.cpp
)
target_link_libraries(WorkpaneTerminalTests PRIVATE WorkpaneTerminalObjects WorkpaneCore)

workpane_add_test_suite(
    WorkpaneWebServerTests
    web-server/WebServerTests.cpp
)
target_link_libraries(WorkpaneWebServerTests PRIVATE WorkpaneWebServerObjects WorkpaneCore)

workpane_add_test_suite(
    WorkpaneLogsTests
    logs/LogsTests.cpp
)
target_link_libraries(WorkpaneLogsTests PRIVATE WorkpaneLogsObjects WorkpaneCore)

workpane_add_test_suite(
    WorkpaneSystemInformationTests
    system-information/SystemInformationTests.cpp
)
target_link_libraries(WorkpaneSystemInformationTests PRIVATE WorkpaneSystemInformationObjects WorkpaneCore)
add_custom_command(TARGET WorkpaneSystemInformationTests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:hwinfo_battery>" "$<TARGET_FILE:hwinfo_cpu>" "$<TARGET_FILE:hwinfo_disk>" "$<TARGET_FILE:hwinfo_gpu>" "$<TARGET_FILE:hwinfo_mainboard>" "$<TARGET_FILE:hwinfo_os>" "$<TARGET_FILE:hwinfo_ram>" "$<TARGET_FILE:hwinfo_network>" "$<TARGET_FILE_DIR:WorkpaneSystemInformationTests>/")

workpane_add_test_suite(
    WorkpaneAiTests
    ai/AiTests.cpp
    ai/AiToolTests.cpp
    ai/AiRuntimeTests.cpp
    ai/AiTestSupport.h
)
target_link_libraries(WorkpaneAiTests PRIVATE WorkpaneAiObjects WorkpaneCore)

workpane_add_test_suite(
    WorkpaneDonateTests
    donate/DonateTests.cpp
)
target_link_libraries(WorkpaneDonateTests PRIVATE WorkpaneDonateObjects WorkpaneCore)

workpane_add_test_suite(
    WorkpaneBrowserTests
    browser/BrowserTests.cpp
)
target_link_libraries(WorkpaneBrowserTests PRIVATE WorkpaneBrowserObjects WorkpaneCore)

workpane_add_test_suite(
    WorkpaneCodeEditorTests
    code-editor/CodeEditorTests.cpp
)
target_link_libraries(WorkpaneCodeEditorTests PRIVATE WorkpaneCodeEditorObjects WorkpaneCore)
