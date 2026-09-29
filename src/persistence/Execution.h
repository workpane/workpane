#pragma once

#include <cstdint>

namespace workpane::persistence {

struct Execution final {
    std::int64_t changes{0};
    std::int64_t lastInsertRowId{0};
};

} // namespace workpane::persistence
