#pragma once

#include "ui/components/collections/TableAction.h"
#include "ui/components/collections/TableCell.h"

#include <string>
#include <vector>

namespace workpane::ui {

struct TableRow final {
    std::string id;
    std::vector<TableCell> cells;
    std::vector<TableAction> actions;
};

} // namespace workpane::ui
