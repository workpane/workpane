#pragma once

namespace workpane::platform {

struct WindowGeometry final {
    int x{0};
    int y{0};
    int width{1280};
    int height{800};
    bool maximized{false};

    [[nodiscard]] bool operator==(const WindowGeometry& other) const = default;
};

} // namespace workpane::platform
