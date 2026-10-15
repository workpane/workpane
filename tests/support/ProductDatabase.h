#pragma once

#include "persistence/Database.h"
#include "support/TemporaryDirectory.h"

namespace workpane::tests {

// Opens the product database the way the product does, inside the directory of one test.
class ProductDatabase final {
  public:
    [[nodiscard]] static persistence::Database open(const TemporaryDirectory& directory);
};

} // namespace workpane::tests
