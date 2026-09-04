#include "terminal/platform/posix/PosixShellIntegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <array>
#include <utility>

namespace workpane::terminalcore {

class PosixShellIntegrationHelper final {
  public:
    static Result<void> writeStartupFile(const QString& path, const QByteArray& contents);
    static Result<void> configureZsh(const QString& historyFile, QProcessEnvironment& environment);
};

Result<void> PosixShellIntegrationHelper::writeStartupFile(const QString& path, const QByteArray& contents) {
    QFile existing(path);

    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == contents) {
        return Result<void>::success();
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);

    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        file.cancelWriting();
        return Result<void>::failure({"shell_integration_write_failed", "The shell integration could not be written", file.errorString()});
    }

    if (!file.commit()) {
        return Result<void>::failure({"shell_integration_commit_failed", "The shell integration could not be committed", file.errorString()});
    }

    return Result<void>::success();
}

Result<void> PosixShellIntegrationHelper::configureZsh(const QString& historyFile, QProcessEnvironment& environment) {
    const QDir historyDirectory = QFileInfo(historyFile).dir();
    const QString integrationDirectory = historyDirectory.filePath(QStringLiteral("zsh"));

    if (!QDir().mkpath(integrationDirectory)) {
        return Result<void>::failure({"shell_integration_directory_failed", "The shell integration directory is unavailable", integrationDirectory});
    }

    const QString userZdotdir = environment.value(QStringLiteral("ZDOTDIR"), QDir::homePath());
    environment.insert(QStringLiteral("WORKPANE_HISTORY_FILE"), historyFile);
    environment.insert(QStringLiteral("WORKPANE_USER_ZDOTDIR"), userZdotdir);
    environment.insert(QStringLiteral("WORKPANE_ZDOTDIR"), integrationDirectory);
    environment.insert(QStringLiteral("ZDOTDIR"), integrationDirectory);

    const QByteArray zshenv = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zshenv\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zshenv\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\nsetopt RCS\n");
    const QByteArray zprofile = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zprofile\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zprofile\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\n");
    const QByteArray zshrc = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zshrc\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zshrc\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\nunsetopt SHARE_HISTORY INC_APPEND_HISTORY_TIME\nsetopt INC_APPEND_HISTORY\nfc -p \"${WORKPANE_HISTORY_FILE}\" 10000 10000\n");
    const QByteArray zlogin = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zlogin\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zlogin\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\n");
    const QByteArray zlogout = QByteArrayLiteral("if [[ -r \"${WORKPANE_USER_ZDOTDIR}/.zlogout\" ]]; then\n    ZDOTDIR=\"${WORKPANE_USER_ZDOTDIR}\"\n    source \"${WORKPANE_USER_ZDOTDIR}/.zlogout\"\nfi\nZDOTDIR=\"${WORKPANE_ZDOTDIR}\"\n");

    const std::array files = {std::pair{QStringLiteral(".zshenv"), zshenv}, std::pair{QStringLiteral(".zprofile"), zprofile}, std::pair{QStringLiteral(".zshrc"), zshrc}, std::pair{QStringLiteral(".zlogin"), zlogin}, std::pair{QStringLiteral(".zlogout"), zlogout}};

    for (const auto& [name, contents] : files) {
        const auto result = writeStartupFile(QDir(integrationDirectory).filePath(name), contents);
        if (!result.hasValue()) {
            return result;
        }
    }

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
