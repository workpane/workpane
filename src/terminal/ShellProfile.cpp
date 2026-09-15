#include "terminal/ShellProfile.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>

namespace workpane::terminalcore {

#ifdef Q_OS_WIN

#endif

class ShellProfileHelper final {
  public:
    static ShellProfile createProfile(const QString& executable);
    static QString quotePosixPath(QString path);
    static QString quotePowerShellPath(QString path);
    static QString quoteCommandPromptPath(const QString& path);
};

ShellProfile ShellProfileHelper::createProfile(const QString& executable) {
    const QFileInfo info(executable);
    return {info.baseName().toLower(), info.baseName(), info.absoluteFilePath(), {}};
}

QString ShellProfileHelper::quotePosixPath(QString path) {
    path.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + path + QLatin1Char('\'');
}

QString ShellProfileHelper::quotePowerShellPath(QString path) {
    path.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + path + QLatin1Char('\'');
}

QString ShellProfileHelper::quoteCommandPromptPath(const QString& path) {
    return QLatin1Char('"') + QDir::toNativeSeparators(path) + QLatin1Char('"');
}

QString ShellPaths::formatLocalPathsForShell(const ShellProfile& profile, const QStringList& paths) {
    QStringList quotedPaths;
    quotedPaths.reserve(paths.size());

#ifdef Q_OS_WIN

    if (profile.id == QStringLiteral("cmd")) {
        for (const auto& path : paths) {
            quotedPaths.append(ShellProfileHelper::quoteCommandPromptPath(path));
        }
    } else {
        for (const auto& path : paths) {
            quotedPaths.append(ShellProfileHelper::quotePowerShellPath(path));
        }
    }

#else
    Q_UNUSED(profile)

    for (const auto& path : paths) {
        quotedPaths.append(ShellProfileHelper::quotePosixPath(path));
    }

#endif

    return quotedPaths.join(QLatin1Char(' ')) + QLatin1Char(' ');
}

ShellProfile ShellProfileResolver::systemDefault() {
#ifdef Q_OS_WIN
    const QString powerShell = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));

    if (!powerShell.isEmpty()) {
        return ShellProfileHelper::createProfile(powerShell);
    }

    const QString windowsPowerShell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));

    if (!windowsPowerShell.isEmpty()) {
        return ShellProfileHelper::createProfile(windowsPowerShell);
    }

    return ShellProfileHelper::createProfile(QDir::toNativeSeparators(qEnvironmentVariable("COMSPEC")));
#else
    const QString environmentShell = qEnvironmentVariable("SHELL");

    if (QFileInfo(environmentShell).isExecutable()) {
        return ShellProfileHelper::createProfile(environmentShell);
    }

    // The account is read into a record of our own, because the one getpwuid returns is shared by the whole process.
    passwd account{};
    passwd* found = nullptr;
    std::array<char, 4096> buffer{};

    if (::getpwuid_r(::getuid(), &account, buffer.data(), buffer.size(), &found) == 0 && found != nullptr && found->pw_shell != nullptr) {
        const QString accountShell = QString::fromLocal8Bit(found->pw_shell);
        if (QFileInfo(accountShell).isExecutable()) {
            return ShellProfileHelper::createProfile(accountShell);
        }
    }

    return ShellProfileHelper::createProfile(QStringLiteral("/bin/sh"));
#endif
}

QList<ShellProfile> ShellProfileResolver::availableProfiles() {
    QList<ShellProfile> profiles;
    // clang-format off
    const auto appendUnique = [&profiles](const QString& path) {
        if (path.isEmpty()) {
            return;
        }
        const auto profile = ShellProfileHelper::createProfile(path);
        const auto existing = std::ranges::find(profiles, profile.executable, &ShellProfile::executable);
        if (existing == profiles.end()) {
            profiles.append(profile);
        }
    };
    // clang-format on

    appendUnique(systemDefault().executable);
#ifdef Q_OS_WIN
    appendUnique(QStandardPaths::findExecutable(QStringLiteral("pwsh.exe")));
    appendUnique(QStandardPaths::findExecutable(QStringLiteral("powershell.exe")));
    appendUnique(QStandardPaths::findExecutable(QStringLiteral("cmd.exe")));
#else
    appendUnique(QStandardPaths::findExecutable(QStringLiteral("zsh")));
    appendUnique(QStandardPaths::findExecutable(QStringLiteral("bash")));
    appendUnique(QStandardPaths::findExecutable(QStringLiteral("fish")));
    appendUnique(QStandardPaths::findExecutable(QStringLiteral("nu")));
#endif
    return profiles;
}

} // namespace workpane::terminalcore
