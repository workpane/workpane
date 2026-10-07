#pragma once

#include <optional>

namespace workpane::platform {

// How long a held key waits before it repeats and how long it waits between the repeats that follow, in seconds, as the reader configured the system.
struct KeyRepeat final {
    float delay{0.0F};
    float interval{0.0F};

    [[nodiscard]] static std::optional<KeyRepeat> system();
};

} // namespace workpane::platform
