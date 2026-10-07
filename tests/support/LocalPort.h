#pragma once

namespace httplib {
class Server;
}

namespace workpane::tests {

// Ports on the loopback interface for the servers of the suite, which never share a port with another listener.
// A server sharing a port would answer requests meant for a server of another test running at the same time.
class LocalPort final {
  public:
    [[nodiscard]] static int free();
    [[nodiscard]] static int bind(httplib::Server& server);
};

} // namespace workpane::tests
