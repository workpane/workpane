#include "AiCommandRunner.h"

#include <QDir>
#include <QHash>

namespace workpane::plugins::ai {

constexpr qsizetype maximumOutputCharacters = 1 << 22;
constexpr int terminationGraceMs = 3000;

class AiCommandRunnerHelper final {
  public:
    static QStringList shellArguments(const QString& command);
    static void killProcessTree(QProcess* process);
    static void requestTermination(QProcess* process);
    static QString shellExecutable();
    static bool isFinalCsiByte(QChar character);
};

QString CommandOutput::commandFailureMessage(const Error& error, const std::function<QString(const QString&)>& translate) {
    static const QHash<QString, QString> keys{
        {QStringLiteral("ai_command_timeout"), QStringLiteral("ai.error.command-timeout")}, {QStringLiteral("ai_command_output_too_large"), QStringLiteral("ai.error.command-output-too-large")}, {QStringLiteral("ai_command_workdir_invalid"), QStringLiteral("ai.error.command-workdir-invalid")}, {QStringLiteral("ai_command_failed"), QStringLiteral("ai.error.command-start-failed")}, {QStringLiteral("ai_command_crashed"), QStringLiteral("ai.error.command-crashed")},
    };
    const QString key = keys.value(error.code);

    // A condition the reader cannot reach from the interface keeps its diagnostic, which is where it belongs.
    return key.isEmpty() ? error.message : translate(key);
}

QStringList AiCommandRunnerHelper::shellArguments(const QString& command) {
#ifdef Q_OS_WIN
    return {QStringLiteral("/c"), command};
#else
    return {QStringLiteral("-lc"), command};
#endif
}

// Windows terminates only the shell it started, so the whole tree is taken down by its identity, otherwise the command keeps running after the task was stopped.
void AiCommandRunnerHelper::killProcessTree(QProcess* process) {
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("taskkill"), {QStringLiteral("/T"), QStringLiteral("/F"), QStringLiteral("/PID"), QString::number(process->processId())});
#endif
    process->kill();
}

// A console process on Windows has no window to close, so there is nothing for a graceful request to reach and the stop is the tree kill itself.
void AiCommandRunnerHelper::requestTermination(QProcess* process) {
#ifdef Q_OS_WIN
    killProcessTree(process);
#else
    process->terminate();
    // clang-format off
    QTimer::singleShot(terminationGraceMs, process, [process]() { if (process->state() != QProcess::NotRunning) { killProcessTree(process); } });
    // clang-format on
#endif
}

QString AiCommandRunnerHelper::shellExecutable() {
#ifdef Q_OS_WIN
    return QStringLiteral("cmd.exe");
#else
    return QStringLiteral("/bin/sh");
#endif
}

bool AiCommandRunnerHelper::isFinalCsiByte(QChar character) {
    return character.unicode() >= 0x40 && character.unicode() <= 0x7E;
}

QString CommandOutput::plainCommandOutput(QString& pending, const QString& chunk) {
    const QString source = pending + chunk;
    pending.clear();

    QString plain;
    plain.reserve(source.size());
    qsizetype index = 0;

    while (index < source.size()) {
        const QChar character = source.at(index);
        if (character == QLatin1Char('\x1b')) {
            if (index + 1 >= source.size()) {
                pending = source.mid(index);
                break;
            }

            const QChar introducer = source.at(index + 1);
            if (introducer == QLatin1Char('[')) {
                qsizetype end = index + 2;
                while (end < source.size() && !AiCommandRunnerHelper::isFinalCsiByte(source.at(end))) {
                    ++end;
                }
                if (end >= source.size()) {
                    pending = source.mid(index);
                    break;
                }
                index = end + 1;
                continue;
            }

            if (introducer == QLatin1Char(']')) {
                qsizetype end = index + 2;
                while (end < source.size() && source.at(end) != QLatin1Char('\a') && !(source.at(end) == QLatin1Char('\x1b') && end + 1 < source.size() && source.at(end + 1) == QLatin1Char('\\'))) {
                    ++end;
                }
                if (end >= source.size()) {
                    pending = source.mid(index);
                    break;
                }
                index = source.at(end) == QLatin1Char('\a') ? end + 1 : end + 2;
                continue;
            }

            index += 2;
            continue;
        }

        if (character == QLatin1Char('\r')) {
            if (index + 1 >= source.size()) {
                pending = source.mid(index);
                break;
            }
            if (source.at(index + 1) != QLatin1Char('\n')) {
                plain.append(QLatin1Char('\n'));
            }
            ++index;
            continue;
        }

        if (character.unicode() < 0x20 && character != QLatin1Char('\n') && character != QLatin1Char('\t')) {
            ++index;
            continue;
        }

        plain.append(character);
        ++index;
    }

    return plain;
}

AiCommandRunner::AiCommandRunner(QObject* parent) : QObject(parent) {
    m_timeout.setSingleShot(true);
    // clang-format off
    connect(&m_timeout, &QTimer::timeout, this, [this]() { m_timedOut = true; reportFailure({"ai_command_timeout", "The command exceeded its time limit", {}}); stopProcess(); });
    // clang-format on
}

AiCommandRunner::~AiCommandRunner() {
    if (m_process == nullptr) {
        return;
    }

    m_process->disconnect(this);
    m_process->setParent(nullptr);
    connect(m_process, &QProcess::finished, m_process, &QObject::deleteLater);
    AiCommandRunnerHelper::killProcessTree(m_process);
    m_process = nullptr;
}

bool AiCommandRunner::running() const {
    return m_process != nullptr;
}

void AiCommandRunner::startProgram(const QString& program, const QStringList& arguments, const QString& workdir, int timeoutSeconds, const QStringList& clearedVariables) {
    if (program.trimmed().isEmpty()) {
        reportFailure({"ai_command_invalid", "The program is required", {}});
        return;
    }

    launch(program, arguments, workdir, timeoutSeconds, clearedVariables);
}

void AiCommandRunner::start(const QString& command, const QString& workdir, int timeoutSeconds) {
    if (command.trimmed().isEmpty()) {
        reportFailure({"ai_command_invalid", "The command is required", {}});
        return;
    }

    launch(AiCommandRunnerHelper::shellExecutable(), AiCommandRunnerHelper::shellArguments(command), workdir, timeoutSeconds, {});
}

void AiCommandRunner::launch(const QString& program, const QStringList& arguments, const QString& workdir, int timeoutSeconds, const QStringList& clearedVariables) {
    if (m_process != nullptr) {
        reportFailure({"ai_command_busy", "The runner is already running a command", {}});
        return;
    }

    const QDir directory(workdir);

    if (!directory.isAbsolute() || !directory.exists()) {
        reportFailure({"ai_command_workdir_invalid", "The command working directory is unavailable", workdir});
        return;
    }

    if (timeoutSeconds < 0) {
        reportFailure({"ai_command_invalid", "The command time limit is invalid", QString::number(timeoutSeconds)});
        return;
    }

    m_output.clear();
    m_decoder.resetState();
    m_pendingControl.clear();
    m_completed = false;
    m_timedOut = false;

    m_process = new QProcess(this);
    // A credential the child can read is one it may spend, so the variables the provider names are removed from what it inherits.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    for (const auto& name : clearedVariables) {
        environment.remove(name);
    }

    m_process->setProcessEnvironment(environment);
    m_process->setWorkingDirectory(directory.absolutePath());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &AiCommandRunner::readOutput);
    connect(m_process, &QProcess::finished, this, &AiCommandRunner::completeProcess);
    // clang-format off
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { if (m_process != nullptr && !m_timedOut) { const QString message = m_process->errorString(); reportFailure({"ai_command_failed", message, {}}); } });
    // clang-format on

    // A zero time limit means the command runs for as long as it needs.
    if (timeoutSeconds > 0) {
        m_timeout.start(timeoutSeconds * 1000);
    }

    m_process->start(program, arguments);
    // Nothing is ever written to the child, so its input is closed at once rather than left open for a program that waits to be piped to.
    m_process->closeWriteChannel();
}

void AiCommandRunner::cancel() {
    m_completed = true;
    stopProcess();
}

// Termination escalates on a timer because the interactive thread never waits for a process to exit.
void AiCommandRunner::stopProcess() {
    if (m_process == nullptr) {
        return;
    }

    m_timeout.stop();
    QProcess* process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    process->setParent(nullptr);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    AiCommandRunnerHelper::requestTermination(process);
}

void AiCommandRunner::readOutput() {
    if (m_process == nullptr) {
        return;
    }

    const QString chunk = CommandOutput::plainCommandOutput(m_pendingControl, m_decoder(m_process->readAllStandardOutput()));

    if (chunk.isEmpty()) {
        return;
    }

    if (m_output.size() + chunk.size() > maximumOutputCharacters) {
        reportFailure({"ai_command_output_too_large", "The command output exceeded the permitted size", {}});
        stopProcess();
        return;
    }

    m_output.append(chunk);
    emit outputReceived(chunk);
}

void AiCommandRunner::completeProcess(int exitCode, QProcess::ExitStatus status) {
    if (m_process == nullptr) {
        return;
    }

    readOutput();
    m_timeout.stop();
    release();

    if (m_completed) {
        return;
    }

    m_completed = true;

    if (status == QProcess::CrashExit) {
        emit failed({"ai_command_crashed", "The command terminated abnormally", QString::number(exitCode)});
        return;
    }

    emit finished(exitCode, m_output);
}

void AiCommandRunner::release() {
    if (m_process == nullptr) {
        return;
    }

    QProcess* process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    process->deleteLater();
}

void AiCommandRunner::reportFailure(const Error& error) {
    if (m_completed) {
        return;
    }

    m_completed = true;
    emit failed(error);
}

} // namespace workpane::plugins::ai
