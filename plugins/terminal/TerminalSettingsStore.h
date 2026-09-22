#pragma once

#include "plugins/PluginInterface.h"
#include "ui/ApplicationShortcuts.h"

#include <QObject>

namespace workpane::plugins::terminalplugin {

inline constexpr int defaultTerminalFontSize = 10;

struct TerminalSettings final {
    QString fontFamily;
    int fontSize{defaultTerminalFontSize};
    QString themeId{QStringLiteral("balanced")};
    // A program writing to the clipboard is refused until the user allows it, because anything printed to the terminal could ask for it.
    bool allowClipboardWrite{false};
    bool openLinksInSystemBrowser{false};
};

class TerminalSettingsStore final : public QObject {
    Q_OBJECT

  public:
    explicit TerminalSettingsStore(PluginHost& host, QObject* parent = nullptr);

    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] const QString& fontFamily() const;
    [[nodiscard]] int fontSize() const;
    [[nodiscard]] const QString& themeId() const;
    [[nodiscard]] bool allowClipboardWrite() const;
    [[nodiscard]] bool openLinksInSystemBrowser() const;
    void setFontFamily(const QString& family);
    void setFontSize(int pointSize);
    void stepFontSize(ui::ContentFontStep step);
    void setThemeId(const QString& themeId);
    void setAllowClipboardWrite(bool enabled);
    void setOpenLinksInSystemBrowser(bool enabled);

  signals:
    void fontChanged(const QString& family);
    void fontSizeChanged(int pointSize);
    void themeChanged(const QString& themeId);
    void allowClipboardWriteChanged(bool enabled);
    void openLinksInSystemBrowserChanged(bool enabled);

  private:
    void apply(TerminalSettings next);
    void emitAll();

    PluginHost& m_host;
    TerminalSettings m_settings;
    TerminalSettings m_committedSettings;
    quint64 m_revision{0};
};

} // namespace workpane::plugins::terminalplugin
