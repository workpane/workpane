#pragma once

#include "ui/model/Component.h"
#include "ui/model/NodeId.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace workpane::ui {

// One mounted tree, owned by the plugin that declared it and indexed by the node identities Lua assigned.
struct Surface final {
    std::string id;
    std::string owner;
    std::filesystem::path assets;
    std::unique_ptr<Component> root;
    std::unordered_map<NodeId, Component*> nodes;
};

} // namespace workpane::ui
