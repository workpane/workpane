#pragma once

#include "Error.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace workpane::tests {

// The sounds a test played and the ones it stopped, and the failures that end the sounds of the files they name at the next update, as a machine that cannot play them does.
struct AudioRecord final {
    struct Played final {
        std::uint64_t sound;
        std::filesystem::path file;
        float volume;
        bool loop;
    };

    std::vector<Played> played;
    std::vector<std::uint64_t> stopped;
    std::map<std::string, Error> failures;
};

} // namespace workpane::tests
