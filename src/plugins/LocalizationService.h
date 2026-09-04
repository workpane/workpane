#pragma once

#include "domain/Result.h"
#include "plugins/PluginInterface.h"

#include <QString>

namespace workpane::plugins {

class LocalizationService final {
  public:
    explicit LocalizationService(QString localeName);

    [[nodiscard]] Result<void> registerCatalog(const QString& pluginId, const TranslationCatalog& catalog);
    void unregisterCatalog(const QString& pluginId);
    [[nodiscard]] Result<void> setLocale(QString localeName);
    [[nodiscard]] QString translate(const QString& key) const;
    [[nodiscard]] const QString& localeName() const;

  private:
    [[nodiscard]] QStringList localeCandidates() const;

    QString m_localeName;
    QHash<QString, TranslationCatalog> m_catalogs;
};

} // namespace workpane::plugins
