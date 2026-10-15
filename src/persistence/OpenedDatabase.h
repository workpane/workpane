#pragma once

#include "Error.h"
#include "persistence/Database.h"

#include <filesystem>
#include <optional>

namespace workpane::persistence {

struct OpenedDatabase final {
    Database database;
    std::optional<std::filesystem::path> setAside;
    std::optional<Error> importFailure;
};

} // namespace workpane::persistence
