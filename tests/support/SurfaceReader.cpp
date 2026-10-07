#include "support/SurfaceReader.h"

#include <algorithm>
#include <string>

namespace workpane::tests {

SurfaceReader::SurfaceReader(const DeclaredSurface* surface) : m_surface(surface) {}

bool SurfaceReader::mounted() const {
    return m_surface != nullptr;
}

// A node shows a translated text when one of its properties names that key.
bool SurfaceReader::showsKey(std::string_view key) const {
    return nodeShowing(key).has_value();
}

// A node shows a literal text when one of its properties holds exactly that text.
bool SurfaceReader::showsText(std::string_view literal) const {
    if (m_surface == nullptr) {
        return false;
    }

    for (const auto& [id, node] : m_surface->nodes) {
        for (const auto& [name, value] : node.properties.items()) {
            if (value.is_string() && value.get<std::string>() == literal) {
                return true;
            }
        }
    }

    return false;
}

// Lua numbers its nodes in the order it creates them, so the first node showing a key is the one with the smallest identity.
std::optional<ui::NodeId> SurfaceReader::nodeShowing(std::string_view key) const {
    if (m_surface == nullptr) {
        return std::nullopt;
    }

    std::optional<ui::NodeId> first;

    for (const auto& [id, node] : m_surface->nodes) {
        for (const auto& [name, value] : node.properties.items()) {
            if (value.is_object() && value.contains("key") && value["key"] == nlohmann::json(key) && (!first.has_value() || id < *first)) {
                first = id;
            }
        }
    }

    return first;
}

std::vector<ui::NodeId> SurfaceReader::nodes(std::string_view kind) const {
    std::vector<ui::NodeId> found;

    if (m_surface == nullptr) {
        return found;
    }

    for (const auto& [id, node] : m_surface->nodes) {
        if (node.kind == kind) {
            found.push_back(id);
        }
    }

    std::ranges::sort(found);

    return found;
}

nlohmann::json SurfaceReader::properties(ui::NodeId node) const {
    if (m_surface == nullptr || !m_surface->nodes.contains(node)) {
        return nlohmann::json::object();
    }

    return m_surface->nodes.at(node).properties;
}

} // namespace workpane::tests
