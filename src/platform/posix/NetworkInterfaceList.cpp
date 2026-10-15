#include "platform/posix/NetworkInterfaceList.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#if defined(__APPLE__)
#include <net/if_dl.h>
#else
#include <netpacket/packet.h>
#endif

#include <array>
#include <cstddef>
#include <cstdio>
#include <map>
#include <string>

namespace workpane::platform {

nlohmann::json NetworkInterfaceList::collect() {
    ifaddrs* addresses = nullptr;
    nlohmann::json interfaces = nlohmann::json::array();

    if (getifaddrs(&addresses) != 0) {
        return interfaces;
    }

    std::map<std::string, nlohmann::json> found;

    for (const ifaddrs* entry = addresses; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || (entry->ifa_flags & IFF_UP) == 0 || (entry->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        nlohmann::json& interface = found[entry->ifa_name];

        if (interface.is_null()) {
            interface = {{"index", if_nametoindex(entry->ifa_name)}, {"description", entry->ifa_name}, {"mac", ""}, {"ipv4", ""}, {"ipv6", ""}};
        }

        const int family = entry->ifa_addr->sa_family;
        std::array<char, INET6_ADDRSTRLEN> text{};

        // A global IPv6 address replaces a link local one, because the global one is how other machines reach this one.
        if (family == AF_INET && interface["ipv4"].get<std::string>().empty()) {
            inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr, text.data(), text.size());
            interface["ipv4"] = text.data();
        } else if (family == AF_INET6) {
            const auto* address = reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr);
            const bool linkLocal = IN6_IS_ADDR_LINKLOCAL(&address->sin6_addr);

            if (interface["ipv6"].get<std::string>().empty() || !linkLocal) {
                inet_ntop(AF_INET6, &address->sin6_addr, text.data(), text.size());
                interface["ipv6"] = text.data();
            }
#if defined(__APPLE__)
        } else if (family == AF_LINK) {
            const auto* link = reinterpret_cast<const sockaddr_dl*>(entry->ifa_addr);
            interface["mac"] = hardwareAddress(reinterpret_cast<const unsigned char*>(LLADDR(link)), link->sdl_alen);
#else
        } else if (family == AF_PACKET) {
            const auto* link = reinterpret_cast<const sockaddr_ll*>(entry->ifa_addr);
            interface["mac"] = hardwareAddress(link->sll_addr, link->sll_halen);
#endif
        }
    }

    freeifaddrs(addresses);

    // Tunnels and link layer helpers carry no address a reader can use, so only interfaces with an IPv4 or a global IPv6 address are listed.
    for (auto& [name, interface] : found) {
        const std::string ipv6 = interface["ipv6"].get<std::string>();
        const bool reachable = !interface["ipv4"].get<std::string>().empty() || (!ipv6.empty() && !ipv6.starts_with("fe80"));

        if (reachable) {
            interfaces.push_back(std::move(interface));
        }
    }

    return interfaces;
}

std::string NetworkInterfaceList::hardwareAddress(const unsigned char* bytes, std::size_t length) {
    std::string written;

    for (std::size_t index = 0; index < length; ++index) {
        std::array<char, 4> part{};
        std::snprintf(part.data(), part.size(), index == 0 ? "%02x" : ":%02x", bytes[index]);
        written += part.data();
    }

    return written;
}

} // namespace workpane::platform
