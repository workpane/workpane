#pragma once

#include "support/DeclaredSurface.h"
#include "ui/model/NodeId.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace workpane::tests {

// The surfaces the plugins declared, kept from the messages that cross the bridge: the calls that mount, patch and replace nodes, and the events whose state the nodes on the Lua side take.
class DeclaredSurfaces final {
  public:
    void observe(std::string_view name, const nlohmann::json& message, const nlohmann::json* reply);
    [[nodiscard]] const DeclaredSurface* find(std::string_view surface) const;

  private:
    static void add(DeclaredSurface& surface, const nlohmann::json& spec);
    static void collect(const DeclaredSurface& surface, ui::NodeId node, std::set<ui::NodeId>& found);
    static void keptIn(const nlohmann::json& specs, std::set<ui::NodeId>& kept);

    void replace(DeclaredSurface& surface, ui::NodeId parent, const nlohmann::json& children);
    void remember(const nlohmann::json& events);

    std::map<std::string, DeclaredSurface, std::less<>> m_surfaces;
};

} // namespace workpane::tests
