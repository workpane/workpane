#include "TerminalFailure.h"

#include <QHash>

namespace workpane::plugins::terminalplugin {

QString TerminalFailures::terminalFailureMessage(const Error& error, PluginHost& host) {
    static const QHash<QString, QString> keys{
        {QStringLiteral("terminal_input_queue_full"), QStringLiteral("terminal.error.input-queue-full")}, {QStringLiteral("terminal_not_running"), QStringLiteral("terminal.error.not-running")}, {QStringLiteral("shell_not_executable"), QStringLiteral("terminal.error.shell-not-executable")}, {QStringLiteral("terminal_working_directory_missing"), QStringLiteral("terminal.error.workdir-missing")}, {QStringLiteral("terminal_spawn_failed"), QStringLiteral("terminal.error.spawn-failed")}, {QStringLiteral("terminal_thread_unavailable"), QStringLiteral("terminal.error.thread-unavailable")}, {QStringLiteral("terminal_backend_failed"), QStringLiteral("terminal.error.backend-failed")}, {QStringLiteral("terminal_workspace_invalid"), QStringLiteral("terminal.error.workspace-invalid")},
    };
    const QString key = keys.value(error.code);

    // A fault of the emulator names no condition the reader can answer, so it reads as one sentence while its own text goes to the log.
    return host.translate(key.isEmpty() ? QStringLiteral("terminal.error.unexpected") : key);
}

} // namespace workpane::plugins::terminalplugin
