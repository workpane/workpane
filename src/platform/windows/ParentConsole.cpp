#include "platform/ParentConsole.h"

#include <windows.h>

#include <cstdio>
#include <tuple>

namespace workpane::platform {

// A program of the windows subsystem starts without a console, so the console of the terminal that started it takes the output unless a pipe or a file already does.
void ParentConsole::attach() {
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN || AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        return;
    }

    FILE* stream = nullptr;
    std::ignore = freopen_s(&stream, "CONOUT$", "w", stdout);
}

} // namespace workpane::platform
