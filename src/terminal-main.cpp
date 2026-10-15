#include "platform/posix/TerminalSession.h"

int main(int argc, char** argv) {
    return workpane::platform::TerminalSession::run(argc, argv);
}
