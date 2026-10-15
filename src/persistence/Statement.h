#pragma once

#include "persistence/Database.h"

#include <string>
#include <vector>

namespace workpane::persistence {

struct Statement final {
    std::string sql;
    std::vector<Value> bindings;
};

} // namespace workpane::persistence
