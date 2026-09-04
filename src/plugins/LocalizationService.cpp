#include "plugins/LocalizationService.h"

#include <QRegularExpression>
#include <QSet>

#include <utility>

namespace workpane::plugins {

class LocalizationServiceHelper final {
  public:
    static const QRegularExpression& translationKeyPattern();
    static const QRegularExpression& pluginIdPattern();
    static const QRegularExpression& localePattern();
};

const QRegularExpression& LocalizationServiceHelper::translationKeyPattern() {
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*\\.[a-z0-9]+(?:-[a-z0-9]+)*\\.[a-z0-9]+(?:-[a-z0-9]+)*$"));
    return pattern;
}

const QRegularExpression& LocalizationServiceHelper::pluginIdPattern() {
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    return pattern;
}

const QRegularExpression& LocalizationServiceHelper::localePattern() {
    static const QRegularExpression pattern(QStringLiteral("^[a-z]{2,3}(?:-[a-z0-9]{2,8})*$"));
    return pattern;
}

LocalizationService::LocalizationService(QString localeName) : m_localeName(std::move(localeName).replace(QLatin1Char('_'), QLatin1Char('-')).toLower()) {}

Result<void> LocalizationService::registerCatalog(const QString& pluginId, const TranslationCatalog& catalog) {
    if (!LocalizationServiceHelper::pluginIdPattern().match(pluginId).hasMatch() || m_catalogs.contains(pluginId)) {
        return Result<void>::failure({"plugin_translation_catalog_invalid", "The plugin translation catalog is invalid", pluginId});
    }

    if (!catalog.contains(QStringLiteral("en"))) {
        return Result<void>::failure({"plugin_translation_english_missing", "The plugin translation catalog requires English", pluginId});
    }

    const auto& englishEntries = catalog.value(QStringLiteral("en"));

    if (englishEntries.isEmpty()) {
        return Result<void>::failure({"plugin_translation_english_missing", "The plugin translation catalog requires English", pluginId});
    }

    for (auto locale = catalog.cbegin(); locale != catalog.cend(); ++locale) {
        if (!LocalizationServiceHelper::localePattern().match(locale.key()).hasMatch() || locale.key() != locale.key().toLower()) {
            return Result<void>::failure({"plugin_translation_locale_invalid", "A plugin translation locale is invalid", locale.key()});
        }

        for (auto entry = locale.value().cbegin(); entry != locale.value().cend(); ++entry) {
            const QString prefix = pluginId + QLatin1Char('.');
            if (!LocalizationServiceHelper::translationKeyPattern().match(entry.key()).hasMatch() || !entry.key().startsWith(prefix) || entry.value().isEmpty() || (locale.key() != QStringLiteral("en") && !englishEntries.contains(entry.key()))) {
                return Result<void>::failure({"plugin_translation_entry_invalid", "A plugin translation entry is invalid", entry.key()});
            }
        }
    }

    m_catalogs.insert(pluginId, catalog);
    return Result<void>::success();
}

void LocalizationService::unregisterCatalog(const QString& pluginId) {
    m_catalogs.remove(pluginId);
}

Result<void> LocalizationService::setLocale(QString localeName) {
    localeName.replace(QLatin1Char('_'), QLatin1Char('-'));
    localeName = localeName.toLower();

    if (!LocalizationServiceHelper::localePattern().match(localeName).hasMatch()) {
        return Result<void>::failure({"application_locale_invalid", "The application locale is invalid", localeName});
    }

    m_localeName = std::move(localeName);
    return Result<void>::success();
}

QString LocalizationService::translate(const QString& key) const {
    const qsizetype separator = key.indexOf(QLatin1Char('.'));

    if (separator <= 0) {
        return key;
    }

    const auto catalog = m_catalogs.constFind(key.first(separator));

    if (catalog == m_catalogs.cend()) {
        return key;
    }

    for (const auto& locale : localeCandidates()) {
        const auto entries = catalog->constFind(locale);
        if (entries == catalog->cend()) {
            continue;
        }

        const auto translation = entries->constFind(key);
        if (translation != entries->cend()) {
            return translation.value();
        }
    }

    return key;
}

const QString& LocalizationService::localeName() const {
    return m_localeName;
}

QStringList LocalizationService::localeCandidates() const {
    QStringList candidates{m_localeName};
    const qsizetype separator = m_localeName.indexOf(QLatin1Char('-'));

    if (separator > 0) {
        candidates.append(m_localeName.first(separator));
    }

    candidates.append(QStringLiteral("en"));

    QSet<QString> seen;
    QStringList unique;

    for (const auto& candidate : candidates) {
        if (!seen.contains(candidate)) {
            seen.insert(candidate);
            unique.append(candidate);
        }
    }

    return unique;
}

} // namespace workpane::plugins
