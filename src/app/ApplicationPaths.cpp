#include "app/ApplicationPaths.h"

namespace workpane::app {

std::filesystem::path ApplicationPaths::lua() const {
    return resources / "lua";
}

std::filesystem::path ApplicationPaths::plugins() const {
    return resources / "plugins";
}

std::filesystem::path ApplicationPaths::fonts() const {
    return resources / "fonts";
}

} // namespace workpane::app
