#pragma once

#include <cstdint>
#include <string>

namespace workpane::files {

// One entry of a folder, named without its folder, with its kind and the size of a file.
struct DirectoryEntry final {
    enum class Kind { Directory, File, Symlink, Other };

    std::string name;
    Kind kind;
    std::uintmax_t size;
};

} // namespace workpane::files
