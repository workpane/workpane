#include "support/ProductDatabase.h"

#include "persistence/DatabaseBootstrap.h"

#include <gtest/gtest.h>

#include <utility>

namespace workpane::tests {

persistence::Database ProductDatabase::open(const TemporaryDirectory& directory) {
    auto opened = persistence::DatabaseBootstrap::open(directory.path());
    EXPECT_TRUE(opened.hasValue());

    return std::move(opened.value().database);
}

} // namespace workpane::tests
