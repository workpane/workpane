#include "app/ApplicationSettingsStore.h"

#include "domain/ApplicationLanguage.h"
#include "persistence/CoreDatabaseSchema.h"
#include "persistence/DatabaseExecutor.h"
#include "persistence/StateStore.h"
#include "plugins/PluginInterface.h"
#include "ui/Theme.h"

#include <QJsonObject>
#include <QLocale>

#include <utility>

namespace workpane::app {

constexpr auto applicationSettingsOwner = "workpane";
constexpr auto windowGeometryKey = "windowGeometry";
constexpr auto languageKey = "language";
constexpr auto themeIdKey = "themeId";

class ApplicationSettingsStoreHelper final {
  public:
    static domain::ApplicationSettings settingsFromDocument(const QJsonObject& document, const domain::ApplicationSettings& declared);
    static QJsonObject settingsDocument(const domain::ApplicationSettings& settings);
};

domain::ApplicationSettings ApplicationSettingsStoreHelper::settingsFromDocument(const QJsonObject& document, const domain::ApplicationSettings& declared) {
    domain::ApplicationSettings settings = declared;
    QString geometry;
    QString themeId = declared.themeId;
    plugins::SettingsReader reader(document);
    reader.readText(QString::fromLatin1(windowGeometryKey), geometry);
    reader.readText(QString::fromLatin1(languageKey), settings.language);
    reader.readText(QString::fromLatin1(themeIdKey), themeId);

    settings.windowGeometry = QByteArray::fromBase64(geometry.toLatin1());
    settings.themeId = ui::ThemeManager::instance().catalog().themeOrDefault(themeId).id();

    if (!domain::ApplicationLanguages::isSupportedApplicationLanguage(settings.language)) {
        settings.language = declared.language;
    }

    return settings;
}

QJsonObject ApplicationSettingsStoreHelper::settingsDocument(const domain::ApplicationSettings& settings) {
    return {{QString::fromLatin1(windowGeometryKey), QString::fromLatin1(settings.windowGeometry.toBase64())}, {QString::fromLatin1(languageKey), settings.language}, {QString::fromLatin1(themeIdKey), settings.themeId}};
}

ApplicationSettingsStore::ApplicationSettingsStore(persistence::StateStore& stateStore, persistence::DatabaseExecutor& databaseExecutor, QObject* parent) : QObject(parent), m_stateStore(stateStore), m_databaseExecutor(databaseExecutor) {}

Result<void> ApplicationSettingsStore::initialize() {
    const auto result = m_stateStore.initialize();

    if (!result.hasValue()) {
        return result;
    }

    const domain::ApplicationSettings declared{{}, domain::ApplicationLanguages::resolveApplicationLanguage(QLocale::system().name(QLocale::TagSeparator::Dash)), ui::ThemeManager::instance().catalog().defaultTheme().id()};
    m_settings = ApplicationSettingsStoreHelper::settingsFromDocument(m_stateStore.settings(QString::fromLatin1(applicationSettingsOwner)), declared);
    m_committedSettings = m_settings;
    return Result<void>::success();
}

const QByteArray& ApplicationSettingsStore::windowGeometry() const {
    return m_settings.windowGeometry;
}

const QString& ApplicationSettingsStore::language() const {
    return m_settings.language;
}

const QString& ApplicationSettingsStore::themeId() const {
    return m_settings.themeId;
}

void ApplicationSettingsStore::setWindowGeometry(const QByteArray& geometry) {
    if (geometry.isEmpty() || geometry == m_settings.windowGeometry) {
        return;
    }

    auto next = m_settings;
    next.windowGeometry = geometry;
    save(std::move(next));
}

Result<void> ApplicationSettingsStore::setLanguage(const QString& language) {
    if (!domain::ApplicationLanguages::isSupportedApplicationLanguage(language)) {
        return Result<void>::failure({"application_language_invalid", "The application language is unsupported", language});
    }
    if (language == m_settings.language) {
        return Result<void>::success();
    }

    auto next = m_settings;
    next.language = language;
    save(std::move(next));
    return Result<void>::success();
}

Result<void> ApplicationSettingsStore::setTheme(const QString& themeId) {
    if (!ui::ThemeManager::instance().catalog().contains(themeId)) {
        return Result<void>::failure({"application_theme_invalid", "The application theme is unsupported", themeId});
    }
    if (themeId == m_settings.themeId) {
        return Result<void>::success();
    }

    auto next = m_settings;
    next.themeId = themeId;
    save(std::move(next));
    return Result<void>::success();
}

void ApplicationSettingsStore::save(domain::ApplicationSettings settings) {
    const QString previousLanguage = m_settings.language;
    const QString previousThemeId = m_settings.themeId;
    m_settings = std::move(settings);

    if (m_settings.language != previousLanguage) {
        emit languageChanged(m_settings.language);
    }

    if (m_settings.themeId != previousThemeId) {
        emit themeChanged(m_settings.themeId);
    }

    const auto candidate = m_settings;
    const quint64 revision = ++m_revision;
    auto future = m_databaseExecutor.saveSettings(QString::fromLatin1(applicationSettingsOwner), ApplicationSettingsStoreHelper::settingsDocument(candidate));
    // clang-format off
    future.then(this, [this, candidate, revision](Result<void> result) {
        if (result.hasValue()) {
            if (revision > m_committedRevision) {
                m_committedSettings = candidate;
                m_committedRevision = revision;
            }
            return;
        }
        if (revision != m_revision) {
            return;
        }
        const QString failedLanguage = m_settings.language;
        const QString failedThemeId = m_settings.themeId;
        m_settings = m_committedSettings;
        if (m_settings.language != failedLanguage) {
            emit languageChanged(m_settings.language);
        }
        if (m_settings.themeId != failedThemeId) {
            emit themeChanged(m_settings.themeId);
        }
        emit saveFailed(result.error().message);
    });
    // clang-format on
}

} // namespace workpane::app
