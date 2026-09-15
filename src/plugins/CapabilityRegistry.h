#pragma once

#include "domain/Result.h"
#include "plugins/PluginCapability.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace workpane::plugins {

// The registry answers which plugin provides a name, so a caller asks for what it wants and never for who does it.
class CapabilityRegistry final {
  public:
    [[nodiscard]] Result<void> provide(const QString& pluginId, const CapabilityDescriptor& descriptor);
    void clear();

    [[nodiscard]] bool contains(const QString& name) const;
    [[nodiscard]] QString provider(const QString& name) const;
    [[nodiscard]] std::optional<int> version(const QString& name) const;
    [[nodiscard]] QStringList names() const;

  private:
    struct Entry final {
        int version{1};
        QString provider;
    };

    QHash<QString, Entry> m_entries;
};

} // namespace workpane::plugins
