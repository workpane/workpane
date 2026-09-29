#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace workpane::platform {

// Lists the network interfaces that are up and carry an address, with their hardware address, their IPv4 address and their IPv6 address, a global one preferred over a link local one.
class NetworkInterfaceList final {
  public:
    [[nodiscard]] static nlohmann::json collect();

  private:
    [[nodiscard]] static std::string hardwareAddress(const unsigned char* bytes, std::size_t length);
};

} // namespace workpane::platform
