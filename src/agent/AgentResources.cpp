#include "agent/AgentResources.h"

#include <QDir>

namespace workpane::agent {

class AgentResourcesHelper final {
  public:
    static void appendMarkdownRoots(QVector<ResourceRoot>& roots, const QDir& base, bool project, ResourceKind kind, const QStringList& locations, const QString& bundleFile);
    static void appendDocuments(QVector<ResourceRoot>& roots, const QDir& base, bool project, ResourceKind kind, const QStringList& names);
};

// A directory holding its declared file and a single Markdown file are both published layouts, so one root accepts either.
void AgentResourcesHelper::appendMarkdownRoots(QVector<ResourceRoot>& roots, const QDir& base, bool project, ResourceKind kind, const QStringList& locations, const QString& bundleFile) {
    for (const QString& location : locations) {
        roots.append({kind, ResourceLayout::MarkdownBundle, QDir::cleanPath(base.filePath(location)), bundleFile, project});
    }
}

void AgentResourcesHelper::appendDocuments(QVector<ResourceRoot>& roots, const QDir& base, bool project, ResourceKind kind, const QStringList& names) {
    for (const QString& name : names) {
        roots.append({kind, ResourceLayout::DocumentFile, QDir::cleanPath(base.filePath(name)), {}, project});
    }
}

// The project roots come first because the workspace the agent runs in overrides what the machine offers.
QVector<ResourceRoot> AgentResourceRoots::resourceRoots(const QString& workdir, const QString& home) {
    QVector<ResourceRoot> roots;

    if (!workdir.isEmpty()) {
        const QDir project(workdir);
        AgentResourcesHelper::appendMarkdownRoots(roots, project, true, ResourceKind::Skill, {QStringLiteral(".claude/skills"), QStringLiteral(".agents/skills"), QStringLiteral(".cursor/skills"), QStringLiteral(".opencode/skills"), QStringLiteral(".codex/skills"), QStringLiteral(".gemini/skills"), QStringLiteral(".windsurf/skills"), QStringLiteral(".skills"), QStringLiteral("skills")}, QStringLiteral("SKILL.md"));
        AgentResourcesHelper::appendMarkdownRoots(roots, project, true, ResourceKind::Command, {QStringLiteral(".claude/commands"), QStringLiteral(".agents/commands"), QStringLiteral(".opencode/commands"), QStringLiteral(".codex/prompts"), QStringLiteral(".commands")}, QStringLiteral("COMMAND.md"));
        AgentResourcesHelper::appendMarkdownRoots(roots, project, true, ResourceKind::Agent, {QStringLiteral(".claude/agents"), QStringLiteral(".agents/agents"), QStringLiteral(".opencode/agents"), QStringLiteral(".cursor/agents")}, QStringLiteral("AGENT.md"));
        AgentResourcesHelper::appendMarkdownRoots(roots, project, true, ResourceKind::Bundle, {QStringLiteral(".claude/plugins"), QStringLiteral(".agents/plugins")}, QStringLiteral("plugin.json"));
        AgentResourcesHelper::appendDocuments(roots, project, true, ResourceKind::Context, {QStringLiteral("AGENTS.md"), QStringLiteral("AGENT.md"), QStringLiteral("CLAUDE.md"), QStringLiteral("GEMINI.md"), QStringLiteral(".cursorrules"), QStringLiteral(".windsurfrules"), QStringLiteral(".github/copilot-instructions.md")});
        AgentResourcesHelper::appendDocuments(roots, project, true, ResourceKind::ServerCatalog, {QStringLiteral(".mcp.json"), QStringLiteral(".cursor/mcp.json"), QStringLiteral(".vscode/mcp.json"), QStringLiteral(".claude/settings.json"), QStringLiteral(".claude/settings.local.json")});
    }

    if (!home.isEmpty()) {
        const QDir user(home);
        AgentResourcesHelper::appendMarkdownRoots(roots, user, false, ResourceKind::Skill, {QStringLiteral(".claude/skills"), QStringLiteral(".agents/skills"), QStringLiteral(".config/agents/skills")}, QStringLiteral("SKILL.md"));
        AgentResourcesHelper::appendMarkdownRoots(roots, user, false, ResourceKind::Command, {QStringLiteral(".claude/commands"), QStringLiteral(".agents/commands"), QStringLiteral(".codex/prompts")}, QStringLiteral("COMMAND.md"));
        AgentResourcesHelper::appendMarkdownRoots(roots, user, false, ResourceKind::Agent, {QStringLiteral(".claude/agents"), QStringLiteral(".agents/agents"), QStringLiteral(".cursor/agents")}, QStringLiteral("AGENT.md"));
        AgentResourcesHelper::appendMarkdownRoots(roots, user, false, ResourceKind::Bundle, {QStringLiteral(".claude/plugins"), QStringLiteral(".agents/plugins")}, QStringLiteral("plugin.json"));
        AgentResourcesHelper::appendDocuments(roots, user, false, ResourceKind::Context, {QStringLiteral(".claude/CLAUDE.md"), QStringLiteral(".agents/AGENTS.md"), QStringLiteral(".codex/AGENTS.md"), QStringLiteral(".gemini/GEMINI.md")});
        AgentResourcesHelper::appendDocuments(roots, user, false, ResourceKind::ServerCatalog, {QStringLiteral(".claude.json"), QStringLiteral(".claude/settings.json"), QStringLiteral(".cursor/mcp.json"), QStringLiteral(".codex/config.toml"), QStringLiteral(".gemini/settings.json")});
    }

    return roots;
}

// The published front matter is the opening fence, its keys and the closing fence, and anything else carries nothing.
QString AgentResourceRoots::frontMatterValue(const QString& content, const QString& key) {
    if (!content.startsWith(QStringLiteral("---"))) {
        return {};
    }

    const qsizetype closing = content.indexOf(QStringLiteral("\n---"), 3);

    if (closing < 0) {
        return {};
    }

    const QString frontMatter = content.mid(3, closing - 3);

    for (const auto& line : frontMatter.split(QLatin1Char('\n'))) {
        const qsizetype separator = line.indexOf(QLatin1Char(':'));

        if (separator < 0 || line.left(separator).trimmed() != key) {
            continue;
        }

        return line.mid(separator + 1).trimmed();
    }

    return {};
}

} // namespace workpane::agent
