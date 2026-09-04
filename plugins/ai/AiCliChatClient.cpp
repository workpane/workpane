#include "AiCliChatClient.h"

#include "AiProviderCatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QStandardPaths>

#include <utility>

namespace workpane::plugins::ai {

constexpr qint64 charactersPerEstimatedToken = 4;

class AiCliChatClientHelper final {
  public:
    static QString roleHeading(const QString& role);
    static const QStringList& searchDirectoryNames();
    static qint64 estimatedTokens(const QString& text);
};

QString AiCliChatClientHelper::roleHeading(const QString& role) {
    if (role == QStringLiteral("assistant")) {
        return QStringLiteral("Assistant");
    }
    if (role == QStringLiteral("system")) {
        return QStringLiteral("Instructions");
    }

    return QStringLiteral("User");
}

const QStringList& AiCliChatClientHelper::searchDirectoryNames() {
    // clang-format off
    static const QStringList directories{QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin"), QStringLiteral("/opt/local/bin"), QStringLiteral("/snap/bin"), QStringLiteral(".local/bin"), QStringLiteral(".bun/bin"), QStringLiteral(".deno/bin"), QStringLiteral(".cargo/bin"), QStringLiteral(".npm-global/bin"), QStringLiteral("AppData/Local/Programs"), QStringLiteral("AppData/Roaming/npm")};
    // clang-format on
    return directories;
}

QStringList CommandLineAgents::commandLineSearchDirectories() {
    QStringList resolved;

    for (const auto& directory : AiCliChatClientHelper::searchDirectoryNames()) {
        const QString absolute = QDir::isAbsolutePath(directory) ? directory : QDir(QDir::homePath()).filePath(directory);

        if (QFileInfo(absolute).isDir()) {
            resolved.append(absolute);
        }
    }

    return resolved;
}

QString CommandLineAgents::resolveCommandLineProgram(const QString& program) {
    const QString onPath = QStandardPaths::findExecutable(program);

    return onPath.isEmpty() ? QStandardPaths::findExecutable(program, CommandLineAgents::commandLineSearchDirectories()) : onPath;
}

QStringList CommandLineAgents::commandLineArguments(const CommandLineDescriptor& descriptor, const QString& prompt, const QString& workdir, const QString& model) {
    QStringList arguments;
    arguments.reserve(descriptor.arguments.size());

    for (const auto& argument : descriptor.arguments) {
        if (argument == QString::fromLatin1(commandLinePromptPlaceholder)) {
            arguments.append(prompt);
            continue;
        }

        if (argument == QString::fromLatin1(commandLineWorkdirPlaceholder)) {
            arguments.append(workdir);
            continue;
        }

        if (argument == QString::fromLatin1(commandLineModelPlaceholder)) {
            arguments.append(model);
            continue;
        }

        arguments.append(argument);
    }

    return arguments;
}

// No command line agent reports what it spent, so a token is counted as the four characters one averages and the number is an estimate rather than a measurement.
qint64 AiCliChatClientHelper::estimatedTokens(const QString& text) {
    return static_cast<qint64>(text.size()) / charactersPerEstimatedToken;
}

QString CommandLineAgents::renderConversationPrompt(const QJsonArray& messages) {
    QStringList rendered;

    for (const auto& value : messages) {
        const QJsonObject message = value.toObject();
        const QString content = message.value(QStringLiteral("content")).toString();

        if (content.isEmpty()) {
            continue;
        }

        rendered.append(QStringLiteral("## %1\n\n%2").arg(AiCliChatClientHelper::roleHeading(message.value(QStringLiteral("role")).toString()), content));
    }

    return rendered.join(QStringLiteral("\n\n"));
}

AiCliChatClient::AiCliChatClient(QObject* parent) : AiCliChatClient(CommandLineAgents::resolveCommandLineProgram, parent) {}

AiCliChatClient::AiCliChatClient(CommandLineResolver resolver, QObject* parent) : AiChatClient(parent), m_resolver(std::move(resolver)) {
    // clang-format off
    connect(&m_runner, &AiCommandRunner::outputReceived, this, [this](const QString& text) { emit contentReceived(text); });
    connect(&m_runner, &AiCommandRunner::finished, this, [this](int exitCode, const QString& output) { completeRun(exitCode, output); });
    connect(&m_runner, &AiCommandRunner::failed, this, [this](const Error& error) { m_running = false; emit failed({error.code, CommandOutput::commandFailureMessage(error, m_translate), error.detail.isEmpty() ? error.message : error.detail}); });
    // clang-format on
}

void AiCliChatClient::send(const ChatRequest& request, const std::function<QString(const QString&)>& translate) {
    m_translate = translate;
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(request.connection.providerId);

    if (provider == nullptr || provider->protocol != WireProtocol::CommandLine) {
        emit failed({"ai_cli_provider_invalid", translate(QStringLiteral("ai.error.cli-provider-invalid")), request.connection.providerId});
        return;
    }

    if (request.workdir.isEmpty()) {
        emit failed({"ai_cli_workdir_required", translate(QStringLiteral("ai.error.cli-workdir-required")), provider->id});
        return;
    }

    const QString program = m_resolver(provider->commandLine.program);

    if (program.isEmpty()) {
        emit failed({"ai_cli_program_missing", translate(QStringLiteral("ai.error.cli-program-missing")), provider->commandLine.program});
        return;
    }

    const QString prompt = CommandLineAgents::renderConversationPrompt(request.messages);
    m_prompt = prompt;
    const QStringList arguments = CommandLineAgents::commandLineArguments(provider->commandLine, prompt, request.workdir, request.connection.modelId);
    m_running = true;
    emit started();
    emit requestSent(program, arguments.join(QLatin1Char('\n')));
    m_runner.startProgram(program, arguments, request.workdir, 0, provider->commandLine.clearedVariables);
}

// A command line agent runs its own tools, so its run is one turn that ends when the program does.
void AiCliChatClient::completeRun(int exitCode, const QString& output) {
    m_running = false;

    if (exitCode != 0) {
        // A program that refuses its own arguments prints the reason and nothing else, so the exit code is the only reason left when it printed nothing.
        const QString printed = output.trimmed();
        const QString reason = printed.isEmpty() ? m_translate(QStringLiteral("ai.error.exit-code")).arg(QString::number(exitCode)) : printed;
        emit failed({"ai_cli_failed", reason, QString::number(exitCode)});
        return;
    }

    // A command line agent reports no usage, so what it was given and what it answered are counted at the four characters a token averages.
    const QString answer = output.trimmed();
    const ChatUsage estimated{AiCliChatClientHelper::estimatedTokens(m_prompt), AiCliChatClientHelper::estimatedTokens(answer)};
    emit finished(answer, {}, estimated, QStringLiteral("stop"));
}

void AiCliChatClient::cancel() {
    m_running = false;
    m_runner.cancel();
}

bool AiCliChatClient::running() const {
    return m_running;
}

} // namespace workpane::plugins::ai
