#pragma once

#include "ui/model/NodeId.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace workpane::ui {

// An event a node reports, which names the properties it changed as fields of its value and the new order of its children when it moved them.
struct UiEvent final {
    std::string surface;
    NodeId node{0};
    std::string name;
    nlohmann::json value;
    nlohmann::json state;
    std::vector<NodeId> order;
};

} // namespace workpane::ui
