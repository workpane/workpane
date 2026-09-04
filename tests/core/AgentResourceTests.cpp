#include "TestFuture.h"
#include "TestPluginHost.h"
#include "agent/AgentResourceCatalog.h"
#include "agent/AgentResources.h"
#include "filesystem/FileSystemService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>

namespace workpane {

class AgentResourceTestsHelper final {
  public:
    static void write(const QString& path, const QString& content);
    static bool carries(const QVector<agent::ResourceDescriptor>& resources, agent::ResourceKind kind, const QString& name);
    static agent::ResourceDescriptor find(const QVector<agent::ResourceDescriptor>& resources, agent::ResourceKind kind, const QString& name);
    static qsizetype declaredByTheWorkspace(const QVector<agent::ResourceDescriptor>& resources, agent::ResourceKind kind);
};

// A case asserts what the temporary workspace declares and never a total, because the machine roots belong to whoever is running the suite.
qsizetype AgentResourceTestsHelper::declaredByTheWorkspace(const QVector<agent::ResourceDescriptor>& resources, agent::ResourceKind kind) {
    qsizetype count = 0;

    for (const auto& resource : resources) {
        if (resource.kind == kind && resource.project) {
            ++count;
        }
    }

    return count;
}

void AgentResourceTestsHelper::write(const QString& path, const QString& content) {
    ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(content.toUtf8()), content.toUtf8().size());
}

bool AgentResourceTestsHelper::carries(const QVector<agent::ResourceDescriptor>& resources, agent::ResourceKind kind, const QString& name) {
    // clang-format off
    return std::any_of(resources.constBegin(), resources.constEnd(), [kind, &name](const agent::ResourceDescriptor& resource) { return resource.kind == kind && resource.name == name; });
    // clang-format on
}

agent::ResourceDescriptor AgentResourceTestsHelper::find(const QVector<agent::ResourceDescriptor>& resources, agent::ResourceKind kind, const QString& name) {
    for (const auto& resource : resources) {
        if (resource.kind == kind && resource.name == name) {
            return resource;
        }
    }

    return {};
}

TEST(AgentResourcesTest, ReadsTheFrontMatterOfThePublishedFormatAndNothingElse) {
    const QString document = QStringLiteral("---\nname: Release notes\ndescription: Writes them from the history\n---\n\nBody\n");
    EXPECT_EQ(agent::AgentResourceRoots::frontMatterValue(document, QStringLiteral("name")), QStringLiteral("Release notes"));
    EXPECT_EQ(agent::AgentResourceRoots::frontMatterValue(document, QStringLiteral("description")), QStringLiteral("Writes them from the history"));
    EXPECT_TRUE(agent::AgentResourceRoots::frontMatterValue(document, QStringLiteral("model")).isEmpty());

    // A document that opens with no fence carries no declaration, and one whose fence never closes carries none either.
    EXPECT_TRUE(agent::AgentResourceRoots::frontMatterValue(QStringLiteral("name: Loose\n"), QStringLiteral("name")).isEmpty());
    EXPECT_TRUE(agent::AgentResourceRoots::frontMatterValue(QStringLiteral("---\nname: Unclosed\n"), QStringLiteral("name")).isEmpty());
    EXPECT_TRUE(agent::AgentResourceRoots::frontMatterValue(QString{}, QStringLiteral("name")).isEmpty());
}

// A skill is written by whoever prepared the workspace, so its front matter is bytes this project did not decide.
TEST(AgentResourcesTest, AnswersEveryHostileFrontMatterInsteadOfReadingPastIt) {
    const QStringList hostile{
        QString{}, QStringLiteral("-"), QStringLiteral("--"), QStringLiteral("---"), QStringLiteral("---\n"), QStringLiteral("---\n---"), QStringLiteral("\n---\nname: late\n---\n"), QStringLiteral("---\nname\n---\n"), QStringLiteral("---\n:value\n---\n"), QStringLiteral("---\nname:\n---\n"), QStringLiteral("---\n   name   :   spaced   \n---\n"), QStringLiteral("---\nname: a: b: c\n---\n"), QStringLiteral("---") + QString(8192, QLatin1Char('-')), QStringLiteral("---\n") + QString(8192, QLatin1Char('\n')) + QStringLiteral("---\n"), QStringLiteral("---\nname: ") + QString(8192, QLatin1Char('x')) + QStringLiteral("\n---\n"), QString::fromUtf8("---\nname: \xC3\x28\n---\n"), QStringLiteral("---\r\nname: windows\r\n---\r\n"), QStringLiteral("---\n\0name: nul\n---\n"),
    };

    for (const auto& document : hostile) {
        std::ignore = agent::AgentResourceRoots::frontMatterValue(document, QStringLiteral("name"));
        std::ignore = agent::AgentResourceRoots::frontMatterValue(document, QString{});
        EXPECT_LE(agent::AgentResourceRoots::frontMatterValue(document, QStringLiteral("name")).size(), document.size()) << document.left(40).toStdString();
    }

    // Pseudo-random documents are seeded, so a failure here reproduces exactly.
    quint32 seed = 0x5eed4321;
    for (int round = 0; round < 400; ++round) {
        QString document;
        const int length = static_cast<int>(seed % 160) + 1;
        for (int index = 0; index < length; ++index) {
            seed = seed * 1664525U + 1013904223U;
            static const QString alphabet = QStringLiteral("-:\n\r abcname0189");
            document.append(alphabet.at(static_cast<int>(seed >> 16) % alphabet.size()));
        }
        EXPECT_LE(agent::AgentResourceRoots::frontMatterValue(document, QStringLiteral("name")).size(), document.size());
    }
}

TEST(AgentResourcesTest, DeclaresTheProjectRootsBeforeTheMachineRootsForEveryPublishedKind) {
    const QVector<agent::ResourceRoot> roots = agent::AgentResourceRoots::resourceRoots(QStringLiteral("/workspace"), QStringLiteral("/home/reader"));
    ASSERT_FALSE(roots.isEmpty());

    qsizetype lastProject = -1;
    qsizetype firstUser = roots.size();

    for (qsizetype index = 0; index < roots.size(); ++index) {
        if (roots.at(index).project) {
            lastProject = index;
            continue;
        }
        firstUser = std::min(firstUser, index);
    }

    // A workspace overrides what the machine offers, so every project root is declared before every user root.
    EXPECT_LT(lastProject, firstUser);

    QSet<agent::ResourceKind> kinds;
    for (const auto& root : roots) {
        kinds.insert(root.kind);
        EXPECT_TRUE(root.path.startsWith(QStringLiteral("/workspace")) || root.path.startsWith(QStringLiteral("/home/reader")));
    }

    EXPECT_TRUE(kinds.contains(agent::ResourceKind::Skill));
    EXPECT_TRUE(kinds.contains(agent::ResourceKind::Command));
    EXPECT_TRUE(kinds.contains(agent::ResourceKind::Agent));
    EXPECT_TRUE(kinds.contains(agent::ResourceKind::Context));
    EXPECT_TRUE(kinds.contains(agent::ResourceKind::Bundle));
    EXPECT_TRUE(kinds.contains(agent::ResourceKind::ServerCatalog));

    // The published conventions of every ecosystem the agent may run inside are declared, in the project and on the machine.
    QStringList declared;
    for (const auto& root : roots) {
        declared.append(root.path);
    }
    for (const auto& expected : {QStringLiteral("/workspace/AGENTS.md"), QStringLiteral("/workspace/CLAUDE.md"), QStringLiteral("/workspace/GEMINI.md"), QStringLiteral("/workspace/.cursorrules"), QStringLiteral("/workspace/.windsurfrules"), QStringLiteral("/workspace/.github/copilot-instructions.md"), QStringLiteral("/workspace/.mcp.json"), QStringLiteral("/workspace/.claude/settings.local.json"), QStringLiteral("/workspace/.claude/skills"), QStringLiteral("/workspace/.cursor/agents"), QStringLiteral("/home/reader/.claude.json"), QStringLiteral("/home/reader/.codex/AGENTS.md"), QStringLiteral("/home/reader/.gemini/GEMINI.md"), QStringLiteral("/home/reader/.claude/CLAUDE.md"), QStringLiteral("/home/reader/.cursor/mcp.json"), QStringLiteral("/home/reader/.codex/config.toml"), QStringLiteral("/home/reader/.codex/prompts")}) {
        EXPECT_TRUE(declared.contains(expected)) << "no root declares " << expected.toStdString();
    }

    // A run with no working directory reads only what the machine declares, and one with no home reads only the workspace.
    // clang-format off
    const auto userOnly = agent::AgentResourceRoots::resourceRoots(QString{}, QStringLiteral("/home/reader"));
    EXPECT_TRUE(std::none_of(userOnly.constBegin(), userOnly.constEnd(), [](const agent::ResourceRoot& root) { return root.project; }));
    const auto projectOnly = agent::AgentResourceRoots::resourceRoots(QStringLiteral("/workspace"), QString{});
    EXPECT_TRUE(std::all_of(projectOnly.constBegin(), projectOnly.constEnd(), [](const agent::ResourceRoot& root) { return root.project; }));
    // clang-format on
    EXPECT_TRUE(agent::AgentResourceRoots::resourceRoots(QString{}, QString{}).isEmpty());
}

TEST(AgentResourceCatalogTest, ReadsEveryPublishedLayoutFromTheWorkspaceItRunsIn) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDir project(workdir.path());

    // Both published skill layouts, the directory holding its instructions and the single Markdown file.
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/release-notes/SKILL.md")), QStringLiteral("---\nname: release-notes\ndescription: Writes release notes\n---\nBody\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".agents/skills/review.md")), QStringLiteral("---\nname: review\ndescription: Reviews a change\n---\nBody\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/commands/deploy.md")), QStringLiteral("---\nname: deploy\ndescription: Deploys the product\n---\nBody\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/agents/reviewer.md")), QStringLiteral("---\nname: reviewer\ndescription: Reviews code\n---\nBody\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral("AGENTS.md")), QStringLiteral("Always run the suite.\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".mcp.json")), QStringLiteral("{\"mcpServers\": {}}\n"));

    // A front matter declaring nothing about the entry is skipped by name and never drops the catalog around it.
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/nameless/SKILL.md")), QStringLiteral("---\nname: nameless\n---\nBody\n"));
    // An entry the layout has no use for is passed over rather than read.
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/notes.txt")), QStringLiteral("ignored\n"));

    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    agent::AgentResourceCatalog catalog(host);

    QVector<agent::ResourceDescriptor> found;
    bool answered = false;
    // clang-format off
    catalog.discover(workdir.path(), [&found, &answered](const QVector<agent::ResourceDescriptor>& resources) { found = resources; answered = true; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; }));
    // clang-format on

    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("release-notes")));
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("review")));
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Command, QStringLiteral("deploy")));
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Agent, QStringLiteral("reviewer")));
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Context, QStringLiteral("AGENTS.md")));
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::ServerCatalog, QStringLiteral(".mcp.json")));

    EXPECT_FALSE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("nameless")));
    EXPECT_FALSE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("notes")));

    // The whole document is what a context file contributes, so the catalog carries what it read.
    EXPECT_EQ(AgentResourceTestsHelper::find(found, agent::ResourceKind::Context, QStringLiteral("AGENTS.md")).content, QStringLiteral("Always run the suite."));
    EXPECT_TRUE(AgentResourceTestsHelper::find(found, agent::ResourceKind::Skill, QStringLiteral("review")).content.isEmpty());
    EXPECT_EQ(AgentResourceTestsHelper::find(found, agent::ResourceKind::Skill, QStringLiteral("review")).description, QStringLiteral("Reviews a change"));
    EXPECT_TRUE(AgentResourceTestsHelper::find(found, agent::ResourceKind::Skill, QStringLiteral("review")).project);

    EXPECT_EQ(AgentResourceTestsHelper::declaredByTheWorkspace(found, agent::ResourceKind::Command), 1);
}

TEST(AgentResourceCatalogTest, LetsTheFirstRootThatDeclaresANameOwnIt) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDir project(workdir.path());

    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/shared/SKILL.md")), QStringLiteral("---\nname: shared\ndescription: The one the workspace declares first\n---\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".agents/skills/shared/SKILL.md")), QStringLiteral("---\nname: shared\ndescription: The one that is shadowed\n---\n"));

    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    agent::AgentResourceCatalog catalog(host);

    QVector<agent::ResourceDescriptor> found;
    bool answered = false;
    // clang-format off
    catalog.discover(workdir.path(), [&found, &answered](const QVector<agent::ResourceDescriptor>& resources) { found = resources; answered = true; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; }));
    // clang-format on

    EXPECT_EQ(AgentResourceTestsHelper::declaredByTheWorkspace(found, agent::ResourceKind::Skill), 1);
    EXPECT_EQ(AgentResourceTestsHelper::find(found, agent::ResourceKind::Skill, QStringLiteral("shared")).description, QStringLiteral("The one the workspace declares first"));
}

TEST(AgentResourceCatalogTest, ReadsWhatABundleCarriesWithoutInstallingAnyOfIt) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDir project(workdir.path());

    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/plugins/toolkit/plugin.json")), QStringLiteral("{\"name\": \"toolkit\"}\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/plugins/toolkit/skills/bundled/SKILL.md")), QStringLiteral("---\nname: bundled\ndescription: A skill the bundle carries\n---\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/plugins/toolkit/commands/ship.md")), QStringLiteral("---\nname: ship\ndescription: A command the bundle carries\n---\n"));

    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    agent::AgentResourceCatalog catalog(host);

    QVector<agent::ResourceDescriptor> found;
    bool answered = false;
    // clang-format off
    catalog.discover(workdir.path(), [&found, &answered](const QVector<agent::ResourceDescriptor>& resources) { found = resources; answered = true; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; }));
    // clang-format on

    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Bundle, QStringLiteral("toolkit")));
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("bundled")));
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Command, QStringLiteral("ship")));
}

TEST(AgentResourceCatalogTest, AnswersAnEmptyWorkspaceAndAWorkspaceThatIsNotThere) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    agent::AgentResourceCatalog catalog(host);

    QVector<agent::ResourceDescriptor> found;
    bool answered = false;
    // clang-format off
    catalog.discover(QDir(workdir.path()).filePath(QStringLiteral("absent")), [&found, &answered](const QVector<agent::ResourceDescriptor>& resources) { found = resources; answered = true; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; }));
    // clang-format on

    // clang-format off
    EXPECT_TRUE(std::none_of(found.constBegin(), found.constEnd(), [](const agent::ResourceDescriptor& resource) { return resource.project; }));
    // clang-format on
}

TEST(AgentResourceCatalogTest, ReadsTheWorkspaceAgainOnceTheRunForgetsWhatItHad) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDir project(workdir.path());
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/first/SKILL.md")), QStringLiteral("---\nname: first\ndescription: The one that was already there\n---\n"));

    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    agent::AgentResourceCatalog catalog(host);

    QVector<agent::ResourceDescriptor> found;
    bool answered = false;
    // clang-format off
    const auto collect = [&found, &answered](const QVector<agent::ResourceDescriptor>& resources) { found = resources; answered = true; };
    catalog.discover(workdir.path(), collect);
    ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; }));
    // clang-format on
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("first")));
    EXPECT_FALSE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("second")));

    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/second/SKILL.md")), QStringLiteral("---\nname: second\ndescription: The one written while the application was open\n---\n"));

    // What was read is kept for the turns of one run, so the skill written meanwhile is not seen yet.
    answered = false;
    // clang-format off
    catalog.discover(workdir.path(), collect);
    ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; }));
    // clang-format on
    EXPECT_FALSE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("second")));

    // A run reads the workspace again, which is what makes an instruction written while the application is open reach it.
    catalog.forget();
    answered = false;
    // clang-format off
    catalog.discover(workdir.path(), collect);
    ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; }));
    // clang-format on
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("first")));
    EXPECT_TRUE(AgentResourceTestsHelper::carries(found, agent::ResourceKind::Skill, QStringLiteral("second")));
}

TEST(AgentResourceCatalogTest, WalksOneDirectoryOnceHoweverManyRunsAskAtTheSameMoment) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    AgentResourceTestsHelper::write(QDir(workdir.path()).filePath(QStringLiteral(".claude/skills/shared/SKILL.md")), QStringLiteral("---\nname: shared\ndescription: Read once\n---\n"));

    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);

    std::atomic_int listings{0};
    // clang-format off
    host.listDirectoryHandler = [&service, &listings](const QString& path, int maximumEntries) { listings.fetch_add(1, std::memory_order_relaxed); return service.listDirectory(path, maximumEntries); };
    // clang-format on
    agent::AgentResourceCatalog catalog(host);

    int answers = 0;
    QVector<agent::ResourceDescriptor> first;
    QVector<agent::ResourceDescriptor> second;
    // clang-format off
    catalog.discover(workdir.path(), [&answers, &first](const QVector<agent::ResourceDescriptor>& resources) { first = resources; ++answers; });
    catalog.discover(workdir.path(), [&answers, &second](const QVector<agent::ResourceDescriptor>& resources) { second = resources; ++answers; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&answers]() { return answers == 2; }));
    // clang-format on

    // Both callers are answered by the one walk, because forty roots are not worth reading twice at once.
    EXPECT_EQ(first, second);
    EXPECT_TRUE(AgentResourceTestsHelper::carries(first, agent::ResourceKind::Skill, QStringLiteral("shared")));
    const int walked = listings.load(std::memory_order_relaxed);
    EXPECT_GT(walked, 0);

    catalog.forget();
    answers = 0;
    // clang-format off
    catalog.discover(workdir.path(), [&answers](const QVector<agent::ResourceDescriptor>&) { ++answers; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&answers]() { return answers == 1; }));
    // clang-format on
    EXPECT_EQ(listings.load(std::memory_order_relaxed), walked * 2);
}

// An entry the catalog cannot use is named in the log, because a writer whose skill never reaches the agent has nowhere else to look.
TEST(AgentResourceCatalogTest, NamesEverySkippedEntryInTheLogAndKeepsTheOnesAroundIt) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDir project(workdir.path());

    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/good.md")), QStringLiteral("---\nname: good\ndescription: The one that loads\n---\nBody\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/undescribed.md")), QStringLiteral("---\nname: undescribed\n---\nBody\n"));
    AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/oversized.md")), QStringLiteral("---\nname: oversized\ndescription: Larger than the catalog reads\n---\n") + QString(200000, QLatin1Char('x')));

    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    agent::AgentResourceCatalog catalog(host);

    QVector<agent::ResourceDescriptor> found;
    bool answered = false;
    // clang-format off
    catalog.discover(workdir.path(), [&found, &answered](const QVector<agent::ResourceDescriptor>& resources) { found = resources; answered = true; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; }));
    // clang-format on

    QStringList names;

    for (const auto& resource : found) {
        if (resource.kind == agent::ResourceKind::Skill) {
            names.append(resource.name);
        }
    }

    EXPECT_TRUE(names.contains(QStringLiteral("good")));
    EXPECT_FALSE(names.contains(QStringLiteral("undescribed")));
    EXPECT_FALSE(names.contains(QStringLiteral("oversized")));

    QStringList skipped;

    for (const auto& entry : host.logs) {
        if (entry.category == QStringLiteral("agent.resources")) {
            skipped.append(entry.details.value(QStringLiteral("detail")).toString());
        }
    }

    // clang-format off
    const auto skippedByName = [&skipped](const QString& name) { return std::ranges::any_of(skipped, [&name](const QString& path) { return path.endsWith(name); }); };
    // clang-format on
    EXPECT_TRUE(skippedByName(QStringLiteral("undescribed.md")));
    EXPECT_TRUE(skippedByName(QStringLiteral("oversized.md")));
}

TEST(AgentResourceCatalogTest, StopsAtTheBoundsItDeclaresInsteadOfReadingWhateverAWorkspaceHolds) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDir project(workdir.path());

    // The size of a workspace is decided by whoever opened it, so a workspace larger than the catalog accepts must still answer.
    constexpr int declaredSkills = 460;
    constexpr int declaredBundles = 40;

    for (int index = 0; index < declaredSkills; ++index) {
        const QString name = QStringLiteral("skill-%1").arg(index, 3, 10, QLatin1Char('0'));
        AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".claude/skills/%1.md").arg(name)), QStringLiteral("---\nname: %1\ndescription: One of many\n---\n").arg(name));
    }

    for (int index = 0; index < declaredBundles; ++index) {
        const QString name = QStringLiteral("bundle-%1").arg(index, 3, 10, QLatin1Char('0'));
        AgentResourceTestsHelper::write(project.filePath(QStringLiteral(".agents/plugins/%1/plugin.json").arg(name)), QStringLiteral("{}\n"));
    }

    filesystem::FileSystemService service;
    test::TestPluginHost host;
    host.useFileSystem(service);
    agent::AgentResourceCatalog catalog(host);

    QVector<agent::ResourceDescriptor> found;
    bool answered = false;
    // clang-format off
    catalog.discover(workdir.path(), [&found, &answered](const QVector<agent::ResourceDescriptor>& resources) { found = resources; answered = true; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; }, 60000));
    // clang-format on

    // The walk ends rather than reading everything, and what it kept is still whole entries rather than a truncated one.
    EXPECT_LT(found.size(), declaredSkills + declaredBundles);
    EXPECT_GT(AgentResourceTestsHelper::declaredByTheWorkspace(found, agent::ResourceKind::Skill), 0);

    for (const auto& resource : found) {
        EXPECT_FALSE(resource.name.isEmpty());
    }
}

} // namespace workpane
