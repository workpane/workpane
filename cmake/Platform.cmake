if(APPLE)
    set(WORKPANE_TERMINAL_PLATFORM_SOURCES
        terminal/platform/posix/PosixPtyBackend.cpp
        terminal/platform/posix/PosixPtyBackend.h
        terminal/platform/posix/PosixShellIntegration.cpp
        terminal/platform/posix/PosixShellIntegration.h
    )
    set(WORKPANE_TERMINAL_PLATFORM_LIBRARIES "-framework CoreFoundation" "-framework Security" util)
elseif(WIN32)
    set(WORKPANE_TERMINAL_PLATFORM_SOURCES
        terminal/platform/windows/ConPtyBackend.cpp
        terminal/platform/windows/ConPtyBackend.h
    )
    set(WORKPANE_TERMINAL_PLATFORM_LIBRARIES userenv)
else()
    set(WORKPANE_TERMINAL_PLATFORM_SOURCES
        terminal/platform/posix/PosixPtyBackend.cpp
        terminal/platform/posix/PosixPtyBackend.h
        terminal/platform/posix/PosixShellIntegration.cpp
        terminal/platform/posix/PosixShellIntegration.h
    )
    set(WORKPANE_TERMINAL_PLATFORM_LIBRARIES util)
endif()
