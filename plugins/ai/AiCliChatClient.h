#pragma once

#include "AiChatClient.h"
#include "AiCommandRunner.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace workpane::plugins::ai {

// A window opened by the desktop does not inherit the path a shell has, so the install directories of the platform are searched after it.
// Which executable a name resolves to is a property of the running system, so a deterministic run is given one.
using CommandLineResolver = std::function<QString(const QString&)>;

class AiCliChatClient final : public AiChatClient {
    Q_OBJECT

  public:
    explicit AiCliChatClient(QObject* parent = nullptr);
    AiCliChatClient(CommandLineResolver resolver, QObject* parent);

    void send(const ChatRequest& request, const std::function<QString(const QString&)>& translate) override;
    void cancel() override;
    [[nodiscard]] bool running() const override;

  private:
    void completeRun(int exitCode, const QString& output);

    CommandLineResolver m_resolver;
    AiCommandRunner m_runner;
    QString m_prompt;
    std::function<QString(const QString&)> m_translate;
    bool m_running{false};
};

// These agents are invoked without a session, so the conversation they are given is the whole of what they know.
class CommandLineAgents final {
  public:
    [[nodiscard]] static QStringList commandLineSearchDirectories();
    [[nodiscard]] static QString resolveCommandLineProgram(const QString& program);
    [[nodiscard]] static QStringList commandLineArguments(const CommandLineDescriptor& descriptor, const QString& prompt, const QString& workdir, const QString& model);
    [[nodiscard]] static QString renderConversationPrompt(const QJsonArray& messages);
};

} // namespace workpane::plugins::ai
