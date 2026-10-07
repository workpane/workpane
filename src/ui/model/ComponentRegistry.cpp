#include "ui/model/ComponentRegistry.h"

#include <utility>

namespace workpane::ui {

void ComponentRegistry::add(std::string kind, Factory factory) {
    m_factories.insert_or_assign(std::move(kind), std::move(factory));
}

std::unique_ptr<Component> ComponentRegistry::create(std::string_view kind, NodeId id) const {
    const auto found = m_factories.find(kind);
    return found == m_factories.end() ? nullptr : found->second(id);
}

std::vector<std::string> ComponentRegistry::kinds() const {
    std::vector<std::string> names;
    names.reserve(m_factories.size());

    for (const auto& [kind, factory] : m_factories) {
        names.push_back(kind);
    }

    return names;
}

} // namespace workpane::ui
