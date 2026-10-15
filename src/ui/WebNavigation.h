#pragma once

#include <string>

namespace workpane::ui {

// Where a web view is and what it can do next, as the engine of the platform reports it, with the icon of its page as a PNG data address once the page drew one.
struct WebNavigation final {
    std::string url;
    std::string title;
    std::string icon;
    bool loading{false};
    bool canGoBack{false};
    bool canGoForward{false};

    [[nodiscard]] bool operator==(const WebNavigation& other) const = default;
};

} // namespace workpane::ui
