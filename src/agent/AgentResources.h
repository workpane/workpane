#pragma once

#include <QString>
#include <QVector>

namespace workpane::agent {

// The published ecosystems leave four kinds of instruction on disk, and a bundle is a directory carrying more of them.
enum class ResourceKind { Skill, Command, Agent, Context, Bundle, ServerCatalog };

// A root either holds entries that declare themselves, in either published layout, or names one document that is the resource.
enum class ResourceLayout { MarkdownBundle, DocumentFile };

struct ResourceRoot final {
    ResourceKind kind{ResourceKind::Skill};
    ResourceLayout layout{ResourceLayout::MarkdownBundle};
    QString path;
    QString bundleFile;
    bool project{true};
};

struct ResourceDescriptor final {
    ResourceKind kind{ResourceKind::Skill};
    QString name;
    QString description;
    QString path;
    QString root;
    // A context file is read whole because the whole document is what joins the prompt, while every other kind discloses only its declaration.
    QString content;
    bool project{true};

    [[nodiscard]] bool operator==(const ResourceDescriptor& other) const = default;
};

// The roots are the published ones of every ecosystem the agent may run inside, and the order decides which of them owns a repeated name.
class AgentResourceRoots final {
  public:
    [[nodiscard]] static QVector<ResourceRoot> resourceRoots(const QString& workdir, const QString& home);
    [[nodiscard]] static QString frontMatterValue(const QString& content, const QString& key);
};

} // namespace workpane::agent
