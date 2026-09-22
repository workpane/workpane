#include "terminal/platform/windows/WindowsShellIntegration.h"

#include "terminal/ShellIntegrationFiles.h"

#include <QDir>
#include <QFileInfo>

namespace workpane::terminalcore {

class WindowsShellIntegrationHelper final {
  public:
    [[nodiscard]] static QByteArray promptScript();
};

// Windows tells nobody which directory another process is standing in, so the shell marks its own with the address this terminal already reads.
QByteArray WindowsShellIntegrationHelper::promptScript() {
    return QByteArrayLiteral("$global:WorkpaneInnerPrompt = $function:prompt\r\n\r\nfunction global:prompt {\r\n    $location = Get-Location\r\n    if ($location.Provider.Name -eq 'FileSystem') {\r\n        $uri = ([System.Uri] $location.ProviderPath).AbsoluteUri\r\n        [Console]::Write(\"$([char]27)]7;$uri$([char]7)\")\r\n    }\r\n    & $global:WorkpaneInnerPrompt\r\n}\r\n");
}

Result<void> WindowsShellIntegration::configure(ShellProfile& profile, const QString& historyFile) {
    if (profile.id != QStringLiteral("pwsh") && profile.id != QStringLiteral("powershell")) {
        return Result<void>::success();
    }

    const auto written = ShellIntegrationFiles::write(QFileInfo(historyFile).dir().path(), QStringLiteral("powershell"), {{QStringLiteral("workpane.ps1"), WindowsShellIntegrationHelper::promptScript()}});

    if (!written.hasValue()) {
        return Result<void>::failure(written.error());
    }

    // The profiles the reader owns are read before this command runs, so the prompt it wraps is the one they configured for themselves.
    profile.arguments.append(QStringLiteral("-NoExit"));
    profile.arguments.append(QStringLiteral("-Command"));
    profile.arguments.append(QStringLiteral(". %1").arg(ShellPaths::quoteForPowerShell(QDir(written.value()).filePath(QStringLiteral("workpane.ps1")))));
    return Result<void>::success();
}

} // namespace workpane::terminalcore
