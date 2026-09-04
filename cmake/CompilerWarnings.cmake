function(workpane_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
        return()
    endif()

    target_compile_options(
        ${target}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wformat=2
            -Wundef
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Werror
    )

    # GCC reports optional payloads reached through a future continuation as possibly uninitialized, which the standard library guarantees they are not.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -Wno-error=maybe-uninitialized)
    endif()
endfunction()
