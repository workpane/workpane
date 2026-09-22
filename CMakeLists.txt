cmake_minimum_required(VERSION 3.28)

project(Workpane VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The COFF object format carries a bounded number of sections, and a debug build of a large translation unit passes it.
if(MSVC)
    add_compile_options(/bigobj)
endif()

option(WORKPANE_ENABLE_COVERAGE "Enable first-party coverage instrumentation" OFF)
option(WORKPANE_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(WORKPANE_BUILD_TESTS "Build the test suites" ON)
set(WORKPANE_CODESIGN_IDENTITY "-" CACHE STRING "macOS codesign identity used for the assembled application")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Coverage)
include(Sanitizers)
include(Dependencies)
include(Platform)
include(Version)

find_package(Qt6 6.11.2 REQUIRED COMPONENTS Concurrent Core Gui Network Sql WebEngineCore WebEngineWidgets Widgets)

if(NOT QT6_IS_SHARED_LIBS_BUILD)
    message(FATAL_ERROR "Workpane requires a shared Qt 6 build and does not support static Qt libraries")
endif()

qt_standard_project_setup(REQUIRES 6.11.2)

add_subdirectory(src/ui)
add_subdirectory(src/agent)
add_subdirectory(src/terminal)
add_subdirectory(plugins/logs)
add_subdirectory(plugins/system-information)
add_subdirectory(plugins/donate)
add_subdirectory(plugins/ai)
add_subdirectory(plugins/browser)
add_subdirectory(plugins/code-editor)
add_subdirectory(plugins/terminal)
add_subdirectory(plugins/web-server)
add_subdirectory(src)

if(WORKPANE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(Packaging)
