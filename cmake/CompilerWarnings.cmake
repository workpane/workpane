function(workpane_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /WX /utf-8)
        return()
    endif()

    target_compile_options(
        ${target}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
            -Wundef
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Werror
    )
endfunction()
