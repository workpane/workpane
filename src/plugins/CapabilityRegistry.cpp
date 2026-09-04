#include "plugins/CapabilityRegistry.h"

#include <algorithm>

namespace workpane::plugins {

Result<void> CapabilityRegistry::provide(const QString& pluginId, const CapabilityDescriptor& descriptor) {
    if (pluginId.isEmpty()) {
        return Result<void>::failure({"capability_provider_invalid", "The capability provider is invalid", descriptor.name});
    }
    if (!CapabilityNames::validCapabilityName(descriptor.name)) {
        return Result<void>::failure({"capability_name_invalid", "The capability name is invalid", descriptor.name});
    }
    if (descriptor.version < 1) {
        return Result<void>::failure({"capability_version_invalid", "The capability version is invalid", descriptor.name});
    }
    if (m_entries.contains(descriptor.name)) {
        return Result<void>::failure({"capability_already_provided", "The capability is already provided", descriptor.name});
    }

    m_entries.insert(descriptor.name, Entry{descriptor.version, pluginId});
    return Result<void>::success();
}

void CapabilityRegistry::clear() {
    m_entries.clear();
}

bool CapabilityRegistry::contains(const QString& name) const {
    return m_entries.contains(name);
}

QString CapabilityRegistry::provider(const QString& name) const {
    const auto entry = m_entries.constFind(name);
    return entry == m_entries.constEnd() ? QString{} : entry.value().provider;
}

std::optional<int> CapabilityRegistry::version(const QString& name) const {
    const auto entry = m_entries.constFind(name);
    return entry == m_entries.constEnd() ? std::nullopt : std::optional<int>(entry.value().version);
}

QStringList CapabilityRegistry::names() const {
    QStringList names = m_entries.keys();
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace workpane::plugins
