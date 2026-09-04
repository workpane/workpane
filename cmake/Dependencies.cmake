include(FetchContent)

set(WORKPANE_GHOSTTY_COMMIT "f64f4aca2c29b554d111b36c3d946a9bddd159ff" CACHE STRING "Pinned Ghostty commit")
find_program(ZIG_EXECUTABLE zig REQUIRED NO_CACHE)

FetchContent_Declare(
    ghostty
    GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
    GIT_TAG ${WORKPANE_GHOSTTY_COMMIT}
    GIT_SHALLOW FALSE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(ghostty)

set(HWINFO_SHARED ON CACHE BOOL "" FORCE)
set(HWINFO_STATIC OFF CACHE BOOL "" FORCE)
set(HWINFO_GPU_OPENCL OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWINFO_CMAKE_BINARY_DIR "${CMAKE_BINARY_DIR}/hwinfo" CACHE PATH "" FORCE)
set(SYSTEM_INFORMATION_HWINFO_SHA "88c5072c4a137d54e94c7e712ae28ac284f1dd9b" CACHE STRING "Pinned hwinfo commit")
FetchContent_Declare(
    hwinfo
    GIT_REPOSITORY https://github.com/lfreist/hwinfo.git
    GIT_TAG ${SYSTEM_INFORMATION_HWINFO_SHA}
    GIT_SHALLOW FALSE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(hwinfo)
set(WORKPANE_HWINFO_TARGETS hwinfo_battery hwinfo_cpu hwinfo_disk hwinfo_gpu hwinfo_mainboard hwinfo_os hwinfo_ram hwinfo_network)

if(WORKPANE_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(googletest)
endif()
