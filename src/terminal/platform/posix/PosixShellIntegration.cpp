#include "terminal/platform/posix/PosixShellIntegration.h"

#include "terminal/ShellIntegrationFiles.h"

#include <QDir>
#include <QFileInfo>

namespace workpane::terminalcore {

class PosixShellIntegrationHelper final {
  public:
    [[nodiscard]] static Result<void> configureZsh(const QString& historyFile, QProcessEnvironment& environment);
};

Result<void> PosixShellIntegrationHelper::configureZsh(const QString& historyFile, QProcessEnvironment& environment) {
    const QString userZdotdir = environment.value(QStringLiteral("ZDOTDIR"), QDir::homePath());

    // Relocating the configuration inside this file is what zsh documents, so the directory it leaves is where the reader really keeps the rest.
    const QByteArray zshenv = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zshenv\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zshenv\"\n    WORKPANE_USER_ZDOTDIR=\"${ZDOTDIR}\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\nsetopt RCS\n");
    const QByteArray zprofile = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zprofile\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zprofile\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\n");
    const QByteArray zshrc = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zshrc\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zshrc\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\nunsetopt SHARE_HISTORY INC_APPEND_HISTORY_TIME\nsetopt INC_APPEND_HISTORY\nfc -p \"${WORKPANE_HISTORY_FILE}\" 10000 10000\n");
    const QByteArray zlogin = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zlogin\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zlogin\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\n");
    const QByteArray zlogout = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zlogout\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zlogout\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\n");

    const auto written = ShellIntegrationFiles::write(QFileInfo(historyFile).dir().path(), QStringLiteral("zsh"), {{QStringLiteral(".zshenv"), zshenv}, {QStringLiteral(".zprofile"), zprofile}, {QStringLiteral(".zshrc"), zshrc}, {QStringLiteral(".zlogin"), zlogin}, {QStringLiteral(".zlogout"), zlogout}});

    if (!written.hasValue()) {
        return Result<void>::failure(written.error());
    }

    environment.insert(QStringLiteral("WORKPANE_HISTORY_FILE"), historyFile);
    environment.insert(QStringLiteral("WORKPANE_USER_ZDOTDIR"), userZdotdir);
    environment.insert(QStringLiteral("WORKPANE_ZDOTDIR"), written.value());
    environment.insert(QStringLiteral("ZDOTDIR"), written.value());
    return Result<void>::success();
}

Result<void> PosixShellIntegration::configure(const ShellProfile& profile, const QString& historyFile, QProcessEnvironment& environment) {
    environment.insert(QStringLiteral("HISTFILE"), historyFile);

    if (profile.id == QStringLiteral("zsh")) {
        return PosixShellIntegrationHelper::configureZsh(historyFile, environment);
    }

    return Result<void>::success();
}

} // namespace workpane::terminalcore
