#pragma once

#include <cstdint>
#include <string>

namespace workpane::http {

// One answered request as the log of a server keeps it, with the moment it arrived as a stored UTC timestamp.
struct RequestRecord final {
    std::string timestamp;
    std::string method;
    std::string path;
    int status{0};
    std::int64_t durationMilliseconds{0};
    std::int64_t responseBytes{0};
    std::string remoteAddress;
};

} // namespace workpane::http
