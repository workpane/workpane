if(WORKPANE_ENABLE_COVERAGE)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/generated/CoverageCompiler.txt" CONTENT "${CMAKE_CXX_COMPILER_ID}\n")
endif()

function(workpane_enable_coverage target)
    if(NOT WORKPANE_ENABLE_COVERAGE)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        target_link_options(${target} PRIVATE --coverage)
        return()
    endif()

    message(FATAL_ERROR "Coverage requires a toolchain that emits gcov data")
endfunction()
