#pragma once

#include <string>

namespace workpane::ui {

// The button a dialog closed with, which is the cancel button when the reader pressed Escape, and the text a prompt carried.
struct DialogAnswer final {
    std::string button;
    std::string value;
};

} // namespace workpane::ui
