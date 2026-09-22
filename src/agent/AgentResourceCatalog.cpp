#include "agent/AgentResourceCatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace workpane::agent {

constexpr int maximumResourceCount = 400;
constexpr int maximumRootEntries = 500;
constexpr qint64 maximumResourceBytes = 1 << 17;
// A bundle carries resources of its own and never another bundle, because the depth of what a workspace declares must not be decided by that workspace.
constexpr qsizetype maximumBundleCount = 32;

// One scan walks the roots in order and the entries of each root in order, so a repeated name always resolves the same way.
struct AgentResourceCatalog::Scan final {
    QString workdir;
    QVector<ResourceRoot> roots;
    int rootIndex{0};
    QVector<filesystem::DirectoryEntry> entries;
    int entryIndex{0};
    QVector<ResourceDescriptor> found;
    bool bundlesExpanded{false};
    QVector<Completion> completions;
};

class AgentResourceCatalogHelper final {
  public:
    static bool claimed(const QVector<ResourceDescriptor>& found, ResourceKind kind, const QString& name);
    static bool describes(ResourceKind kind);
};

bool AgentResourceCatalogHelper::claimed(const QVector<ResourceDescriptor>& found, ResourceKind kind, const QString& name) {
    // clang-format off
    return std::any_of(found.constBegin(), found.constEnd(), [kind, &name](const ResourceDescriptor& resource) { return resource.kind == kind && resource.name.compare(name, Qt::CaseInsensitive) == 0; });
    // clang-format on
}

// A skill, a command and an agent declare themselves in front matter, while a context file and a server catalog are the whole document.
bool AgentResourceCatalogHelper::describes(ResourceKind kind) {
    return kind == ResourceKind::Skill || kind == ResourceKind::Command || kind == ResourceKind::Agent;
}

AgentResourceCatalog::AgentResourceCatalog(plugins::PluginHost& host, QObject* parent) : QObject(parent), m_host(host) {}

ResourceDescriptor AgentResourceCatalog::describe(const ResourceRoot& root, const QString& name, const QString& path, const QString& content) {
    ResourceDescriptor resource{root.kind, name, {}, path, root.path, {}, root.project};

    if (root.kind == ResourceKind::Context) {
        resource.content = content.trimmed();
        return resource;
    }

    if (!AgentResourceCatalogHelper::describes(root.kind)) {
        return resource;
    }

    const QString declared = AgentResourceRoots::frontMatterValue(content, QStringLiteral("name"));
    resource.description = AgentResourceRoots::frontMatterValue(content, QStringLiteral("description"));

    if (!declared.isEmpty()) {
        resource.name = declared;
    }

    return resource;
}

QVector<ResourceDescriptor> AgentResourceCatalog::ofKind(const QVector<ResourceDescriptor>& resources, ResourceKind kind) {
    QVector<ResourceDescriptor> selected;

    for (const auto& resource : resources) {
        if (resource.kind == kind) {
            selected.append(resource);
        }
    }

    return selected;
}

// What was read is kept for the turns of one run, because the prompt and the tools ask for it on every turn and a disk is not answered that often.
void AgentResourceCatalog::discover(const QString& workdir, const Completion& completion) {
    if (m_cacheValid && m_scannedWorkdir == workdir) {
        completion(m_cached);
        return;
    }

    // A walk already under way for this directory answers everyone who asked, because forty roots are not worth reading twice at once.
    if (m_running != nullptr && m_running->workdir == workdir) {
        m_running->completions.append(completion);
        return;
    }

    auto scan = std::make_shared<Scan>();
    scan->workdir = workdir;
    scan->roots = AgentResourceRoots::resourceRoots(workdir, QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    scan->completions.append(completion);
    m_running = scan;
    scanRoot(scan);
}

void AgentResourceCatalog::forget() {
    m_cacheValid = false;
    m_cached.clear();
    m_scannedWorkdir.clear();
}

void AgentResourceCatalog::scanRoot(const std::shared_ptr<Scan>& scan) {
    if (scan->rootIndex >= scan->roots.size() || scan->found.size() >= maximumResourceCount) {
        expandBundles(scan);
        return;
    }

    const ResourceRoot root = scan->roots.at(scan->rootIndex);

    if (root.layout == ResourceLayout::DocumentFile) {
        readDocument(scan);
        return;
    }

    auto future = m_host.listDirectory(root.path, maximumRootEntries);
    // clang-format off
    future.then(this, [this, scan](Result<QVector<filesystem::DirectoryEntry>> listed) {
        scan->entries = listed.hasValue() ? listed.value() : QVector<filesystem::DirectoryEntry>{};
        scan->entryIndex = 0;
        scanEntry(scan);
    });
    // clang-format on
}

// A document root names one file, which is read directly because the published ecosystems name it with a leading dot and a directory listing carries no hidden entry.
void AgentResourceCatalog::readDocument(const std::shared_ptr<Scan>& scan) {
    const ResourceRoot root = scan->roots.at(scan->rootIndex);
    const QString name = QFileInfo(root.path).fileName();

    if (AgentResourceCatalogHelper::claimed(scan->found, root.kind, name)) {
        ++scan->rootIndex;
        scanRoot(scan);
        return;
    }

    auto future = m_host.readFile(root.path, maximumResourceBytes);
    // clang-format off
    future.then(this, [this, scan, root, name](Result<QByteArray> content) {
        if (content.hasValue()) {
            scan->found.append(describe(root, name, root.path, QString::fromUtf8(content.value())));
        }
        ++scan->rootIndex;
        scanRoot(scan);
    });
    // clang-format on
}

void AgentResourceCatalog::scanEntry(const std::shared_ptr<Scan>& scan) {
    const ResourceRoot root = scan->roots.at(scan->rootIndex);
    QString name;
    QString path;

    // An entry the layout has no use for is skipped in place, because recursing once per skipped file would grow the stack with the directory.
    while (scan->entryIndex < scan->entries.size() && scan->found.size() < maximumResourceCount) {
        const filesystem::DirectoryEntry entry = scan->entries.at(scan->entryIndex);
        ++scan->entryIndex;

        const bool flat = !entry.directory && entry.name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive);

        if (!entry.directory && !flat) {
            continue;
        }

        const QString candidate = flat ? entry.name.chopped(3) : entry.name;

        if (AgentResourceCatalogHelper::claimed(scan->found, root.kind, candidate)) {
            continue;
        }

        name = candidate;
        path = flat ? QDir(root.path).filePath(entry.name) : QDir(root.path).filePath(entry.name + QLatin1Char('/') + root.bundleFile);
        break;
    }

    if (name.isEmpty()) {
        ++scan->rootIndex;
        scanRoot(scan);
        return;
    }

    auto future = m_host.readFile(path, maximumResourceBytes);
    // clang-format off
    future.then(this, [this, scan, root, name, path](Result<QByteArray> content) {
        if (!content.hasValue()) {
            m_host.log(plugins::LogLevel::Warning, QStringLiteral("agent.resources"), QStringLiteral("A resource could not be read and was skipped"), {{QStringLiteral("detail"), path}});
            scanEntry(scan);
            return;
        }
        const ResourceDescriptor resource = describe(root, name, path, QString::fromUtf8(content.value()));
        if (AgentResourceCatalogHelper::describes(root.kind) && resource.description.isEmpty()) {
            // A front matter that declares nothing about the entry is skipped by name, because the catalog it belongs to still answers.
            m_host.log(plugins::LogLevel::Warning, QStringLiteral("agent.resources"), QStringLiteral("A resource declares no description and was skipped"), {{QStringLiteral("detail"), path}});
            scanEntry(scan);
            return;
        }
        if (!AgentResourceCatalogHelper::claimed(scan->found, resource.kind, resource.name)) {
            scan->found.append(resource);
        }
        scanEntry(scan);
    });
    // clang-format on
}

// A discovered bundle contributes the resources it carries, which is what makes a published plugin readable without installing it.
void AgentResourceCatalog::expandBundles(const std::shared_ptr<Scan>& scan) {
    if (scan->bundlesExpanded) {
        finish(scan);
        return;
    }

    scan->bundlesExpanded = true;
    const QVector<ResourceDescriptor> bundles = ofKind(scan->found, ResourceKind::Bundle);
    QVector<ResourceRoot> derived;

    for (qsizetype index = 0; index < bundles.size() && index < maximumBundleCount; ++index) {
        const QDir directory = QFileInfo(bundles.at(index).path).dir();
        const bool project = bundles.at(index).project;
        derived.append({ResourceKind::Skill, ResourceLayout::MarkdownBundle, QDir::cleanPath(directory.filePath(QStringLiteral("skills"))), QStringLiteral("SKILL.md"), project});
        derived.append({ResourceKind::Command, ResourceLayout::MarkdownBundle, QDir::cleanPath(directory.filePath(QStringLiteral("commands"))), QStringLiteral("COMMAND.md"), project});
        derived.append({ResourceKind::Agent, ResourceLayout::MarkdownBundle, QDir::cleanPath(directory.filePath(QStringLiteral("agents"))), QStringLiteral("AGENT.md"), project});
    }

    if (derived.isEmpty()) {
        finish(scan);
        return;
    }

    scan->roots = derived;
    scan->rootIndex = 0;
    scanRoot(scan);
}

void AgentResourceCatalog::finish(const std::shared_ptr<Scan>& scan) {
    m_cached = scan->found;
    m_scannedWorkdir = scan->workdir;
    m_cacheValid = true;

    if (m_running == scan) {
        m_running.reset();
    }

    // Everyone waiting is answered with what this walk found, because a completion may ask for the catalog again and start another one.
    const QVector<ResourceDescriptor> answer = scan->found;

    for (const auto& completion : scan->completions) {
        completion(answer);
    }
}

} // namespace workpane::agent
