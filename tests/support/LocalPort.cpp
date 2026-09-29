#include "support/LocalPort.h"

#include <httplib.h>

namespace workpane::tests {

// A port nobody holds, which the operating system picks and the probe releases before a test binds it.
int LocalPort::free() {
    httplib::Server probe;
    const int port = bind(probe);
    probe.stop();

    return port;
}

// The server takes a port the operating system picks for it alone, since the default of the library lets several listeners share one.
int LocalPort::bind(httplib::Server& server) {
    // clang-format off
    server.set_socket_options([](socket_t socket) {
#if defined(_WIN32)
        httplib::set_socket_opt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
        httplib::set_socket_opt(socket, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
    });
    // clang-format on

    return server.bind_to_any_port("127.0.0.1");
}

} // namespace workpane::tests
