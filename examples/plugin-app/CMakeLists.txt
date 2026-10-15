cmake_minimum_required(VERSION 3.29)

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")
project(ExampleStudio VERSION 1.2.0 LANGUAGES C CXX)

get_filename_component(workpaneSource "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_subdirectory("${workpaneSource}" workpane)

include("${CMAKE_CURRENT_LIST_DIR}/Applications.cmake")
