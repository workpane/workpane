#pragma once

#include <filesystem>
#include <string>

namespace workpane::ui {

// A file a page downloaded, where the web view saved it and whether it finished or failed with the reason the engine gave.
struct WebDownload final {
    std::filesystem::path path;
    bool finished{false};
    std::string message;
};

} // namespace workpane::ui
