#include "AiAgentPrompt.h"

#include <QRegularExpression>

namespace workpane::plugins::ai {

constexpr QLatin1StringView promptTagPattern("\\{\\{([A-Z0-9_]+)\\}\\}");

const QVector<PromptTagDescriptor>& AgentPrompts::promptTags() {
    static const QVector<PromptTagDescriptor> tags{{QStringLiteral("SYSTEM_PROMPT_DATA"), QStringLiteral("ai.tag.system-prompt-data")}, {QStringLiteral("AGENT_NAME"), QStringLiteral("ai.tag.agent-name")}, {QStringLiteral("AGENT_DESCRIPTION"), QStringLiteral("ai.tag.agent-description")}, {QStringLiteral("TASK_TITLE"), QStringLiteral("ai.tag.task-title")}, {QStringLiteral("TASK_DESCRIPTION"), QStringLiteral("ai.tag.task-description")}, {QStringLiteral("TASK_PROMPT"), QStringLiteral("ai.tag.task-prompt")}, {QStringLiteral("TASK_WORKDIR"), QStringLiteral("ai.tag.task-workdir")}, {QStringLiteral("TASK_ISSUE_URL"), QStringLiteral("ai.tag.task-issue-url")}, {QStringLiteral("DATE_TIME"), QStringLiteral("ai.tag.date-time")}, {QStringLiteral("DATE_TIME_UTC"), QStringLiteral("ai.tag.date-time-utc")}, {QStringLiteral("TIME_ZONE"), QStringLiteral("ai.tag.time-zone")}, {QStringLiteral("LOCALE"), QStringLiteral("ai.tag.locale")}, {QStringLiteral("LANGUAGE"), QStringLiteral("ai.tag.language")}, {QStringLiteral("OPERATING_SYSTEM"), QStringLiteral("ai.tag.operating-system")}, {QStringLiteral("USER_NAME"), QStringLiteral("ai.tag.user-name")}, {QStringLiteral("HOME_DIRECTORY"), QStringLiteral("ai.tag.home-directory")}, {QStringLiteral("TOOLS"), QStringLiteral("ai.tag.tools")}, {QStringLiteral("SKILLS"), QStringLiteral("ai.tag.skills")}, {QStringLiteral("CONTEXT_FILES"), QStringLiteral("ai.tag.context-files")}, {QStringLiteral("MODEL"), QStringLiteral("ai.tag.model")}, {QStringLiteral("MODEL_TRAITS"), QStringLiteral("ai.tag.model-traits")}, {QStringLiteral("VISION"), QStringLiteral("ai.tag.vision")}, {QStringLiteral("SEARCH"), QStringLiteral("ai.tag.search")}, {QStringLiteral("SPEECH"), QStringLiteral("ai.tag.speech")}, {QStringLiteral("SERVERS"), QStringLiteral("ai.tag.servers")}, {QStringLiteral("CONTEXT_WINDOW"), QStringLiteral("ai.tag.context-window")}, {QStringLiteral("OUTPUT_BUDGET"), QStringLiteral("ai.tag.output-budget")}};
    return tags;
}

// A tag nobody declares is refused where the prompt is written, so no run ever meets one it cannot answer.
QStringList AgentPrompts::unknownPromptTags(const QString& prompt) {
    QStringList declared;

    for (const auto& tag : AgentPrompts::promptTags()) {
        declared.append(tag.name);
    }

    QStringList unknown;
    const QRegularExpression pattern{promptTagPattern};
    QRegularExpressionMatchIterator matches = pattern.globalMatch(prompt);

    while (matches.hasNext()) {
        const QString name = matches.next().captured(1);
        if (!declared.contains(name) && !unknown.contains(name)) {
            unknown.append(name);
        }
    }

    return unknown;
}

QString AgentPrompts::renderPrompt(const QString& prompt, const QHash<QString, QString>& values) {
    QString rendered = prompt;

    for (const auto& tag : AgentPrompts::promptTags()) {
        rendered.replace(QStringLiteral("{{%1}}").arg(tag.name), values.value(tag.name));
    }

    return rendered;
}

} // namespace workpane::plugins::ai
