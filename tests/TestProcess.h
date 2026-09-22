#pragma once

#include <QString>
#include <QStringList>

namespace workpane::test {

class TestProcesses final {
  public:
    // A command in a test runs through the shell of the running platform, because that is the one the runner starts.
    [[nodiscard]] static inline QString shellExecutable() {
#ifdef Q_OS_WIN
        return QStringLiteral("cmd.exe");
#else
        return QStringLiteral("/bin/sh");
#endif
    }

    [[nodiscard]] static inline QStringList shellArguments(const QString& command) {
#ifdef Q_OS_WIN
        return {QStringLiteral("/c"), command};
#else
        return {QStringLiteral("-c"), command};
#endif
    }

    // The Windows wait uses ping because timeout refuses a redirected console, which is how a test runner starts it.
    [[nodiscard]] static inline QString sleepingCommand(int seconds) {
#ifdef Q_OS_WIN
        return QStringLiteral("ping -n %1 127.0.0.1 > NUL 2>&1").arg(seconds + 1);
#else
        return QStringLiteral("trap '' TERM; sleep %1").arg(seconds);
#endif
    }
};

} // namespace workpane::test
