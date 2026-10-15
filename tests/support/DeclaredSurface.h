#pragma once

#include "ui/model/NodeId.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace workpane::tests {

// A surface as its plugin declared it, with the kind, the properties and the children of every node, and how many changes the host accepted since its mount.
struct DeclaredSurface final {
    struct Node final {
        std::string kind;
        nlohmann::json properties;
        std::vector<ui::NodeId> children;
    };

    std::map<ui::NodeId, Node> nodes;
    std::size_t changes{0};
};

} // namespace workpane::tests
