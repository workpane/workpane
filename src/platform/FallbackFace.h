#pragma once

#include <cstddef>
#include <filesystem>

namespace workpane::platform {

// A face of the system that draws what the faces of the product lack, such as color emoji, symbols and other scripts, named by its file and its index inside it.
struct FallbackFace final {
    static constexpr std::size_t largestCount{8};

    std::filesystem::path file;
    unsigned int index{0};
    bool color{false};
};

} // namespace workpane::platform
