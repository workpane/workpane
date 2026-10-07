#include "support/RootedPath.h"

namespace workpane::tests {

std::filesystem::path RootedPath::of(std::string_view relative) {
    return std::filesystem::current_path().root_path() / relative;
}

} // namespace workpane::tests
