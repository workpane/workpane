#include "support/Resources.h"

namespace workpane::tests {

std::filesystem::path Resources::staged() {
    return WORKPANE_TEST_RESOURCES;
}

std::filesystem::path Resources::fonts() {
    return staged() / "fonts";
}

} // namespace workpane::tests
