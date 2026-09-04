#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace workpane::plugins::ai {

// A tag the system prompt of an agent may carry, replaced by what the run knows when the turn is built.
struct PromptTagDescriptor final {
    QString name;
    QString descriptionKey;
};

class AgentPrompts final {
  public:
    [[nodiscard]] static const QVector<PromptTagDescriptor>& promptTags();
    [[nodiscard]] static QStringList unknownPromptTags(const QString& prompt);
    [[nodiscard]] static QString renderPrompt(const QString& prompt, const QHash<QString, QString>& values);
};

} // namespace workpane::plugins::ai
