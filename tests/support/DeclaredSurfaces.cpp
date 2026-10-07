#include "support/DeclaredSurfaces.h"

#include <string>
#include <utility>
#include <vector>

namespace workpane::tests {

// Only calls the host accepted change what a surface shows, and events reach Lua on the interface thread under their own name.
void DeclaredSurfaces::observe(std::string_view name, const nlohmann::json& message, const nlohmann::json* reply) {
    if (reply == nullptr) {
        if (name == "workpane.ui.events") {
            remember(message.at("events"));
        }

        return;
    }

    if (!reply->value("ok", false)) {
        return;
    }

    if (name == "workpane_ui_mount") {
        DeclaredSurface mounted;
        add(mounted, message.at("tree"));
        m_surfaces[message.at("surface").get<std::string>()] = std::move(mounted);
        return;
    }

    if (name == "workpane_ui_unmount") {
        m_surfaces.erase(message.at("surface").get<std::string>());
        return;
    }

    const auto found = m_surfaces.find(message.value("surface", std::string()));

    if ((name != "workpane_ui_patch" && name != "workpane_ui_children") || found == m_surfaces.end()) {
        return;
    }

    const auto node = message.at("node").get<ui::NodeId>();
    ++found->second.changes;

    if (name == "workpane_ui_patch") {
        found->second.nodes.at(node).properties.update(message.at("props"));
        return;
    }

    if (name == "workpane_ui_children") {
        replace(found->second, node, message.at("children"));
        found->second.nodes.at(node).properties.update(message.value("props", nlohmann::json::object()));
    }
}

const DeclaredSurface* DeclaredSurfaces::find(std::string_view surface) const {
    const auto found = m_surfaces.find(surface);
    return found == m_surfaces.end() ? nullptr : &found->second;
}

// A spec that names a kept node leaves that node and everything below it as it was.
void DeclaredSurfaces::add(DeclaredSurface& surface, const nlohmann::json& spec) {
    const auto node = spec.at("id").get<ui::NodeId>();

    if (spec.value("kept", false)) {
        return;
    }

    DeclaredSurface::Node declared{spec.at("kind").get<std::string>(), spec.value("props", nlohmann::json::object()), {}};

    for (const auto& child : spec.value("children", nlohmann::json::array())) {
        declared.children.push_back(child.at("id").get<ui::NodeId>());
        add(surface, child);
    }

    surface.nodes[node] = std::move(declared);
}

void DeclaredSurfaces::collect(const DeclaredSurface& surface, ui::NodeId node, std::set<ui::NodeId>& found) {
    found.insert(node);

    for (const ui::NodeId child : surface.nodes.at(node).children) {
        collect(surface, child, found);
    }
}

void DeclaredSurfaces::keptIn(const nlohmann::json& specs, std::set<ui::NodeId>& kept) {
    for (const auto& spec : specs) {
        if (spec.value("kept", false)) {
            kept.insert(spec.at("id").get<ui::NodeId>());
            continue;
        }

        keptIn(spec.value("children", nlohmann::json::array()), kept);
    }
}

// The nodes below the parent that the new children do not keep leave with their subtrees, and a kept node keeps its own.
void DeclaredSurfaces::replace(DeclaredSurface& surface, ui::NodeId parent, const nlohmann::json& children) {
    std::set<ui::NodeId> kept;
    keptIn(children, kept);
    std::set<ui::NodeId> staying;

    for (const ui::NodeId node : kept) {
        collect(surface, node, staying);
    }

    std::set<ui::NodeId> leaving;

    for (const ui::NodeId child : surface.nodes.at(parent).children) {
        collect(surface, child, leaving);
    }

    for (const ui::NodeId node : leaving) {
        if (!staying.contains(node)) {
            surface.nodes.erase(node);
        }
    }

    std::vector<ui::NodeId> placed;

    for (const auto& child : children) {
        placed.push_back(child.at("id").get<ui::NodeId>());
        add(surface, child);
    }

    surface.nodes.at(parent).children = std::move(placed);
}

// An event names the properties it changed as fields of its value and the new order of the children it moved.
void DeclaredSurfaces::remember(const nlohmann::json& events) {
    const nlohmann::json empty = nlohmann::json::object();

    for (const auto& event : events) {
        const auto found = m_surfaces.find(event.at("surface").get<std::string>());

        if (found == m_surfaces.end() || !found->second.nodes.contains(event.at("node").get<ui::NodeId>())) {
            continue;
        }

        DeclaredSurface::Node& node = found->second.nodes.at(event.at("node").get<ui::NodeId>());
        const nlohmann::json& state = event.contains("state") ? event.at("state") : empty;

        for (const auto& [property, field] : state.items()) {
            node.properties[property] = event.at("value").at(field.get<std::string>());
        }

        if (event.contains("order")) {
            node.children = event.at("order").get<std::vector<ui::NodeId>>();
        }
    }
}

} // namespace workpane::tests
