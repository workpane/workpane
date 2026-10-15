cmake_minimum_required(VERSION 3.29)

# The oldest macOS release the product runs on is chosen before the project, because the first project command already fixes the target of the toolchain.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Oldest macOS release the product runs on")

if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "13.3")
    message(FATAL_ERROR "Workpane requires a macOS deployment target of at least 13.3")
endif()

project(Workpane VERSION 0.0.1 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The executable and every shared library it loads land in the same two directories, which is the layout a package reproduces.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(MSVC)
    add_compile_options(/bigobj)
endif()

option(WORKPANE_ENABLE_COVERAGE "Enable first-party coverage instrumentation" OFF)
option(WORKPANE_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(WORKPANE_BUILD_APPLICATION "Build the Workpane desktop application and its bundled plugins" ${PROJECT_IS_TOP_LEVEL})
option(WORKPANE_BUILD_TESTS "Build the Workpane application test suites" ${WORKPANE_BUILD_APPLICATION})

if(WORKPANE_BUILD_TESTS AND NOT WORKPANE_BUILD_APPLICATION)
    message(FATAL_ERROR "The Workpane application test suites require WORKPANE_BUILD_APPLICATION")
endif()
set(WORKPANE_CODESIGN_IDENTITY "-" CACHE STRING "Identity the assembled application is signed with on macOS")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Coverage)
include(Sanitizers)
include(Dependencies)
include(Platform)
include(Version)
include(WorkpaneApplication)

add_subdirectory(src)

if(WORKPANE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(WORKPANE_BUILD_APPLICATION)
    include(Packaging)
endif()
