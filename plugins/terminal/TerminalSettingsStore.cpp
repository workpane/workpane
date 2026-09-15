#include "TerminalSettingsStore.h"

#include "terminal/TerminalThemeCatalog.h"
#include "ui/Components.h"

#include <QJsonObject>

#include <utility>

namespace workpane::plugins::terminalplugin {

class TerminalSettingsStoreHelper final {
  public:
    static TerminalSettings settingsFromDocument(const QJsonObject& document, const QString& declaredFontFamily);
    static QJsonObject settingsDocument(const TerminalSettings& settings);
};

TerminalSettings TerminalSettingsStoreHelper::settingsFromDocument(const QJsonObject& document, const QString& declaredFontFamily) {
    TerminalSettings settings;
    settings.fontFamily = declaredFontFamily;
    const TerminalSettings declared = settings;
    plugins::SettingsReader reader(document);
    reader.readText(QStringLiteral("fontFamily"), settings.fontFamily);
    reader.readInteger(QStringLiteral("fontSize"), settings.fontSize);
    reader.readText(QStringLiteral("themeId"), settings.themeId);
    reader.readBool(QStringLiteral("confirmMultilinePaste"), settings.confirmMultilinePaste);
    reader.readBool(QStringLiteral("allowClipboardWrite"), settings.allowClipboardWrite);

    if (!ui::Components::monospacedFontFamilies().contains(settings.fontFamily)) {
        settings.fontFamily = declared.fontFamily;
    }

    if (!ui::ContentFontSizes::validContentFontSize(settings.fontSize)) {
        settings.fontSize = declared.fontSize;
    }

    if (!terminalcore::TerminalThemes::terminalThemeExists(settings.themeId)) {
        settings.themeId = declared.themeId;
    }

    return settings;
}

QJsonObject TerminalSettingsStoreHelper::settingsDocument(const TerminalSettings& settings) {
    return {{QStringLiteral("fontFamily"), settings.fontFamily}, {QStringLiteral("fontSize"), settings.fontSize}, {QStringLiteral("themeId"), settings.themeId}, {QStringLiteral("confirmMultilinePaste"), settings.confirmMultilinePaste}, {QStringLiteral("allowClipboardWrite"), settings.allowClipboardWrite}};
}

TerminalSettingsStore::TerminalSettingsStore(PluginHost& host, QObject* parent) : QObject(parent), m_host(host) {}

Result<void> TerminalSettingsStore::initialize() {
    m_settings = TerminalSettingsStoreHelper::settingsFromDocument(m_host.settings(), ui::Components::defaultMonospacedFontFamily());
    m_committedSettings = m_settings;
    return Result<void>::success();
}

const QString& TerminalSettingsStore::fontFamily() const {
    return m_settings.fontFamily;
}

int TerminalSettingsStore::fontSize() const {
    return m_settings.fontSize;
}

const QString& TerminalSettingsStore::themeId() const {
    return m_settings.themeId;
}

bool TerminalSettingsStore::confirmMultilinePaste() const {
    return m_settings.confirmMultilinePaste;
}

bool TerminalSettingsStore::allowClipboardWrite() const {
    return m_settings.allowClipboardWrite;
}

void TerminalSettingsStore::setFontFamily(const QString& family) {
    if (family == m_settings.fontFamily || !ui::Components::monospacedFontFamilies().contains(family)) {
        return;
    }

    auto next = m_settings;
    next.fontFamily = family;
    apply(std::move(next));
    emit fontChanged(m_settings.fontFamily);
}

void TerminalSettingsStore::setFontSize(int pointSize) {
    if (pointSize == m_settings.fontSize || !ui::ContentFontSizes::validContentFontSize(pointSize)) {
        return;
    }

    auto next = m_settings;
    next.fontSize = pointSize;
    apply(std::move(next));
    emit fontSizeChanged(m_settings.fontSize);
}

void TerminalSettingsStore::stepFontSize(ui::ContentFontStep step) {
    if (step == ui::ContentFontStep::Reset) {
        setFontSize(defaultTerminalFontSize);
        return;
    }

    setFontSize(ui::ContentFontSizes::steppedContentFontSize(m_settings.fontSize, step == ui::ContentFontStep::Increase ? 1 : -1));
}

void TerminalSettingsStore::setThemeId(const QString& themeId) {
    if (themeId == m_settings.themeId || !terminalcore::TerminalThemes::terminalThemeExists(themeId)) {
        return;
    }

    auto next = m_settings;
    next.themeId = themeId;
    apply(std::move(next));
    emit themeChanged(m_settings.themeId);
}

void TerminalSettingsStore::setConfirmMultilinePaste(bool enabled) {
    if (enabled == m_settings.confirmMultilinePaste) {
        return;
    }

    auto next = m_settings;
    next.confirmMultilinePaste = enabled;
    apply(std::move(next));
    emit confirmMultilinePasteChanged(m_settings.confirmMultilinePaste);
}

void TerminalSettingsStore::setAllowClipboardWrite(bool enabled) {
    if (enabled == m_settings.allowClipboardWrite) {
        return;
    }

    auto next = m_settings;
    next.allowClipboardWrite = enabled;
    apply(std::move(next));
    emit allowClipboardWriteChanged(m_settings.allowClipboardWrite);
}

void TerminalSettingsStore::apply(TerminalSettings next) {
    m_settings = next;

    const quint64 revision = ++m_revision;
    auto future = m_host.saveSettings(TerminalSettingsStoreHelper::settingsDocument(next));
    // clang-format off
    future.then(this, [this, next, revision](Result<void> result) {
        if (result.hasValue()) {
            m_committedSettings = next;
            return;
        }
        if (revision != m_revision) {
            return;
        }
        m_settings = m_committedSettings;
        m_host.notify(m_host.translate(QStringLiteral("terminal.plugin.title")), m_host.translate(QStringLiteral("terminal.error.settings-save-message")), plugins::AlertSeverity::Error);
        emitAll();
    });
    // clang-format on
}

void TerminalSettingsStore::emitAll() {
    emit fontChanged(m_settings.fontFamily);
    emit fontSizeChanged(m_settings.fontSize);
    emit themeChanged(m_settings.themeId);
    emit confirmMultilinePasteChanged(m_settings.confirmMultilinePaste);
    emit allowClipboardWriteChanged(m_settings.allowClipboardWrite);
}

} // namespace workpane::plugins::terminalplugin
