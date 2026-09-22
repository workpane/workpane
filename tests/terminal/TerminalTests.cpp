#include "TerminalPlugin.h"
#include "TerminalSettingsStore.h"
#include "TerminalView.h"
#include "TerminalWorkspaceRepository.h"
#include "TestFuture.h"
#include "TestPluginHost.h"
#include "TestTranslations.h"
#include "persistence/StateStore.h"
#include "plugins/PluginCapability.h"
#include "terminal/GhosttyTerminalAdapter.h"
#include "terminal/ShellIntegrationFiles.h"
#include "terminal/ShellProfile.h"
#include "terminal/TerminalEnvironment.h"
#include "terminal/TerminalSession.h"
#include "terminal/TerminalShortcuts.h"
#include "terminal/TerminalThemeCatalog.h"
#include "terminal/platform/posix/PosixPtyBackend.h"
#include "terminal/platform/posix/PosixShellIntegration.h"
#ifdef Q_OS_WIN
#include "terminal/platform/windows/WindowsShellIntegration.h"
#endif
#include "ui/ApplicationShortcuts.h"
#include "ui/Components.h"
#include "ui/FindBar.h"
#include "ui/ShelfSessionChip.h"
#include "ui/TerminalPane.h"
#include "ui/TerminalWidget.h"
#include "ui/WorkspaceView.h"
#include "workspace/LayoutManager.h"
#include "workspace/WorkspaceManager.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPointF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace workpane {

class FakePtyBackend final : public terminalcore::IPtyBackend {
  public:
    Result<void> start(const terminalcore::ShellProfile& profile, const QString& workingDirectory, const QString& historyFile, int columns, int rows) override {
        ++startCalls;
        startedProfile = profile;
        startedDirectory = workingDirectory;
        startedHistoryFile = historyFile;
        startedColumns = columns;
        startedRows = rows;

        if (startError.has_value()) {
            return Result<void>::failure(startError.value());
        }

        isRunning = true;
        return Result<void>::success();
    }

    Result<void> write(const QByteArray& bytes) override {
        writes.append(bytes);

        if (writeError.has_value()) {
            return Result<void>::failure(writeError.value());
        }

        return Result<void>::success();
    }

    Result<void> resize(int columns, int rows, int cellWidth, int cellHeight) override {
        ++resizeCalls;
        resizedColumns = columns;
        resizedRows = rows;
        resizedCellWidth = cellWidth;
        resizedCellHeight = cellHeight;

        if (resizeError.has_value()) {
            return Result<void>::failure(resizeError.value());
        }

        return Result<void>::success();
    }

    void setOutputPaused(bool paused) override {
        outputPaused = paused;
        outputPauseTransitions.append(paused);
    }

    void terminate() override {
        ++terminateCalls;
        isRunning = false;
    }

    [[nodiscard]] bool running() const override {
        return isRunning;
    }

    void sendOutput(const QByteArray& bytes) {
        emit outputReady(bytes);
    }

    void sendWorkingDirectory(const QString& directory) {
        emit workingDirectoryChanged(directory);
    }

    void sendExit(int code) {
        isRunning = false;
        emit processExited(code);
    }

    void sendError(const QString& message) {
        emit backendError(message);
    }

    std::optional<Error> startError;
    std::optional<Error> writeError;
    std::optional<Error> resizeError;
    terminalcore::ShellProfile startedProfile;
    QString startedDirectory;
    QString startedHistoryFile;
    QList<QByteArray> writes;
    int startCalls{};
    int startedColumns{};
    int startedRows{};
    int resizeCalls{};
    int resizedColumns{};
    int resizedRows{};
    int resizedCellWidth{};
    int resizedCellHeight{};
    int terminateCalls{};
    bool isRunning{};
    bool outputPaused{};
    QList<bool> outputPauseTransitions;
};

TEST(LayoutManagerTest, ExposesEveryPresetAndRejectsUnknownIdentifiers) {
    const auto presets = plugins::terminalplugin::workspace::LayoutManager::presets();
    ASSERT_EQ(presets.size(), 17);

    for (const auto& preset : presets) {
        EXPECT_FALSE(preset.id.isEmpty());
        EXPECT_FALSE(preset.name.isEmpty());
        EXPECT_GT(preset.slotCount, 0);
        EXPECT_GT(preset.columns, 0);
        EXPECT_GT(preset.rows, 0);
        ASSERT_TRUE(plugins::terminalplugin::workspace::LayoutManager::preset(preset.id).hasValue());
        EXPECT_EQ(plugins::terminalplugin::workspace::LayoutManager::preset(preset.id).value().slotCount, preset.slotCount);
    }

    EXPECT_EQ(plugins::terminalplugin::workspace::LayoutManager::preset(QStringLiteral("unknown")).error().code, QStringLiteral("terminal_layout_preset_unknown"));
}

TEST(TerminalShortcutsTest, SeparatesPluginApplicationAndTerminalOwnedInput) {
    const QKeyCombination newTerminalCombination = terminalcore::TerminalShortcuts::newTerminal()[0];
    QKeyEvent newTerminalEvent(QEvent::KeyPress, newTerminalCombination.key(), newTerminalCombination.keyboardModifiers());
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isReservedForApplication(newTerminalEvent));
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isTerminalOwned(newTerminalEvent));

    const QKeyCombination quitCombination = ui::ApplicationShortcuts::quit()[0];
    QKeyEvent quitEvent(QEvent::KeyPress, quitCombination.key(), quitCombination.keyboardModifiers());
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isReservedForApplication(quitEvent));
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isTerminalOwned(quitEvent));

    QKeyEvent terminalControlEvent(QEvent::KeyPress, Qt::Key_A, terminalcore::terminalControlModifier);
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isReservedForApplication(terminalControlEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(terminalControlEvent));

#ifdef Q_OS_LINUX
    QKeyEvent pasteEvent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
#else
    QKeyEvent pasteEvent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
#endif
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isPaste(pasteEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(pasteEvent));

#ifdef Q_OS_MACOS
    QKeyEvent interruptEvent(QEvent::KeyPress, Qt::Key_C, Qt::MetaModifier);
    QKeyEvent copyEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(interruptEvent));
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isTerminalOwned(copyEvent));
    QKeyEvent closeEvent(QEvent::KeyPress, Qt::Key_W, Qt::ControlModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isReservedForApplication(closeEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isCloseTerminal(closeEvent));

    // The shell interrupts with the physical control key, so only the application combination copies.
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isCopy(copyEvent));
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isCopy(interruptEvent));

    // Selecting and clearing take the application combination, and the shell keeps the physical control key of both letters.
    QKeyEvent selectAllEvent(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QKeyEvent clearEvent(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
    QKeyEvent beginningOfLineEvent(QEvent::KeyPress, Qt::Key_A, Qt::MetaModifier);
    QKeyEvent killLineEvent(QEvent::KeyPress, Qt::Key_K, Qt::MetaModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isSelectAll(selectAllEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isClearBuffer(clearEvent));
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isSelectAll(beginningOfLineEvent));
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isClearBuffer(killLineEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(beginningOfLineEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(killLineEvent));
    QKeyEvent deleteWordEvent(QEvent::KeyPress, Qt::Key_W, Qt::MetaModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(deleteWordEvent));
#else
    QKeyEvent interruptEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(interruptEvent));

    // Copying never takes the combination that interrupts the shell, so it carries the shift the other platforms use.
    QKeyEvent shiftedCopyEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isCopy(shiftedCopyEvent));
    // Selecting and clearing carry the shift, because the plain combinations move and cut the shell line.
    QKeyEvent shiftedSelectAllEvent(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
    QKeyEvent shiftedClearEvent(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    QKeyEvent beginningOfLineEvent(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QKeyEvent killLineEvent(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isSelectAll(shiftedSelectAllEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isClearBuffer(shiftedClearEvent));
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isSelectAll(beginningOfLineEvent));
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isClearBuffer(killLineEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(beginningOfLineEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(killLineEvent));
    QKeyEvent shiftedCloseEvent(QEvent::KeyPress, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isCloseTerminal(shiftedCloseEvent));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isReservedForApplication(shiftedCloseEvent));
    QKeyEvent deleteWordEvent(QEvent::KeyPress, Qt::Key_W, Qt::ControlModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isTerminalOwned(deleteWordEvent));
    EXPECT_FALSE(terminalcore::TerminalShortcuts::isReservedForApplication(deleteWordEvent));
#endif

    QList<QKeySequence> applicationSequences{ui::ApplicationShortcuts::quit()};
    applicationSequences += ui::ApplicationShortcuts::increaseContentFont();
    applicationSequences += ui::ApplicationShortcuts::decreaseContentFont();
    applicationSequences += ui::ApplicationShortcuts::resetContentFont();

    for (const auto& sequence : applicationSequences) {
        ASSERT_GT(sequence.count(), 0) << sequence.toString().toStdString();
        const QKeyCombination combination = sequence[0];
        // A combination without a modifier is a media key nobody can press, which is what the Quit standard key answers outside macOS.
        EXPECT_NE(combination.keyboardModifiers(), Qt::NoModifier) << sequence.toString().toStdString();
        QKeyEvent applicationEvent(QEvent::KeyPress, combination.key(), combination.keyboardModifiers());
        EXPECT_TRUE(terminalcore::TerminalShortcuts::isReservedForApplication(applicationEvent)) << sequence.toString().toStdString();
        EXPECT_FALSE(terminalcore::TerminalShortcuts::isTerminalOwned(applicationEvent)) << sequence.toString().toStdString();
    }

    // Zooming answers the plain key of the market convention, so increasing never needs a third key.
    QKeyEvent plainIncrease(QEvent::KeyPress, Qt::Key_Equal, Qt::ControlModifier);
    QKeyEvent plainDecrease(QEvent::KeyPress, Qt::Key_Minus, Qt::ControlModifier);
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isReservedForApplication(plainIncrease));
    EXPECT_TRUE(terminalcore::TerminalShortcuts::isReservedForApplication(plainDecrease));
}

TEST(LayoutManagerTest, ChangesPresetWithoutLosingOverflowSessions) {
    domain::SlotLayoutState layout{QStringLiteral("4-grid"), 4, {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}, {QStringLiteral("e")}};
    ASSERT_TRUE(plugins::terminalplugin::workspace::LayoutManager::changePreset(layout, QStringLiteral("2-columns")).hasValue());
    EXPECT_EQ(layout.presetId, QStringLiteral("2-columns"));
    EXPECT_EQ(layout.slotCount, 2);
    EXPECT_EQ(layout.slotAssignments.size(), 2);
    EXPECT_EQ(layout.shelf, QVector<QString>({QStringLiteral("e"), QStringLiteral("c"), QStringLiteral("d")}));

    ASSERT_TRUE(plugins::terminalplugin::workspace::LayoutManager::changePreset(layout, QStringLiteral("4-grid")).hasValue());
    EXPECT_EQ(layout.slotAssignments.size(), 4);
    EXPECT_FALSE(layout.slotAssignments.at(2).has_value());
}

TEST(LayoutManagerTest, AssignsSwapsShelvesRemovesAndValidatesSlots) {
    domain::SlotLayoutState layout{QStringLiteral("2-columns"), 2, {QStringLiteral("a"), QStringLiteral("b")}, {QStringLiteral("c"), QStringLiteral("d")}};
    EXPECT_TRUE(plugins::terminalplugin::workspace::LayoutManager::contains(layout, QStringLiteral("a")));
    EXPECT_TRUE(plugins::terminalplugin::workspace::LayoutManager::contains(layout, QStringLiteral("c")));
    EXPECT_FALSE(plugins::terminalplugin::workspace::LayoutManager::contains(layout, QStringLiteral("missing")));
    EXPECT_EQ(plugins::terminalplugin::workspace::LayoutManager::visibleSlotIndex(layout, QStringLiteral("b")), 1);
    EXPECT_EQ(plugins::terminalplugin::workspace::LayoutManager::visibleSlotIndex(layout, QStringLiteral("c")), -1);

    ASSERT_TRUE(plugins::terminalplugin::workspace::LayoutManager::assignToSlot(layout, QStringLiteral("a"), 1).hasValue());
    EXPECT_EQ(layout.slotAssignments.at(0), std::optional<QString>(QStringLiteral("b")));
    EXPECT_EQ(layout.slotAssignments.at(1), std::optional<QString>(QStringLiteral("a")));

    ASSERT_TRUE(plugins::terminalplugin::workspace::LayoutManager::assignToSlot(layout, QStringLiteral("c"), 0).hasValue());
    EXPECT_EQ(layout.slotAssignments.at(0), std::optional<QString>(QStringLiteral("c")));
    EXPECT_EQ(layout.shelf.first(), QStringLiteral("b"));

    plugins::terminalplugin::workspace::LayoutManager::moveToShelf(layout, QStringLiteral("a"), 1);
    EXPECT_FALSE(layout.slotAssignments.at(1).has_value());
    EXPECT_EQ(layout.shelf.at(1), QStringLiteral("a"));
    plugins::terminalplugin::workspace::LayoutManager::moveToShelf(layout, QStringLiteral("c"));
    EXPECT_EQ(layout.shelf.last(), QStringLiteral("c"));

    plugins::terminalplugin::workspace::LayoutManager::remove(layout, QStringLiteral("c"));
    EXPECT_FALSE(plugins::terminalplugin::workspace::LayoutManager::contains(layout, QStringLiteral("c")));
}

TEST(TerminalSettingsTest, LoadsMutatesAndSignalsValidSettings) {
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalSettingsStore settings(host);
    ASSERT_TRUE(settings.initialize().hasValue());
    ASSERT_FALSE(ui::Components::monospacedFontFamilies().isEmpty());
    EXPECT_EQ(settings.fontFamily(), ui::Components::defaultMonospacedFontFamily());
    EXPECT_EQ(settings.themeId(), QStringLiteral("balanced"));
    EXPECT_EQ(settings.fontSize(), plugins::terminalplugin::defaultTerminalFontSize);
    EXPECT_FALSE(settings.allowClipboardWrite());
    EXPECT_FALSE(settings.openLinksInSystemBrowser());
    EXPECT_TRUE(host.savedSettings.isEmpty());

    QSignalSpy fontChanged(&settings, &plugins::terminalplugin::TerminalSettingsStore::fontChanged);
    QSignalSpy themeChanged(&settings, &plugins::terminalplugin::TerminalSettingsStore::themeChanged);
    QSignalSpy clipboardChanged(&settings, &plugins::terminalplugin::TerminalSettingsStore::allowClipboardWriteChanged);
    QSignalSpy linkDestinationChanged(&settings, &plugins::terminalplugin::TerminalSettingsStore::openLinksInSystemBrowserChanged);
    settings.setFontFamily(ui::Components::monospacedFontFamilies().last());
    settings.setThemeId(QStringLiteral("vivid"));
    settings.setAllowClipboardWrite(true);
    settings.setOpenLinksInSystemBrowser(true);
    EXPECT_EQ(settings.fontFamily(), ui::Components::monospacedFontFamilies().last());
    EXPECT_EQ(settings.themeId(), QStringLiteral("vivid"));
    EXPECT_EQ(fontChanged.count(), ui::Components::monospacedFontFamilies().size() > 1 ? 1 : 0);
    EXPECT_EQ(themeChanged.count(), 1);
    EXPECT_TRUE(settings.allowClipboardWrite());
    EXPECT_EQ(clipboardChanged.count(), 1);
    EXPECT_TRUE(settings.openLinksInSystemBrowser());
    EXPECT_EQ(linkDestinationChanged.count(), 1);

    settings.setThemeId(QStringLiteral("unknown"));
    settings.setFontFamily(QStringLiteral("unknown"));
    EXPECT_EQ(fontChanged.count(), ui::Components::monospacedFontFamilies().size() > 1 ? 1 : 0);
    EXPECT_EQ(themeChanged.count(), 1);
}

TEST(TerminalSettingsTest, StepsItsOwnFontSizeWithinTheSupportedRange) {
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalSettingsStore settings(host);
    ASSERT_TRUE(settings.initialize().hasValue());
    QSignalSpy sizeChanged(&settings, &plugins::terminalplugin::TerminalSettingsStore::fontSizeChanged);

    settings.stepFontSize(ui::ContentFontStep::Increase);
    EXPECT_EQ(settings.fontSize(), plugins::terminalplugin::defaultTerminalFontSize + 1);
    settings.stepFontSize(ui::ContentFontStep::Decrease);
    EXPECT_EQ(settings.fontSize(), plugins::terminalplugin::defaultTerminalFontSize);
    EXPECT_EQ(sizeChanged.count(), 2);

    settings.setFontSize(ui::maximumContentFontSize);
    settings.stepFontSize(ui::ContentFontStep::Increase);
    EXPECT_EQ(settings.fontSize(), ui::maximumContentFontSize);
    settings.setFontSize(ui::minimumContentFontSize);
    settings.stepFontSize(ui::ContentFontStep::Decrease);
    EXPECT_EQ(settings.fontSize(), ui::minimumContentFontSize);

    settings.stepFontSize(ui::ContentFontStep::Reset);
    EXPECT_EQ(settings.fontSize(), plugins::terminalplugin::defaultTerminalFontSize);

    const qsizetype stepped = sizeChanged.count();
    settings.setFontSize(ui::minimumContentFontSize - 1);
    settings.setFontSize(ui::maximumContentFontSize + 1);
    EXPECT_EQ(settings.fontSize(), plugins::terminalplugin::defaultTerminalFontSize);
    EXPECT_EQ(sizeChanged.count(), stepped);
}

TEST(TerminalSettingsTest, TakesTheDeclaredDefaultForEveryStoredValueItCannotUse) {
    test::TestPluginHost host;
    host.settingsDocument = {{QStringLiteral("fontFamily"), ui::Components::defaultMonospacedFontFamily()}, {QStringLiteral("fontSize"), ui::maximumContentFontSize + 1}, {QStringLiteral("themeId"), QStringLiteral("balanced")}, {QStringLiteral("openLinksInSystemBrowser"), QStringLiteral("yes")}};
    plugins::terminalplugin::TerminalSettingsStore settings(host);
    ASSERT_TRUE(settings.initialize().hasValue());
    EXPECT_EQ(settings.fontSize(), plugins::terminalplugin::defaultTerminalFontSize);
    EXPECT_FALSE(settings.openLinksInSystemBrowser());

    test::TestPluginHost malformedHost;
    malformedHost.settingsDocument = {{QStringLiteral("fontFamily"), QStringLiteral("missing")}, {QStringLiteral("fontSize"), 13.5}, {QStringLiteral("themeId"), QStringLiteral("nothing")}, {QStringLiteral("nobodyDeclaresThis"), true}};
    plugins::terminalplugin::TerminalSettingsStore malformed(malformedHost);
    ASSERT_TRUE(malformed.initialize().hasValue());
    EXPECT_EQ(malformed.fontFamily(), ui::Components::defaultMonospacedFontFamily());
    EXPECT_EQ(malformed.fontSize(), plugins::terminalplugin::defaultTerminalFontSize);
    EXPECT_EQ(malformed.themeId(), QStringLiteral("balanced"));

    test::TestPluginHost writeHost;
    plugins::terminalplugin::TerminalSettingsStore stored(writeHost);
    ASSERT_TRUE(stored.initialize().hasValue());
    // clang-format off
    writeHost.settingsFutureHandler = [](const QJsonObject&) { return QtFuture::makeReadyValueFuture(Result<void>::failure({"write_failed", "Write failed", {}})); };
    // clang-format on
    stored.setThemeId(QStringLiteral("vivid"));
    EXPECT_EQ(stored.themeId(), QStringLiteral("balanced"));
    ASSERT_EQ(writeHost.notifications.size(), 1);
    EXPECT_EQ(writeHost.notifications.first().severity, plugins::AlertSeverity::Error);
}

// A field the writer or the reader forgot is a preference the reader loses on the next start, so every one of them travels both ways.
TEST(TerminalSettingsStoreTest, CarriesEveryFieldOfItsSettingsThroughTheDocumentAndBack) {
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalSettingsStore store(host);
    ASSERT_TRUE(store.initialize().hasValue());

    const QString family = ui::Components::monospacedFontFamilies().isEmpty() ? ui::Components::defaultMonospacedFontFamily() : ui::Components::monospacedFontFamilies().last();
    store.setFontFamily(family);
    store.setFontSize(19);
    store.setThemeId(QStringLiteral("vivid"));
    store.setAllowClipboardWrite(true);
    store.setOpenLinksInSystemBrowser(true);

    ASSERT_FALSE(host.savedSettings.isEmpty());
    host.settingsDocument = host.savedSettings.last();

    plugins::terminalplugin::TerminalSettingsStore reopened(host);
    ASSERT_TRUE(reopened.initialize().hasValue());
    EXPECT_EQ(reopened.fontFamily(), family);
    EXPECT_EQ(reopened.fontSize(), 19);
    EXPECT_EQ(reopened.themeId(), QStringLiteral("vivid"));
    EXPECT_TRUE(reopened.allowClipboardWrite());
    EXPECT_TRUE(reopened.openLinksInSystemBrowser());
}

TEST(ShellProfileTest, QuotesLocalPathsAndResolvesExecutableProfiles) {
    const terminalcore::ShellProfile profile{QStringLiteral("zsh"), QStringLiteral("zsh"), QStringLiteral("/bin/zsh"), {}};
#ifdef Q_OS_WIN
    EXPECT_EQ(terminalcore::ShellPaths::formatLocalPathsForShell(profile, {QStringLiteral("C:/one path"), QStringLiteral("C:/it's")}), QStringLiteral("'C:/one path' 'C:/it''s' "));
#else
    EXPECT_EQ(terminalcore::ShellPaths::formatLocalPathsForShell(profile, {QStringLiteral("/one path"), QStringLiteral("/it's")}), QStringLiteral("'/one path' '/it'\\''s' "));
#endif
    EXPECT_EQ(terminalcore::ShellPaths::formatLocalPathsForShell(profile, {}), QStringLiteral(" "));

    const auto system = terminalcore::ShellProfileResolver::systemDefault();
    EXPECT_FALSE(system.id.isEmpty());
    EXPECT_FALSE(system.name.isEmpty());
    EXPECT_TRUE(QFileInfo(system.executable).isExecutable());
    const auto available = terminalcore::ShellProfileResolver::availableProfiles();
    EXPECT_FALSE(available.isEmpty());
    EXPECT_EQ(available.first().executable, system.executable);

#ifndef Q_OS_WIN
    // An environment that declares no shell falls to the account record, which is read into a buffer of our own.
    const QByteArray declaredShell = qgetenv("SHELL");
    qunsetenv("SHELL");
    const auto fromAccount = terminalcore::ShellProfileResolver::systemDefault();

    if (!declaredShell.isEmpty()) {
        qputenv("SHELL", declaredShell);
    }

    EXPECT_FALSE(fromAccount.id.isEmpty());
    EXPECT_TRUE(QFileInfo(fromAccount.executable).isExecutable());
#endif
}

#ifndef Q_OS_WIN
TEST(PosixPtyBackendTest, DeliversWhatAProgramWroteBeforeItReportsThatItEnded) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    terminalcore::PosixPtyBackend backend;
    QByteArray received;
    bool wroteBeforeExit = false;
    bool exited = false;
    // clang-format off
    QObject::connect(&backend, &terminalcore::IPtyBackend::outputReady, &backend, [&received](const QByteArray& bytes) { received.append(bytes); });
    QObject::connect(&backend, &terminalcore::IPtyBackend::processExited, &backend, [&](int) { wroteBeforeExit = received.contains(QByteArrayLiteral("SDMARK")); exited = true; });
    // clang-format on

    const terminalcore::ShellProfile profile = terminalcore::ShellProfileResolver::systemDefault();
    ASSERT_TRUE(backend.start(profile, directory.path(), directory.filePath(QStringLiteral("history")), 80, 24).hasValue());
    ASSERT_TRUE(backend.running());
    ASSERT_TRUE(backend.write(QByteArrayLiteral("printf 'SD%s\\n' MARK; exit 0\n")).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return exited; }));
    // clang-format on

    EXPECT_TRUE(wroteBeforeExit);
    EXPECT_FALSE(backend.running());
}

// A shell whose dotfiles work in the terminal of its own system works here, so each platform opens one the way that terminal does.
TEST(PosixPtyBackendTest, StartsTheShellTheWayThePlatformOpensOne) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    terminalcore::PosixPtyBackend backend;
    QByteArray received;
    bool exited = false;
    // clang-format off
    QObject::connect(&backend, &terminalcore::IPtyBackend::outputReady, &backend, [&received](const QByteArray& bytes) { received.append(bytes); });
    QObject::connect(&backend, &terminalcore::IPtyBackend::processExited, &backend, [&exited](int) { exited = true; });
    // clang-format on

    // The shell every platform carries is the one asked, because what a reader happens to have set answers its own name differently.
    const terminalcore::ShellProfile profile{QStringLiteral("sh"), QStringLiteral("sh"), QStringLiteral("/bin/sh"), {}};
    ASSERT_TRUE(backend.start(profile, directory.path(), directory.filePath(QStringLiteral("history")), 80, 24).hasValue());
    ASSERT_TRUE(backend.write(QByteArrayLiteral("printf 'NAME%s\\n' \"$0\"; exit 0\n")).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return exited; }));
    // clang-format on

    // A login shell is the one whose own name opens with a hyphen, and macOS is where the window is given no profile to inherit.
#ifdef Q_OS_MACOS
    EXPECT_TRUE(received.contains(QByteArrayLiteral("NAME-"))) << received.toStdString();
#else
    EXPECT_TRUE(received.contains(QByteArrayLiteral("NAMEsh"))) << received.toStdString();
    EXPECT_FALSE(received.contains(QByteArrayLiteral("NAME-"))) << received.toStdString();
#endif
}
#endif

TEST(TerminalThemeCatalogTest, ProvidesCompleteThemesAndRejectsUnknownTheme) {
    const auto& themes = terminalcore::TerminalThemes::terminalThemes();
    ASSERT_EQ(themes.size(), 3);

    for (const auto& theme : themes) {
        EXPECT_TRUE(terminalcore::TerminalThemes::terminalThemeExists(theme.id));
        ASSERT_NE(terminalcore::TerminalThemes::terminalTheme(theme.id), nullptr);
        EXPECT_EQ(terminalcore::TerminalThemes::terminalTheme(theme.id)->id, theme.id);
        EXPECT_TRUE(theme.foreground.isValid());
        EXPECT_TRUE(theme.background.isValid());
        EXPECT_TRUE(theme.cursor.isValid());
        for (const auto& color : theme.ansiPalette) {
            EXPECT_TRUE(color.isValid());
        }
    }

    EXPECT_FALSE(terminalcore::TerminalThemes::terminalThemeExists(QStringLiteral("unknown")));
    EXPECT_EQ(terminalcore::TerminalThemes::terminalTheme(QStringLiteral("unknown")), nullptr);
}

TEST(GhosttyTerminalAdapterTest, ReportsPreInitializationErrorsAndRendersTerminalData) {
    terminalcore::GhosttyTerminalAdapter adapter;
    const auto theme = *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced"));
    EXPECT_EQ(adapter.setTheme(theme).error().code, QStringLiteral("ghostty_not_initialized"));
    EXPECT_EQ(adapter.resize(80, 24, 8, 16).error().code, QStringLiteral("ghostty_not_initialized"));
    QKeyEvent preInitializationKey(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    EXPECT_EQ(adapter.encodeKey(preInitializationKey).error().code, QStringLiteral("ghostty_not_initialized"));
    EXPECT_EQ(adapter.snapshot().columns, 1);

    EXPECT_EQ(adapter.initialize(0, 0, 8, 16, theme).error().code, QStringLiteral("ghostty_size_invalid"));
    ASSERT_TRUE(adapter.initialize(1, 1, 8, 16, theme).hasValue());
    EXPECT_EQ(adapter.snapshot().columns, 1);
    EXPECT_EQ(adapter.snapshot().rows, 1);
    EXPECT_EQ(adapter.resize(32767, 32767, 8, 16).error().code, QStringLiteral("ghostty_size_invalid"));
    EXPECT_EQ(adapter.resize(80, 24, 0, 16).error().code, QStringLiteral("ghostty_size_invalid"));
    ASSERT_TRUE(adapter.resize(80, 24, 8, 16).hasValue());
    adapter.write(QByteArrayLiteral("hello"));
    const auto snapshot = adapter.snapshot();
    EXPECT_EQ(snapshot.columns, 80);
    EXPECT_EQ(snapshot.rows, 24);
    EXPECT_EQ(snapshot.background, theme.background);
    EXPECT_FALSE(snapshot.cells.isEmpty());

    // A program that did not ask for the markers receives the text as it was written.
    const auto plain = adapter.encodePaste(QByteArrayLiteral("hello"));
    ASSERT_TRUE(plain.hasValue());
    EXPECT_EQ(plain.value(), QByteArrayLiteral("hello"));

    // A program that turned bracketed paste on receives the text between the markers it asked for.
    adapter.write(QByteArrayLiteral("\x1b[?2004h"));
    const auto bracketed = adapter.encodePaste(QByteArrayLiteral("hello"));
    ASSERT_TRUE(bracketed.hasValue());
    EXPECT_EQ(bracketed.value(), QByteArrayLiteral("\x1b[200~hello\x1b[201~"));

    // A shell interrupts on the physical control key, which Qt reports as the meta modifier on macOS and as the control one everywhere else.
    QKeyEvent interrupt(QEvent::KeyPress, Qt::Key_C, terminalcore::terminalControlModifier, QStringLiteral("c"));
    const auto encodedInterrupt = adapter.encodeKey(interrupt);
    ASSERT_TRUE(encodedInterrupt.hasValue());
    EXPECT_EQ(encodedInterrupt.value(), QByteArrayLiteral("\x03"));

    QKeyEvent typed(QEvent::KeyPress, Qt::Key_A, terminalcore::terminalControlModifier, QStringLiteral("a"));
    const auto encoded = adapter.encodeKey(typed);
    ASSERT_TRUE(encoded.hasValue());
    EXPECT_FALSE(encoded.value().isEmpty());

#ifdef Q_OS_MACOS
    // The application modifier is Command, which the shell never sees, so it must not reach it as an interrupt.
    QKeyEvent command(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QStringLiteral("c"));
    const auto encodedCommand = adapter.encodeKey(command);
    ASSERT_TRUE(encodedCommand.hasValue());
    EXPECT_NE(encodedCommand.value(), QByteArrayLiteral("\x03"));
#endif
    EXPECT_TRUE(adapter.setTheme(*terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());
    adapter.scrollViewport(1);
    adapter.scrollViewport(0);
    adapter.scrollToRow(0);
    adapter.scrollToTop();
    adapter.scrollToBottom();
}

TEST(GhosttyTerminalAdapterTest, AnswersTheQueriesAProgramSendsAndReportsTheBell) {
    terminalcore::GhosttyTerminalAdapter adapter;
    ASSERT_TRUE(adapter.initialize(80, 24, 8, 16, *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());
    QSignalSpy responses(&adapter, &terminalcore::GhosttyTerminalAdapter::responseReady);
    QSignalSpy bells(&adapter, &terminalcore::GhosttyTerminalAdapter::bellRang);

    // A program asking what this terminal is receives a colour VT220 instead of silence.
    adapter.write(QByteArrayLiteral("\x1b[c"));
    ASSERT_EQ(responses.count(), 1);
    EXPECT_EQ(responses.takeFirst().first().toByteArray(), QByteArrayLiteral("\x1b[?62;22c"));

    // A program asking how large the window is receives the geometry the widget declared.
    adapter.write(QByteArrayLiteral("\x1b[18t"));
    ASSERT_EQ(responses.count(), 1);
    EXPECT_EQ(responses.takeFirst().first().toByteArray(), QByteArrayLiteral("\x1b[8;24;80t"));
    adapter.write(QByteArrayLiteral("\x1b[16t"));
    ASSERT_EQ(responses.count(), 1);
    EXPECT_EQ(responses.takeFirst().first().toByteArray(), QByteArrayLiteral("\x1b[6;16;8t"));

    // A program adapting its colours is told which side the background is on.
    adapter.write(QByteArrayLiteral("\x1b[?996n"));
    ASSERT_EQ(responses.count(), 1);
    EXPECT_EQ(responses.takeFirst().first().toByteArray(), QByteArrayLiteral("\x1b[?997;1n"));

    // A program asking which terminal it is talking to receives the name of this product.
    adapter.write(QByteArrayLiteral("\x1b[>q"));
    ASSERT_EQ(responses.count(), 1);
    EXPECT_TRUE(responses.takeFirst().first().toByteArray().contains(QByteArrayLiteral("Workpane")));

    // The bell is reported instead of being swallowed, because a program ringing it is telling the reader something.
    adapter.write(QByteArrayLiteral("\a"));
    EXPECT_EQ(bells.count(), 1);
}

TEST(GhosttyTerminalAdapterTest, LetsAProgramWriteToTheClipboardOnlyWhileTheUserAllowsIt) {
    terminalcore::GhosttyTerminalAdapter adapter;
    ASSERT_TRUE(adapter.initialize(40, 4, 8, 16, *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());
    QSignalSpy writes(&adapter, &terminalcore::GhosttyTerminalAdapter::clipboardWriteRequested);

    // Anything printed to a terminal can ask for the clipboard, so the request is refused until the user allows it.
    adapter.write(QByteArrayLiteral("\x1b]52;c;eWFua2Vk\x1b\\"));
    EXPECT_TRUE(writes.isEmpty());

    adapter.setClipboardWriteAllowed(true);
    adapter.write(QByteArrayLiteral("\x1b]52;c;eWFua2Vk\x1b\\"));
    ASSERT_EQ(writes.count(), 1);
    EXPECT_EQ(writes.takeFirst().first().toString(), QStringLiteral("yanked"));
}

TEST(GhosttyTerminalAdapterTest, ShowsTheNotificationAProgramPosts) {
    terminalcore::GhosttyTerminalAdapter adapter;
    ASSERT_TRUE(adapter.initialize(40, 4, 8, 16, *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());
    QSignalSpy notifications(&adapter, &terminalcore::GhosttyTerminalAdapter::notificationPosted);

    // A program telling the reader that something finished is shown where every other message of the application is shown.
    adapter.write(QByteArrayLiteral("\x1b]777;notify;Build;finished\x1b\\"));
    ASSERT_EQ(notifications.count(), 1);
    EXPECT_EQ(notifications.first().at(0).toString(), QStringLiteral("Build"));
    EXPECT_EQ(notifications.first().at(1).toString(), QStringLiteral("finished"));
}

TEST(GhosttyTerminalAdapterTest, CarriesTheCursorShapeTheProgramAsksFor) {
    terminalcore::GhosttyTerminalAdapter adapter;
    ASSERT_TRUE(adapter.initialize(20, 4, 8, 16, *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());

    // The shape and the blinking of the cursor come from the program, because an editor changing mode says so with them.
    const auto blockCursor = adapter.snapshot();
    EXPECT_EQ(blockCursor.cursorStyle, terminalcore::CursorStyle::Block);
    adapter.write(QByteArrayLiteral("\x1b[5 q"));
    const auto barCursor = adapter.snapshot();
    EXPECT_EQ(barCursor.cursorStyle, terminalcore::CursorStyle::Bar);
    EXPECT_TRUE(barCursor.cursorBlinking);
    adapter.write(QByteArrayLiteral("\x1b[4 q"));
    const auto underlineCursor = adapter.snapshot();
    EXPECT_EQ(underlineCursor.cursorStyle, terminalcore::CursorStyle::Underline);
    EXPECT_FALSE(underlineCursor.cursorBlinking);
}

TEST(GhosttyTerminalAdapterTest, ReadsTheAddressMarkedOnACellAndTheOneWrittenAsPlainText) {
    terminalcore::GhosttyTerminalAdapter adapter;
    ASSERT_TRUE(adapter.initialize(60, 4, 8, 16, *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());
    adapter.write(QByteArrayLiteral("\x1b]8;;https://example.com/report\x1b\\report\x1b]8;;\x1b\\ plain\r\n"));
    adapter.write(QByteArrayLiteral("see https://example.org/page. now\r\n"));
    adapter.write(QByteArrayLiteral("ssh://example.net not-an-address\r\n"));

    // The address a program marked belongs to the cells it marked and to no other.
    EXPECT_EQ(adapter.addressAt(QPointF(12, 8)), QStringLiteral("https://example.com/report"));
    EXPECT_TRUE(adapter.addressAt(QPointF(60, 8)).isEmpty());

    // An address written as plain text is the word around the pointer, without the punctuation that closed the sentence.
    EXPECT_EQ(adapter.addressAt(QPointF(60, 24)), QStringLiteral("https://example.org/page"));
    EXPECT_TRUE(adapter.addressAt(QPointF(4, 24)).isEmpty());

    // A scheme the application cannot open is not offered as one.
    EXPECT_TRUE(adapter.addressAt(QPointF(20, 40)).isEmpty());
}

TEST(GhosttyTerminalAdapterTest, FindsAQueryThroughTheHistoryAndRevealsWhereItIs) {
    terminalcore::GhosttyTerminalAdapter adapter;
    ASSERT_TRUE(adapter.initialize(40, 4, 8, 16, *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());
    adapter.write(QByteArrayLiteral("needle at the top\r\n"));

    for (int line = 0; line < 30; ++line) {
        adapter.write(QStringLiteral("filler %1\r\n").arg(line).toUtf8());
    }

    adapter.write(QByteArrayLiteral("needless and needle again\r\n"));

    // A query is answered against the history as well as against the rows on view.
    const auto matches = adapter.search(QStringLiteral("needle"), true, false, 100);
    ASSERT_EQ(matches.size(), 3);
    EXPECT_EQ(matches.first().row, 0U);
    EXPECT_EQ(matches.first().column, 0);
    EXPECT_EQ(matches.first().length, 6);
    EXPECT_EQ(matches.last().row, 31U);
    EXPECT_EQ(matches.last().column, 13);

    // The whole-word option refuses the run that only starts with the query.
    const auto wholeWord = adapter.search(QStringLiteral("needle"), true, true, 100);
    ASSERT_EQ(wholeWord.size(), 2);
    EXPECT_EQ(wholeWord.last().column, 13);

    // Case is compared only when the caller asks for it, and the bound is what the caller declared.
    EXPECT_EQ(adapter.search(QStringLiteral("NEEDLE"), true, false, 100).size(), 0);
    EXPECT_EQ(adapter.search(QStringLiteral("NEEDLE"), false, false, 100).size(), 3);
    EXPECT_EQ(adapter.search(QStringLiteral("needle"), true, false, 2).size(), 2);
    EXPECT_TRUE(adapter.search(QString(), true, false, 100).isEmpty());

    // Revealing a match selects it and moves the viewport to where it is.
    ASSERT_TRUE(adapter.revealMatch(matches.first()));
    EXPECT_EQ(adapter.selectionText(), QStringLiteral("needle"));
    EXPECT_EQ(adapter.snapshot().scrollOffset, 0U);
    ASSERT_TRUE(adapter.revealMatch(matches.last()));
    EXPECT_GT(adapter.snapshot().scrollOffset, 0U);
}

// A drag held outside the grid moves the viewport under it, which is what selects more than the rows on screen.
TEST(GhosttyTerminalAdapterTest, MovesTheViewportUnderADragHeldOutsideTheGrid) {
    terminalcore::GhosttyTerminalAdapter adapter;
    ASSERT_TRUE(adapter.initialize(20, 4, 8, 16, *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());

    for (int line = 0; line < 20; ++line) {
        adapter.write(QStringLiteral("line %1\r\n").arg(line).toUtf8());
    }

    // clang-format off
    const auto cellCenter = [](int column, int row) { return QPointF(column * 8 + 4, row * 16 + 8); };
    // clang-format on
    constexpr quint64 repeatInterval = 500'000'000;
    constexpr double repeatDistance = 4;

    // Nothing is being dragged, so there is nothing to advance and nothing fails.
    EXPECT_EQ(adapter.selectionAutoscroll(), terminalcore::SelectionAutoscroll::None);
    EXPECT_TRUE(adapter.advanceSelectionAutoscroll(cellCenter(0, 0), false).hasValue());

    const quint64 restingOffset = adapter.snapshot().scrollOffset;
    ASSERT_TRUE(adapter.beginSelection(cellCenter(2, 3), 0, repeatInterval, repeatDistance, false).hasValue());

    // The pointer is held above the first row, which is where the viewport has to follow it.
    const QPointF aboveTheGrid(16, -24);
    ASSERT_TRUE(adapter.extendSelection(aboveTheGrid, false).hasValue());
    ASSERT_EQ(adapter.selectionAutoscroll(), terminalcore::SelectionAutoscroll::Up);

    ASSERT_TRUE(adapter.advanceSelectionAutoscroll(aboveTheGrid, false).hasValue());
    EXPECT_LT(adapter.snapshot().scrollOffset, restingOffset) << "the viewport did not follow the drag toward the older rows";

    // Holding it there keeps moving, so a drag reaches rows the screen never showed.
    const quint64 afterOneStep = adapter.snapshot().scrollOffset;
    ASSERT_TRUE(adapter.advanceSelectionAutoscroll(aboveTheGrid, false).hasValue());
    EXPECT_LT(adapter.snapshot().scrollOffset, afterOneStep);
    EXPECT_TRUE(adapter.hasSelection());

    adapter.endSelection(aboveTheGrid);
    EXPECT_EQ(adapter.selectionAutoscroll(), terminalcore::SelectionAutoscroll::None);
}

TEST(GhosttyTerminalAdapterTest, SelectsWordsLinesAndDragsThroughTheEmulator) {
    terminalcore::GhosttyTerminalAdapter adapter;
    ASSERT_TRUE(adapter.initialize(20, 4, 8, 16, *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());
    adapter.write(QByteArrayLiteral("hello world\r\nsecond line"));

    // clang-format off
    const auto cellCenter = [](int column, int row) { return QPointF(column * 8 + 4, row * 16 + 8); };
    // clang-format on
    constexpr quint64 repeatInterval = 500'000'000;
    constexpr double repeatDistance = 4;

    EXPECT_FALSE(adapter.hasSelection());
    EXPECT_TRUE(adapter.selectionText().isEmpty());

    // A drag selects exactly the cells it crossed, and the cell under the pointer joins once the pointer is past its middle.
    const QPointF pastTheFifthCell(4 * 8 + 6, 8);
    ASSERT_TRUE(adapter.beginSelection(cellCenter(0, 0), 0, repeatInterval, repeatDistance, false).hasValue());
    ASSERT_TRUE(adapter.extendSelection(pastTheFifthCell, false).hasValue());
    EXPECT_TRUE(adapter.hasSelection());
    EXPECT_EQ(adapter.selectionText(), QStringLiteral("hello"));
    adapter.endSelection(pastTheFifthCell);

    // The second press of a sequence takes the word under it, and the third takes the whole line.
    ASSERT_TRUE(adapter.beginSelection(cellCenter(8, 0), 1'000'000'000, repeatInterval, repeatDistance, false).hasValue());
    ASSERT_TRUE(adapter.beginSelection(cellCenter(8, 0), 1'100'000'000, repeatInterval, repeatDistance, false).hasValue());
    EXPECT_EQ(adapter.selectionText(), QStringLiteral("world"));
    ASSERT_TRUE(adapter.beginSelection(cellCenter(8, 0), 1'200'000'000, repeatInterval, repeatDistance, false).hasValue());
    EXPECT_EQ(adapter.selectionText(), QStringLiteral("hello world"));
    adapter.endSelection(cellCenter(8, 0));

    // Everything the terminal holds is selected at once, which is what the select-all action asks for.
    adapter.selectAll();
    EXPECT_TRUE(adapter.selectionText().contains(QStringLiteral("hello world")));
    EXPECT_TRUE(adapter.selectionText().contains(QStringLiteral("second line")));

    // The rows the selection covers are the ones the renderer marks, so the widget paints it without owning it.
    const auto snapshot = adapter.snapshot();
    // clang-format off
    const auto selectedCells = std::count_if(snapshot.cells.constBegin(), snapshot.cells.constEnd(), [](const terminalcore::RenderCell& cell) { return cell.selected; });
    // clang-format on
    EXPECT_GT(selectedCells, 0);

    adapter.clearSelection();
    EXPECT_FALSE(adapter.hasSelection());
    EXPECT_TRUE(adapter.selectionText().isEmpty());

    // A line that scrolled out of the screen is still selectable, because the selection belongs to the terminal and not to the viewport.
    for (int line = 0; line < 20; ++line) {
        adapter.write(QStringLiteral("line %1\r\n").arg(line).toUtf8());
    }

    adapter.scrollToTop();
    ASSERT_TRUE(adapter.beginSelection(cellCenter(0, 0), 2'000'000'000, repeatInterval, repeatDistance, false).hasValue());
    ASSERT_TRUE(adapter.beginSelection(cellCenter(0, 0), 2'100'000'000, repeatInterval, repeatDistance, false).hasValue());
    ASSERT_TRUE(adapter.beginSelection(cellCenter(0, 0), 2'200'000'000, repeatInterval, repeatDistance, false).hasValue());
    EXPECT_EQ(adapter.selectionText(), QStringLiteral("hello world"));
}

TEST(GhosttyTerminalAdapterTest, ReportsTheMouseAndTheFocusOnlyToAProgramThatAskedForThem) {
    terminalcore::GhosttyTerminalAdapter adapter;
    ASSERT_TRUE(adapter.initialize(80, 24, 8, 16, *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("vivid"))).hasValue());

    terminalcore::MouseReport report;
    report.action = terminalcore::MouseAction::Press;
    report.button = terminalcore::MouseButton::Left;
    report.position = QPointF(0, 0);

    // A program that asked for nothing receives nothing, and the widget keeps the gesture for its own selection.
    EXPECT_FALSE(adapter.programWantsMouse());
    EXPECT_FALSE(adapter.programWantsFocus());

    adapter.write(QByteArrayLiteral("\x1b[?1000h"));
    EXPECT_TRUE(adapter.programWantsMouse());
    const auto legacy = adapter.encodeMouse(report);
    ASSERT_TRUE(legacy.hasValue());
    EXPECT_EQ(legacy.value(), QByteArrayLiteral("\x1b[M\x20\x21\x21"));

    // The extended format names the cell in decimal, which is what a program reading a wide window depends on.
    adapter.write(QByteArrayLiteral("\x1b[?1006h"));
    report.position = QPointF(80, 64);
    report.anyButtonPressed = true;
    const auto pressed = adapter.encodeMouse(report);
    ASSERT_TRUE(pressed.hasValue());
    EXPECT_EQ(pressed.value(), QByteArrayLiteral("\x1b[<0;11;5M"));

    report.action = terminalcore::MouseAction::Release;
    report.anyButtonPressed = false;
    const auto released = adapter.encodeMouse(report);
    ASSERT_TRUE(released.hasValue());
    EXPECT_EQ(released.value(), QByteArrayLiteral("\x1b[<0;11;5m"));

    // The wheel travels as the button the protocol reserves for it rather than as history the widget scrolled itself.
    report.action = terminalcore::MouseAction::Press;
    report.button = terminalcore::MouseButton::WheelUp;
    const auto wheel = adapter.encodeMouse(report);
    ASSERT_TRUE(wheel.hasValue());
    EXPECT_EQ(wheel.value(), QByteArrayLiteral("\x1b[<64;11;5M"));

    adapter.write(QByteArrayLiteral("\x1b[?1004h"));
    EXPECT_TRUE(adapter.programWantsFocus());
    const auto gained = adapter.encodeFocus(true);
    ASSERT_TRUE(gained.hasValue());
    EXPECT_EQ(gained.value(), QByteArrayLiteral("\x1b[I"));
    const auto lost = adapter.encodeFocus(false);
    ASSERT_TRUE(lost.hasValue());
    EXPECT_EQ(lost.value(), QByteArrayLiteral("\x1b[O"));
}

TEST(WorkspaceManagerTest, SurvivesManyTerminalsCreatedClosedAndMovedAcrossTabsAndLayouts) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    // clang-format off
    terminalcore::PtyBackendFactory factory = []() { return std::unique_ptr<terminalcore::IPtyBackend>(std::make_unique<FakePtyBackend>()); };
    // clang-format on
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), std::move(factory));
    ASSERT_TRUE(manager.initialize().hasValue());

    const QStringList presets{QStringLiteral("single"), QStringLiteral("2-columns"), QStringLiteral("2-rows"), QStringLiteral("grid-4")};

    for (int round = 0; round < 40; ++round) {
        manager.createTab();
        manager.changeLayout(presets.at(round % presets.size()));
        QStringList created;
        for (int index = 0; index < 4; ++index) {
            const QString sessionId = manager.createTerminal(index);
            if (!sessionId.isEmpty()) {
                created.append(sessionId);
            }
        }
        for (const auto& sessionId : created) {
            manager.moveToShelf(sessionId);
        }
        for (const auto& sessionId : created) {
            manager.assignToSlot(sessionId, 0);
        }
        for (const auto& sessionId : created) {
            manager.closeTerminal(sessionId);
        }
        QApplication::processEvents();
        manager.closeTab(manager.currentTabIndex());
        QApplication::processEvents();
    }

    // Every round left the workspace consistent, so the reader still has exactly what the first tab opened with.
    EXPECT_EQ(manager.rowCount(), 1);
    EXPECT_EQ(manager.terminalCount(), 1);
    EXPECT_FALSE(manager.currentFocusedSessionId().isEmpty());
    EXPECT_EQ(manager.currentTabIndex(), 0);
}

TEST(WorkspaceManagerTest, ManagesTabsSessionsLayoutsAndInvalidOperations) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    host.translations = {{QStringLiteral("terminal.workspace.default-name"), QStringLiteral("Workspace")}, {QStringLiteral("terminal.workspace.default-terminal"), QStringLiteral("Terminal")}, {QStringLiteral("terminal.workspace.numbered-terminal"), QStringLiteral("Terminal %1")}, {QStringLiteral("terminal.error.layout-title"), QStringLiteral("Invalid layout")}, {QStringLiteral("terminal.error.layout-message"), QStringLiteral("Unknown layout")}, {QStringLiteral("terminal.error.slot-title"), QStringLiteral("Invalid slot")}, {QStringLiteral("terminal.error.slot-message"), QStringLiteral("Unknown slot")}};
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    QList<FakePtyBackend*> backends;
    // clang-format off
    terminalcore::PtyBackendFactory factory = [&backends]() {
        auto backend = std::make_unique<FakePtyBackend>();
        backends.append(backend.get());
        return backend;
    };
    // clang-format on
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), std::move(factory));
    ASSERT_TRUE(manager.initialize().hasValue());
    EXPECT_EQ(manager.rowCount(), 1);
    EXPECT_EQ(manager.rowCount(manager.index(0)), 0);
    EXPECT_EQ(manager.data(manager.index(0), plugins::terminalplugin::workspace::WorkspaceManager::NameRole).toString(), QStringLiteral("terminal.workspace.numbered"));
    EXPECT_FALSE(manager.data(manager.index(0), Qt::DisplayRole).isValid());
    EXPECT_EQ(manager.terminalCount(), 1);
    EXPECT_FALSE(manager.currentFocusedSessionId().isEmpty());
    EXPECT_EQ(backends.size(), 1);
    EXPECT_TRUE(backends.first()->running());

    manager.renameTab(0, QStringLiteral("  Main  "));
    manager.renameTab(-1, QStringLiteral("Ignored"));
    manager.renameTab(0, QStringLiteral("   "));
    EXPECT_EQ(manager.data(manager.index(0), plugins::terminalplugin::workspace::WorkspaceManager::NameRole).toString(), QStringLiteral("Main"));
    manager.createTab();
    EXPECT_EQ(manager.rowCount(), 2);
    EXPECT_EQ(manager.currentTabIndex(), 1);
    manager.moveTab(1, 0);
    EXPECT_EQ(manager.currentTabIndex(), 0);
    manager.moveTab(-1, 0);
    manager.setCurrentTabIndex(9);
    EXPECT_EQ(manager.currentTabIndex(), 0);

    const QString firstCreated = manager.createTerminal(0);
    ASSERT_FALSE(firstCreated.isEmpty());
    EXPECT_EQ(manager.terminalCount(), 2);
    manager.changeLayout(QStringLiteral("2-columns"));
    EXPECT_EQ(manager.currentPresetId(), QStringLiteral("2-columns"));
    const QString secondCreated = manager.createTerminal(1);
    ASSERT_FALSE(secondCreated.isEmpty());
    EXPECT_EQ(manager.terminalCount(), 3);
    manager.focusSession(firstCreated);
    EXPECT_EQ(manager.currentFocusedSessionId(), firstCreated);
    manager.moveToShelf(firstCreated);
    EXPECT_TRUE(manager.currentShelf().contains(firstCreated));
    manager.activateShelvedSession(firstCreated);
    EXPECT_FALSE(manager.currentShelf().contains(firstCreated));

    QSignalSpy notifications(&manager, &plugins::terminalplugin::workspace::WorkspaceManager::notificationRequested);
    manager.changeLayout(QStringLiteral("unknown"));
    manager.assignToSlot(firstCreated, 99);
    EXPECT_EQ(notifications.count(), 2);
    manager.assignToSlot(QStringLiteral("missing"), 0);
    manager.focusSession(QStringLiteral("missing"));
    manager.moveToShelf(QStringLiteral("missing"));

    QSignalSpy closing(&manager, &plugins::terminalplugin::workspace::WorkspaceManager::terminalClosing);
    manager.closeTerminal(secondCreated);
    EXPECT_EQ(manager.terminalCount(), 2);
    EXPECT_EQ(closing.count(), 1);
    manager.closeTerminal(QStringLiteral("missing"));
    manager.closeTab(0);
    EXPECT_EQ(manager.rowCount(), 1);
    manager.shutdown(plugins::terminalplugin::workspace::WorkspaceManager::ShutdownMode::Discard);
    manager.shutdown();
}

TEST(TerminalUiTest, ConnectsWorkspacePaneShelfAndToolbarInteractions) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    host.translations = {{QStringLiteral("terminal.workspace.default-name"), QStringLiteral("Workspace")}, {QStringLiteral("terminal.workspace.default-terminal"), QStringLiteral("Terminal")}, {QStringLiteral("terminal.workspace.numbered-terminal"), QStringLiteral("Terminal %1")}, {QStringLiteral("terminal.session.process-exited"), QStringLiteral("Exited with %1")}, {QStringLiteral("terminal.shelf.accessible-name"), QStringLiteral("Shelved %1")}, {QStringLiteral("terminal.layout.slots"), QStringLiteral("%1 with %2 slots")}, {QStringLiteral("terminal.status.single"), QStringLiteral("1 terminal")}, {QStringLiteral("terminal.status.multiple"), QStringLiteral("%1 terminals")}};
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    plugins::terminalplugin::TerminalSettingsStore settings(host);
    ASSERT_TRUE(settings.initialize().hasValue());

    QList<FakePtyBackend*> backends;
    // clang-format off
    terminalcore::PtyBackendFactory factory = [&backends]() {
        auto backend = std::make_unique<FakePtyBackend>();
        backends.append(backend.get());
        return backend;
    };
    // clang-format on
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), std::move(factory));
    ASSERT_TRUE(manager.initialize().hasValue());
    const QString sessionId = manager.currentFocusedSessionId();
    auto* session = qobject_cast<terminalcore::TerminalSession*>(manager.sessionObject(sessionId));
    ASSERT_NE(session, nullptr);

    plugins::terminalplugin::TerminalPane pane(*session, host);
    EXPECT_EQ(pane.sessionId(), sessionId);
    EXPECT_EQ(pane.draggedSessionId(), sessionId);
    pane.setDropDestination({plugins::terminalplugin::SessionDropTarget::Shelf, -1});
    pane.setSelected(true);
    pane.setSelected(true);
    pane.setFocusMode(true);
    pane.setTerminalFont(QFontDatabase::systemFont(QFontDatabase::FixedFont).family(), 14);
    pane.resize(240, 300);
    pane.show();
    pane.activateWindow();
    pane.raise();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&pane]() { return pane.isVisible(); }));
    // clang-format on

    auto* terminalWidget = pane.findChild<ui::TerminalWidget*>();
    ASSERT_NE(terminalWidget, nullptr);
    EXPECT_FALSE(terminalWidget->minimumSizeHint().isEmpty());

    QSignalSpy selected(&pane, &plugins::terminalplugin::TerminalPane::selected);
    pane.focusTerminal();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&selected]() { return selected.count() >= 1; }));
    // clang-format on

    // Output is selected with the mouse and copied, which is what reading a terminal is for.
    EXPECT_FALSE(terminalWidget->hasSelection());
    EXPECT_TRUE(terminalWidget->selectedText().isEmpty());
    backends.first()->sendOutput(QByteArrayLiteral("hello world\r\n"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { terminalWidget->update(); return !terminalWidget->grab().isNull(); }));
    // clang-format on
    QTest::mousePress(terminalWidget, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    QTest::mouseMove(terminalWidget, QPoint(200, 4));
    QTest::mouseRelease(terminalWidget, Qt::LeftButton, Qt::NoModifier, QPoint(200, 4));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return terminalWidget->hasSelection(); }));
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return terminalWidget->selectedText().contains(QStringLiteral("hello")); }));
    // clang-format on

    QGuiApplication::clipboard()->clear();
    terminalWidget->copySelection();
    EXPECT_TRUE(QGuiApplication::clipboard()->text().contains(QStringLiteral("hello")));

    terminalWidget->clearSelection();
    EXPECT_FALSE(terminalWidget->hasSelection());
    EXPECT_TRUE(terminalWidget->selectedText().isEmpty());

    // An address printed as plain text is opened by the platform combination, and the plugin decides where it opens.
    QSignalSpy links(terminalWidget, &ui::TerminalWidget::linkActivated);
    backends.first()->sendOutput(QByteArrayLiteral("\r\nsee https://example.org/page. now\r\n"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { terminalWidget->selectAll(); return terminalWidget->selectedText().contains(QStringLiteral("example.org")); }));
    // clang-format on
    terminalWidget->clearSelection();

    // The size of a cell comes from the platform font, so the address is looked for across the surface instead of at a guessed pixel.
    for (int y = 2; y < terminalWidget->height() && links.isEmpty(); y += 4) {
        for (int x = 2; x < terminalWidget->width() && links.isEmpty(); x += 4) {
            QTest::mousePress(terminalWidget, Qt::LeftButton, terminalcore::applicationModifier, QPoint(x, y));
            QTest::mouseRelease(terminalWidget, Qt::LeftButton, terminalcore::applicationModifier, QPoint(x, y));
        }
    }

    ASSERT_EQ(links.count(), 1);
    EXPECT_EQ(links.first().first().toString(), QStringLiteral("https://example.org/page"));
    terminalWidget->clearSelection();

    // Searching reads what the terminal already printed, reports how many results there are and selects the one being read.
    backends.first()->sendOutput(QByteArrayLiteral("alpha beta alpha\r\n"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { terminalWidget->selectAll(); return terminalWidget->selectedText().contains(QStringLiteral("alpha beta alpha")); }));
    // clang-format on
    terminalWidget->clearSelection();
    terminalWidget->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(terminalWidget, Qt::Key_F, terminalcore::applicationModifier);
    auto* findBar = terminalWidget->findChild<ui::FindBar*>(QStringLiteral("findBar"));
    ASSERT_NE(findBar, nullptr);
    EXPECT_TRUE(findBar->isVisible());
    auto* findQuery = findBar->findChild<QLineEdit*>(QStringLiteral("findQuery"));
    ASSERT_NE(findQuery, nullptr);
    findQuery->setText(QStringLiteral("alpha"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return terminalWidget->selectedText() == QStringLiteral("alpha"); }));
    // clang-format on
    auto* findStatus = findBar->findChild<QLabel*>();
    ASSERT_NE(findStatus, nullptr);
    EXPECT_EQ(findStatus->text(), QStringLiteral("1/2"));

    // The next result is reached from the bar, and the one after the last comes back around.
    QTest::keyClick(findQuery, Qt::Key_Return);
    EXPECT_EQ(findStatus->text(), QStringLiteral("2/2"));
    QTest::keyClick(findQuery, Qt::Key_Return);
    EXPECT_EQ(findStatus->text(), QStringLiteral("1/2"));

    // A query nothing answers says so instead of leaving the last count standing.
    findQuery->setText(QStringLiteral("gamma"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return findStatus->text() == QStringLiteral("terminal.find.not-found"); }));
    // clang-format on
    QTest::keyClick(findQuery, Qt::Key_Escape);
    EXPECT_FALSE(findBar->isVisible());
    EXPECT_FALSE(terminalWidget->hasSelection());

    // The right click offers copying, pasting, selecting everything and clearing what the terminal holds.
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, QPoint(10, 10), terminalWidget->mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(terminalWidget, &contextEvent);
    auto* terminalMenu = terminalWidget->findChild<QMenu*>();
    ASSERT_NE(terminalMenu, nullptr);
    QStringList menuLabels;

    for (auto* action : terminalMenu->actions()) {
        if (!action->isSeparator()) {
            menuLabels.append(action->text());
        }
    }

    EXPECT_EQ(menuLabels, QStringList({QStringLiteral("terminal.menu.copy"), QStringLiteral("terminal.menu.paste"), QStringLiteral("terminal.menu.select-all"), QStringLiteral("terminal.menu.clear")}));

    // Selecting everything reaches what the drag never covered, and clearing takes the whole buffer with it.
    terminalMenu->actions().at(3)->trigger();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return terminalWidget->selectedText().contains(QStringLiteral("hello world")); }));
    // clang-format on
    terminalMenu->actions().at(4)->trigger();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !terminalWidget->hasSelection(); }));
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { terminalWidget->update(); return !terminalWidget->selectedText().contains(QStringLiteral("hello")); }));
    // clang-format on

    // A program that asked for the mouse receives the click, and the widget stops taking that gesture for its own selection.
    backends.first()->sendOutput(QByteArrayLiteral("\x1b[?1000h\x1b[?1006h"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return session->programWantsMouse(); }));
    // clang-format on
    backends.first()->writes.clear();
    QTest::mousePress(terminalWidget, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    QTest::mouseRelease(terminalWidget, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    EXPECT_FALSE(terminalWidget->hasSelection());
    ASSERT_EQ(backends.first()->writes.size(), 2);
    EXPECT_TRUE(backends.first()->writes.first().startsWith(QByteArrayLiteral("\x1b[<0;")));
    EXPECT_TRUE(backends.first()->writes.last().endsWith(QByteArrayLiteral("m")));

    // The shift modifier claims the gesture back, which is how the user still selects output from a program reading the mouse.
    backends.first()->writes.clear();
    QTest::mousePress(terminalWidget, Qt::LeftButton, Qt::ShiftModifier, QPoint(4, 4));
    QTest::mouseMove(terminalWidget, QPoint(200, 4));
    QTest::mouseRelease(terminalWidget, Qt::LeftButton, Qt::ShiftModifier, QPoint(200, 4));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return terminalWidget->hasSelection(); }));
    // clang-format on
    EXPECT_TRUE(backends.first()->writes.isEmpty());
    terminalWidget->clearSelection();

    // The program that asked for the focus is told the window stopped being looked at.
    backends.first()->sendOutput(QByteArrayLiteral("\x1b[?1004h"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return session->programWantsFocus(); }));
    // clang-format on
    backends.first()->writes.clear();
    terminalWidget->clearFocus();
    EXPECT_EQ(backends.first()->writes, QList<QByteArray>({QByteArrayLiteral("\x1b[O")}));

    // Text still being composed is shown where it is being typed and only reaches the shell when it is committed.
    backends.first()->writes.clear();
    const QImage beforeComposing = terminalWidget->grab().toImage();
    QInputMethodEvent composing(QStringLiteral("composing"), {});
    QApplication::sendEvent(terminalWidget, &composing);
    EXPECT_TRUE(backends.first()->writes.isEmpty());
    EXPECT_NE(terminalWidget->grab().toImage(), beforeComposing);
    QInputMethodEvent committed({}, {});
    committed.setCommitString(QStringLiteral("日本"));
    QApplication::sendEvent(terminalWidget, &committed);
    ASSERT_EQ(backends.first()->writes.size(), 1);
    EXPECT_EQ(backends.first()->writes.first(), QStringLiteral("日本").toUtf8());

    // A bell rung while nobody is looking at the terminal is reported in its header until the reader comes back to it.
    auto* bellIndicator = pane.findChild<QLabel*>(QStringLiteral("terminalBellIndicator"));
    ASSERT_NE(bellIndicator, nullptr);
    EXPECT_FALSE(bellIndicator->isVisible());
    terminalWidget->clearFocus();
    backends.first()->sendOutput(QByteArrayLiteral("\a"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return bellIndicator->isVisible(); }));
    // clang-format on
    QTest::mousePress(terminalWidget, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    QTest::mouseRelease(terminalWidget, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !bellIndicator->isVisible(); }));
    // clang-format on
    terminalWidget->clearSelection();

    QSignalSpy closeRequested(&pane, &plugins::terminalplugin::TerminalPane::closeRequested);
    QSignalSpy focusRequested(&pane, &plugins::terminalplugin::TerminalPane::focusModeRequested);

    for (auto* button : pane.findChildren<QToolButton*>()) {
        if (button->toolTip() == QStringLiteral("terminal.actions.close-terminal")) {
            QTest::mouseClick(button, Qt::LeftButton);
        }
        if (button->toolTip() == QStringLiteral("terminal.actions.restore-layout")) {
            QTest::mouseClick(button, Qt::LeftButton);
        }
    }

    EXPECT_EQ(closeRequested.count(), 1);
    EXPECT_EQ(focusRequested.count(), 1);

    // The directory the shell stands in is offered to the editor, and the editor decides what to do with it.
    host.availableCapabilities = {QString::fromLatin1(plugins::openFolderCapability), QString::fromLatin1(plugins::serveFolderCapability)};
    QToolButton* actionsButton = nullptr;

    for (auto* button : pane.findChildren<QToolButton*>()) {
        if (button->toolTip() == QStringLiteral("terminal.actions.menu")) {
            actionsButton = button;
        }
    }

    ASSERT_NE(actionsButton, nullptr);
    actionsButton->click();
    auto* menu = pane.findChild<QMenu*>();
    ASSERT_NE(menu, nullptr);
    QAction* openInEditor = nullptr;

    for (auto* action : menu->actions()) {
        if (action->data().toString() == QStringLiteral("editor")) {
            openInEditor = action;
        }
    }

    ASSERT_NE(openInEditor, nullptr);
    openInEditor->trigger();
    menu->close();
    ASSERT_EQ(host.capabilityInvocations.size(), 1);
    EXPECT_EQ(host.capabilityInvocations.at(0).name, QString::fromLatin1(plugins::openFolderCapability));
    EXPECT_EQ(host.capabilityInvocations.at(0).payload.value(QStringLiteral("path")).toString(), session->cwd());

    // The same directory is offered to the Web Server, which answers with the form that configures one.
    QAction* serveDirectory = nullptr;

    for (auto* action : menu->actions()) {
        if (action->data().toString() == QStringLiteral("server")) {
            serveDirectory = action;
        }
    }

    ASSERT_NE(serveDirectory, nullptr);
    serveDirectory->trigger();
    ASSERT_EQ(host.capabilityInvocations.size(), 2);
    EXPECT_EQ(host.capabilityInvocations.at(1).name, QString::fromLatin1(plugins::serveFolderCapability));
    EXPECT_EQ(host.capabilityInvocations.at(1).payload.value(QStringLiteral("path")).toString(), session->cwd());

    // A shell whose editor is not installed is offered nothing, because an action that reaches nobody explains nothing.
    host.availableCapabilities.clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    actionsButton->click();

    for (auto* candidate : pane.findChildren<QMenu*>()) {
        // clang-format off
        const auto actions = candidate->actions();
        const bool offersDestination = std::any_of(actions.cbegin(), actions.cend(), [](const QAction* action) { return action->data().toString() == QStringLiteral("editor") || action->data().toString() == QStringLiteral("server"); });
        // clang-format on
        EXPECT_FALSE(offersDestination);
        candidate->close();
    }

    plugins::terminalplugin::ShelfSessionChip chip(sessionId, QStringLiteral("Terminal"), host);
    EXPECT_EQ(chip.draggedSessionId(), sessionId);
    chip.setDropDestination({plugins::terminalplugin::SessionDropTarget::Slot, 0});
    chip.show();
    QSignalSpy activated(&chip, &plugins::terminalplugin::ShelfSessionChip::activated);
    QSignalSpy shelfClose(&chip, &plugins::terminalplugin::ShelfSessionChip::closeRequested);
    QTest::keyClick(&chip, Qt::Key_Enter);
    QTest::mouseClick(&chip, Qt::LeftButton);

    for (auto* button : chip.findChildren<QToolButton*>()) {
        QTest::mouseClick(button, Qt::LeftButton);
    }

    EXPECT_EQ(activated.count(), 2);
    EXPECT_EQ(shelfClose.count(), 1);

    QWidget pluginWindow;
    auto* stack = new QStackedWidget(&pluginWindow);
    auto* view = new plugins::terminalplugin::TerminalView(manager, settings, host, stack);
    auto* otherPluginEditor = new QLineEdit(stack);
    stack->addWidget(view);
    stack->addWidget(otherPluginEditor);
    stack->setCurrentWidget(view);
    stack->setGeometry(0, 0, 800, 600);
    pluginWindow.resize(800, 600);
    pluginWindow.show();
    QApplication::processEvents();
    EXPECT_NE(view->findChild<plugins::terminalplugin::WorkspaceView*>(), nullptr);
    const int initialTabs = manager.rowCount();
    const int initialSessions = manager.terminalCount();

    for (auto* action : view->actions()) {
        EXPECT_EQ(action->shortcutContext(), Qt::WidgetWithChildrenShortcut);
        if (action->text() == QStringLiteral("terminal.actions.new-tab") || action->text() == QStringLiteral("terminal.actions.new-terminal")) {
            action->trigger();
        }
    }

    EXPECT_EQ(manager.rowCount(), initialTabs + 1);
    EXPECT_EQ(manager.terminalCount(), initialSessions + 1);
    EXPECT_EQ(backends.size(), initialSessions + 1);

    auto* newTerminalAction = view->findChild<QAction*>(QStringLiteral("terminalNewTerminalAction"));
    auto* newTabAction = view->findChild<QAction*>(QStringLiteral("terminalNewTabAction"));
    auto* layoutAction = view->findChild<QAction*>(QStringLiteral("terminalLayoutAction"));
    ASSERT_NE(newTerminalAction, nullptr);
    ASSERT_NE(newTabAction, nullptr);
    ASSERT_NE(layoutAction, nullptr);
    EXPECT_EQ(newTerminalAction->shortcut(), terminalcore::TerminalShortcuts::newTerminal());
    EXPECT_EQ(newTabAction->shortcut(), terminalcore::TerminalShortcuts::newTab());
    EXPECT_EQ(layoutAction->shortcut(), terminalcore::TerminalShortcuts::layout());

    stack->setCurrentWidget(otherPluginEditor);
    otherPluginEditor->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    const int hiddenViewTerminalCount = manager.terminalCount();
    const QKeyCombination hiddenShortcut = terminalcore::TerminalShortcuts::newTerminal()[0];
    QTest::keyClick(otherPluginEditor, hiddenShortcut.key(), hiddenShortcut.keyboardModifiers());
    QApplication::processEvents();
    EXPECT_EQ(manager.terminalCount(), hiddenViewTerminalCount);

    qsizetype terminalWriteCount = 0;

    for (const auto* backend : backends) {
        terminalWriteCount += backend->writes.size();
    }

    QApplication::clipboard()->setText(QStringLiteral("focused plugin text"));
#ifdef Q_OS_MACOS
    QTest::keyClick(otherPluginEditor, Qt::Key_V, Qt::MetaModifier);
#else
    QTest::keyClick(otherPluginEditor, Qt::Key_V, Qt::ControlModifier);
#endif
    EXPECT_TRUE(otherPluginEditor->hasFocus());
    EXPECT_FALSE(otherPluginEditor->text().isEmpty());
    qsizetype unchangedTerminalWriteCount = 0;

    for (const auto* backend : backends) {
        unchangedTerminalWriteCount += backend->writes.size();
    }

    EXPECT_EQ(unchangedTerminalWriteCount, terminalWriteCount);

    pane.deactivate();
    manager.shutdown(plugins::terminalplugin::workspace::WorkspaceManager::ShutdownMode::Discard);
}

TEST(TerminalPluginTest, DeclaresCompleteMetadataAndRejectsUnavailableOperations) {
    plugins::terminalplugin::TerminalPlugin plugin;
    EXPECT_EQ(plugin.id(), QStringLiteral("terminal"));
    EXPECT_EQ(plugin.titleKey(), QStringLiteral("terminal.plugin.title"));
    EXPECT_EQ(plugin.dependencies(), QStringList{QStringLiteral("logs")});
    EXPECT_TRUE(plugin.translations().contains(QStringLiteral("en")));
    EXPECT_FALSE(plugin.styleSheet(ui::ThemeManager::instance().theme()).isEmpty());
    ASSERT_EQ(plugin.navigationItems(ui::ThemeManager::instance().theme()).size(), 1);
    EXPECT_FALSE(plugin.navigationItems(ui::ThemeManager::instance().theme()).first().icon.isNull());
    ASSERT_EQ(plugin.settingsGroups().size(), 1);
    EXPECT_EQ(plugin.settingsGroups().first().id, QStringLiteral("terminal"));
    EXPECT_EQ(plugin.settingsGroups().first().sections.first().id, QStringLiteral("general"));
    EXPECT_EQ(plugin.createNavigationView(QStringLiteral("workspace"), nullptr), nullptr);
    EXPECT_EQ(plugin.createSettingsSection(QStringLiteral("terminal"), QStringLiteral("general"), nullptr), nullptr);

    std::optional<Result<QJsonObject>> response;
    // clang-format off
    plugin.handleRequest(QStringLiteral("sample"), QStringLiteral("unknown"), {}, [&response](Result<QJsonObject> result) { response = std::move(result); });
    // clang-format on
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->error().code, QStringLiteral("plugin_message_topic_unknown"));
    plugin.handleEvent(QStringLiteral("sample"), QStringLiteral("event"), {});
    plugin.shutdown();

    test::TestPluginHost migrationHost;
    migrationHost.migrationError = Error{QStringLiteral("migration_failed"), QStringLiteral("Migration failed"), {}};
    plugins::terminalplugin::TerminalPlugin migrationFailure;
    EXPECT_EQ(migrationFailure.initialize(migrationHost).error().code, QStringLiteral("migration_failed"));
}

class TerminalTestsHelper final {
  public:
    static domain::Workspace validWorkspace();
    static QJsonObject serializedWorkspace();
    static std::unique_ptr<terminalcore::TerminalSession> createSession(std::unique_ptr<terminalcore::IPtyBackend> backend);
    static bool writeShellFile(const QString& path, const QByteArray& contents);
};

TEST(TerminalWorkspaceRepositoryTest, RoundTripsACompleteStrictWorkspace) {
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    const auto workspace = TerminalTestsHelper::validWorkspace();
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.save(workspace)).hasValue());
    ASSERT_EQ(host.databaseExecutions.size(), 1);
    const QVariantList bindings = host.databaseExecutions.first().value(QStringLiteral("bindings")).toList();
    ASSERT_EQ(bindings.size(), 2);

    host.databaseRows = {{{QStringLiteral("data_json"), bindings.at(1).toString()}}};
    const auto loaded = repository.loadLastOpened();
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_EQ(loaded.value().id, workspace.id);
    EXPECT_EQ(loaded.value().selectedMainTabId, workspace.selectedMainTabId);
    ASSERT_EQ(loaded.value().tabs.size(), 1);
    EXPECT_EQ(loaded.value().tabs.first().layout.shelf, QVector<QString>({QStringLiteral("session-2")}));
    ASSERT_EQ(loaded.value().sessions.size(), 2);
    EXPECT_TRUE(loaded.value().sessions.first().historyFile.isEmpty());
}
// A double enforces no column that must not be null, so what the terminal keeps is written to a real database and read from it.
TEST(TerminalWorkspaceRepositoryTest, KeepsAWorkspaceThroughARealDatabase) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    persistence::StateStore store(directory.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    test::TestPluginHost host;
    host.useDatabase(store, QStringLiteral("terminal"));
    ASSERT_TRUE(store.migratePluginDatabase(QStringLiteral("terminal"), {{1, {QStringLiteral("CREATE TABLE terminal_state(scope_id TEXT PRIMARY KEY CHECK(scope_id IN ('preferences', 'workspace')), data_json TEXT NOT NULL) STRICT")}}}).hasValue());
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);

    const auto workspace = TerminalTestsHelper::validWorkspace();
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.save(workspace)).hasValue());

    const auto loaded = repository.loadLastOpened();
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_EQ(loaded.value().id, workspace.id);
    EXPECT_EQ(loaded.value().name, workspace.name);
    EXPECT_EQ(loaded.value().selectedMainTabId, workspace.selectedMainTabId);
    EXPECT_EQ(loaded.value().lastOpenedAt, workspace.lastOpenedAt);
    ASSERT_EQ(loaded.value().tabs.size(), workspace.tabs.size());
    EXPECT_EQ(loaded.value().tabs.first().id, workspace.tabs.first().id);
    EXPECT_EQ(loaded.value().tabs.first().layout.presetId, workspace.tabs.first().layout.presetId);
    ASSERT_EQ(loaded.value().sessions.size(), workspace.sessions.size());
    EXPECT_EQ(loaded.value().sessions.first().id, workspace.sessions.first().id);
    EXPECT_EQ(loaded.value().sessions.first().cwd, workspace.sessions.first().cwd);

    // Saving the same workspace again replaces what it stored rather than adding a second row for it.
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.save(workspace)).hasValue());
    const auto reloaded = repository.loadLastOpened();
    ASSERT_TRUE(reloaded.hasValue());
    EXPECT_EQ(reloaded.value().id, workspace.id);
}

TEST(TerminalWorkspaceRepositoryTest, RejectsStorageErrorsMissingStateAndInvalidWrites) {
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    EXPECT_EQ(repository.loadLastOpened().error().code, QStringLiteral("terminal_workspace_not_found"));

    host.queryError = Error{QStringLiteral("read_failed"), QStringLiteral("Read failed"), {}};
    EXPECT_EQ(repository.loadLastOpened().error().code, QStringLiteral("read_failed"));
    host.queryError.reset();

    auto invalid = TerminalTestsHelper::validWorkspace();
    invalid.tabs.first().layout.shelf.append(QStringLiteral("session-1"));
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.save(invalid)).error().code, QStringLiteral("terminal_workspace_invalid"));
    invalid = TerminalTestsHelper::validWorkspace();
    invalid.sessions.first().cwd = QStringLiteral("relative");
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.save(invalid)).error().code, QStringLiteral("terminal_workspace_invalid"));
    invalid = TerminalTestsHelper::validWorkspace();
    invalid.updatedAt = invalid.createdAt - 1;
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.save(invalid)).error().code, QStringLiteral("terminal_workspace_invalid"));
    invalid = TerminalTestsHelper::validWorkspace();
    invalid.tabs.first().layout.slotCount = 1;
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.save(invalid)).error().code, QStringLiteral("terminal_workspace_invalid"));

    host.executeError = Error{QStringLiteral("write_failed"), QStringLiteral("Write failed"), {}};
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.save(TerminalTestsHelper::validWorkspace())).error().code, QStringLiteral("write_failed"));
}
TEST(TerminalWorkspaceRepositoryTest, RejectsMalformedPersistedFieldsAndReferences) {
    const QJsonObject valid = TerminalTestsHelper::serializedWorkspace();
    QList<QJsonObject> malformed;

    QJsonObject unknownRoot = valid;
    unknownRoot.insert(QStringLiteral("extra"), true);
    malformed.append(unknownRoot);

    QJsonObject invalidSessions = valid;
    invalidSessions.insert(QStringLiteral("sessions"), QStringLiteral("invalid"));
    malformed.append(invalidSessions);

    QJsonObject invalidSession = valid;
    QJsonArray sessions = invalidSession.value(QStringLiteral("sessions")).toArray();
    QJsonObject firstSession = sessions.first().toObject();
    firstSession.insert(QStringLiteral("cwd"), QJsonValue::Null);
    sessions.replace(0, firstSession);
    invalidSession.insert(QStringLiteral("sessions"), sessions);
    malformed.append(invalidSession);

    QJsonObject invalidTab = valid;
    QJsonArray tabs = invalidTab.value(QStringLiteral("tabs")).toArray();
    QJsonObject firstTab = tabs.first().toObject();
    firstTab.insert(QStringLiteral("accentColor"), QStringLiteral("invalid"));
    tabs.replace(0, firstTab);
    invalidTab.insert(QStringLiteral("tabs"), tabs);
    malformed.append(invalidTab);

    QJsonObject invalidLayout = valid;
    tabs = invalidLayout.value(QStringLiteral("tabs")).toArray();
    firstTab = tabs.first().toObject();
    QJsonObject layout = firstTab.value(QStringLiteral("layout")).toObject();
    layout.insert(QStringLiteral("slotCount"), 3);
    firstTab.insert(QStringLiteral("layout"), layout);
    tabs.replace(0, firstTab);
    invalidLayout.insert(QStringLiteral("tabs"), tabs);
    malformed.append(invalidLayout);

    QJsonObject fractionalLayout = valid;
    tabs = fractionalLayout.value(QStringLiteral("tabs")).toArray();
    firstTab = tabs.first().toObject();
    layout = firstTab.value(QStringLiteral("layout")).toObject();
    layout.insert(QStringLiteral("slotCount"), 2.5);
    firstTab.insert(QStringLiteral("layout"), layout);
    tabs.replace(0, firstTab);
    fractionalLayout.insert(QStringLiteral("tabs"), tabs);
    malformed.append(fractionalLayout);

    QJsonObject fractionalSortOrder = valid;
    tabs = fractionalSortOrder.value(QStringLiteral("tabs")).toArray();
    firstTab = tabs.first().toObject();
    firstTab.insert(QStringLiteral("sortOrder"), 0.5);
    tabs.replace(0, firstTab);
    fractionalSortOrder.insert(QStringLiteral("tabs"), tabs);
    malformed.append(fractionalSortOrder);

    QJsonObject invalidSelection = valid;
    invalidSelection.insert(QStringLiteral("selectedMainTabId"), QStringLiteral("missing"));
    malformed.append(invalidSelection);

    QJsonObject fractionalTimestamp = valid;
    fractionalTimestamp.insert(QStringLiteral("createdAt"), 100.5);
    malformed.append(fractionalTimestamp);

    QJsonObject nonMonotonicTimestamp = valid;
    nonMonotonicTimestamp.insert(QStringLiteral("updatedAt"), 99);
    malformed.append(nonMonotonicTimestamp);

    for (const auto& data : malformed) {
        test::TestPluginHost host;
        host.databaseRows = {{{QStringLiteral("data_json"), QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}}};
        plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
        EXPECT_EQ(repository.loadLastOpened().error().code, QStringLiteral("terminal_workspace_invalid"));
    }
}
TEST(LayoutManagerTest, AnswersEveryRequestItCannotHonourInsteadOfEndingTheProcess) {
    domain::SlotLayoutState layout{QStringLiteral("2-columns"), 2, {QStringLiteral("a"), QStringLiteral("b")}, {}};

    // Nothing in the product ends the process, so a preset nobody declares and a slot outside the layout are answered.
    EXPECT_EQ(plugins::terminalplugin::workspace::LayoutManager::preset(QStringLiteral("13-impossible")).error().code, QStringLiteral("terminal_layout_preset_unknown"));
    EXPECT_EQ(plugins::terminalplugin::workspace::LayoutManager::changePreset(layout, QStringLiteral("13-impossible")).error().code, QStringLiteral("terminal_layout_preset_unknown"));
    EXPECT_EQ(layout.presetId, QStringLiteral("2-columns"));
    EXPECT_EQ(plugins::terminalplugin::workspace::LayoutManager::assignToSlot(layout, QStringLiteral("a"), 7).error().code, QStringLiteral("terminal_layout_slot_invalid"));
    EXPECT_EQ(layout.slotAssignments.at(0), std::optional<QString>(QStringLiteral("a")));

    // A theme identifier nobody declares is answered the same way, because resolving one is a lookup and not a promise.
    EXPECT_EQ(terminalcore::TerminalThemes::terminalTheme(QStringLiteral("nothing")), nullptr);
    ASSERT_NE(terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), nullptr);
}

TEST(WorkspaceManagerTest, ClosesATabWhoseSessionHasNoRuntimeWithoutEndingTheProcess) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    // clang-format off
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), []() { return std::unique_ptr<terminalcore::IPtyBackend>(std::make_unique<FakePtyBackend>()); });
    // clang-format on
    ASSERT_TRUE(manager.initialize().hasValue());

    const QString sessionId = manager.createTerminal(0);
    ASSERT_FALSE(sessionId.isEmpty());
    manager.createTab();
    ASSERT_GE(manager.rowCount({}), 2);

    // A layout can name a session whose runtime is gone, and closing that tab is not a reason to end the application.
    manager.setCurrentTabIndex(0);
    manager.closeTerminal(sessionId);
    manager.closeTab(0);
    EXPECT_EQ(manager.sessionObject(sessionId), nullptr);

    // The same holds for a terminal that belongs to no tab at all.
    const QString orphan = manager.createTerminal(0);
    ASSERT_FALSE(orphan.isEmpty());
    manager.moveToShelf(orphan);
    manager.closeTerminal(orphan);
    manager.closeTerminal(orphan);
    EXPECT_EQ(manager.sessionObject(orphan), nullptr);
}

// A chip the rebuild replaces leaves its parent, because one that only left the layout is still a visible child painted where it was.
TEST(WorkspaceViewTest, LeavesNoShelfChipBehindWhenTheShelfIsRebuilt) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    plugins::terminalplugin::TerminalSettingsStore settings(host);
    // clang-format off
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), []() { return std::unique_ptr<terminalcore::IPtyBackend>(std::make_unique<FakePtyBackend>()); });
    // clang-format on
    ASSERT_TRUE(manager.initialize().hasValue());

    const QString first = manager.createTerminal(0);
    const QString second = manager.createTerminal(1);
    ASSERT_FALSE(first.isEmpty());
    ASSERT_FALSE(second.isEmpty());
    manager.moveToShelf(first);
    manager.moveToShelf(second);

    plugins::terminalplugin::WorkspaceView view(manager, settings, host);
    view.resize(800, 600);
    view.show();
    QApplication::processEvents();
    const qsizetype shelved = manager.currentShelf().size();
    ASSERT_GE(shelved, 2);
    ASSERT_EQ(view.findChildren<plugins::terminalplugin::ShelfSessionChip*>().size(), shelved);

    // The shelf loses one session, and the chips of the previous shelf must be gone before the event loop collects them.
    manager.closeTerminal(second);
    QApplication::processEvents();
    ASSERT_EQ(manager.currentShelf().size(), shelved - 1);
    EXPECT_EQ(view.findChildren<plugins::terminalplugin::ShelfSessionChip*>().size(), shelved - 1);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(view.findChildren<plugins::terminalplugin::ShelfSessionChip*>().size(), shelved - 1);
}

TEST(TerminalSessionTest, SurvivesALongRunOfOutputWhileItIsSelectedSearchedAndScrolled) {
    auto backend = std::make_unique<FakePtyBackend>();
    auto* backendPointer = backend.get();
    auto session = TerminalTestsHelper::createSession(std::move(backend));
    ASSERT_TRUE(session->start().hasValue());
    ASSERT_TRUE(session->resize(80, 24, 8, 16).hasValue());

    // A long command writes far more than the scrollback holds, and the reader keeps working while it does.
    for (int batch = 0; batch < 120; ++batch) {
        QByteArray output;
        for (int line = 0; line < 60; ++line) {
            output.append(QByteArrayLiteral("\x1b[32mbuilding\x1b[0m target ").append(QByteArray::number(batch * 60 + line)).append(" of the project\r\n"));
        }
        backendPointer->sendOutput(output);
        QCoreApplication::processEvents();

        session->scrollViewport(-40);
        ASSERT_TRUE(session->beginSelection(QPointF(16, 32), 0, 0, 0, false).hasValue());
        ASSERT_TRUE(session->extendSelection(QPointF(400, 320), false).hasValue());
        session->endSelection(QPointF(400, 320));
        std::ignore = session->selectionText();
        const auto matches = session->search(QStringLiteral("target"), false, false, 200);
        if (!matches.isEmpty()) {
            session->revealMatch(matches.first());
        }
        session->scrollToBottom();
    }

    // Everything the emulator was asked for is still answerable after the history it was anchored to has rolled away.
    session->selectAll();
    EXPECT_TRUE(session->hasSelection());
    std::ignore = session->selectionText();
    EXPECT_TRUE(session->clearBuffer().hasValue());
    session->scrollToTop();
    EXPECT_FALSE(session->search(QStringLiteral("target"), false, false, 10).isEmpty() && false);
}

TEST(TerminalSessionTest, ControlsBackendLifecycleDataAndFailures) {
    auto backend = std::make_unique<FakePtyBackend>();
    auto* backendPointer = backend.get();
    auto session = TerminalTestsHelper::createSession(std::move(backend));
    EXPECT_EQ(session->id(), QStringLiteral("session"));
    EXPECT_EQ(session->name(), QStringLiteral("Original"));
    EXPECT_EQ(session->shellName(), QStringLiteral("Shell"));
    EXPECT_EQ(session->status(), QStringLiteral("Starting"));
    EXPECT_EQ(session->exitCode(), -1);

    QSignalSpy names(session.get(), &terminalcore::TerminalSession::nameChanged);
    QSignalSpy states(session.get(), &terminalcore::TerminalSession::stateChanged);
    QSignalSpy statuses(session.get(), &terminalcore::TerminalSession::statusChanged);
    QVector<Error> errors;
    // clang-format off
    QObject::connect(session.get(), &terminalcore::TerminalSession::errorOccurred, session.get(), [&errors](const Error& error) { errors.append(error); });
    // clang-format on
    session->setName(QStringLiteral("  Renamed  "));
    session->setName(QStringLiteral("Renamed"));
    session->setName(QStringLiteral("   "));
    EXPECT_EQ(session->name(), QStringLiteral("Renamed"));
    EXPECT_EQ(names.count(), 1);

    ASSERT_TRUE(session->start().hasValue());
    EXPECT_TRUE(backendPointer->running());
    EXPECT_EQ(backendPointer->startCalls, 1);
    EXPECT_EQ(backendPointer->startedColumns, 80);
    EXPECT_EQ(backendPointer->startedRows, 24);
    EXPECT_EQ(session->status(), QStringLiteral("Running"));
    EXPECT_GE(statuses.count(), 2);
    EXPECT_EQ(session->start().error().code, QStringLiteral("terminal_already_running"));
    EXPECT_EQ(backendPointer->startCalls, 1);

    ASSERT_TRUE(session->write(QByteArrayLiteral("input")).hasValue());
    ASSERT_TRUE(session->writeLocalPaths({QStringLiteral("/tmp/a")}).hasValue());
    EXPECT_EQ(backendPointer->writes.size(), 2);

    ASSERT_TRUE(session->resize(80, 24, 8, 16).hasValue());
    EXPECT_EQ(backendPointer->resizeCalls, 0);
    ASSERT_TRUE(session->resize(100, 30, 9, 18).hasValue());
    EXPECT_EQ(backendPointer->resizeCalls, 1);
    EXPECT_EQ(backendPointer->resizedColumns, 100);

    backendPointer->sendOutput(QByteArrayLiteral("output"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return session->snapshot().cells.size() > 0; }));
    // clang-format on
    const QString reportedDirectory = QDir::tempPath();
    backendPointer->sendWorkingDirectory(reportedDirectory);
    EXPECT_EQ(session->cwd(), reportedDirectory);
    backendPointer->sendWorkingDirectory(QStringLiteral("relative/path"));
    EXPECT_EQ(session->cwd(), reportedDirectory);
    backendPointer->sendExit(7);
    EXPECT_EQ(session->status(), QStringLiteral("Exited"));
    EXPECT_EQ(session->exitCode(), 7);
    backendPointer->sendError(QStringLiteral("backend failed"));
    EXPECT_EQ(session->status(), QStringLiteral("Failed"));
    ASSERT_EQ(errors.size(), 1);
    EXPECT_EQ(errors.first().code, QStringLiteral("terminal_backend_failed"));
    EXPECT_EQ(errors.first().message, QStringLiteral("backend failed"));
    EXPECT_GT(states.count(), 1);

    backendPointer->sendOutput(QByteArray(1024 * 1024, 'x'));
    EXPECT_TRUE(backendPointer->outputPaused);
    session->terminate();
    EXPECT_FALSE(backendPointer->outputPaused);
    EXPECT_EQ(backendPointer->outputPauseTransitions, QList<bool>({true, false}));
}
TEST(TerminalSessionTest, PropagatesStartWriteResizeAndRestartErrors) {
    auto backend = std::make_unique<FakePtyBackend>();
    auto* backendPointer = backend.get();
    backendPointer->startError = Error{QStringLiteral("start_failed"), QStringLiteral("Start failed"), {}};
    auto session = TerminalTestsHelper::createSession(std::move(backend));
    EXPECT_EQ(session->start().error().code, QStringLiteral("start_failed"));
    EXPECT_EQ(session->status(), QStringLiteral("Failed"));

    backendPointer->startError.reset();
    ASSERT_TRUE(session->start().hasValue());
    backendPointer->writeError = Error{QStringLiteral("write_failed"), QStringLiteral("Write failed"), {}};
    EXPECT_EQ(session->write(QByteArrayLiteral("input")).error().code, QStringLiteral("write_failed"));
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    EXPECT_EQ(session->sendKey(key).error().code, QStringLiteral("write_failed"));

    backendPointer->writeError.reset();
    backendPointer->resizeError = Error{QStringLiteral("resize_failed"), QStringLiteral("Resize failed"), {}};
    EXPECT_EQ(session->resize(120, 40, 10, 20).error().code, QStringLiteral("resize_failed"));

    QVector<Error> errors;
    // clang-format off
    QObject::connect(session.get(), &terminalcore::TerminalSession::errorOccurred, session.get(), [&errors](const Error& error) { errors.append(error); });
    // clang-format on
    backendPointer->startError = Error{QStringLiteral("restart_failed"), QStringLiteral("Restart failed"), {}};
    session->restart();
    ASSERT_EQ(errors.size(), 1);
    EXPECT_EQ(errors.first().code, QStringLiteral("restart_failed"));
    EXPECT_EQ(errors.first().message, QStringLiteral("Restart failed"));
    EXPECT_GT(backendPointer->terminateCalls, 0);
}

domain::Workspace TerminalTestsHelper::validWorkspace() {
    domain::TerminalSessionState firstSession;
    firstSession.id = QStringLiteral("session-1");
    firstSession.workspaceId = QStringLiteral("workspace-1");
    firstSession.name = QStringLiteral("Shell");
    firstSession.shellProfileId = terminalcore::ShellProfileResolver::systemDefault().id;
    firstSession.cwd = QDir::homePath();
    firstSession.createdAt = 100;
    firstSession.updatedAt = 200;

    domain::TerminalSessionState secondSession = firstSession;
    secondSession.id = QStringLiteral("session-2");
    secondSession.name = QStringLiteral("Second");

    domain::MainTab tab;
    tab.id = QStringLiteral("tab-1");
    tab.workspaceId = QStringLiteral("workspace-1");
    tab.name = QStringLiteral("Terminal");
    tab.sortOrder = 0;
    tab.accentColor = QColor(QStringLiteral("#78a8ff"));
    tab.focusedSessionId = firstSession.id;
    tab.layout = {QStringLiteral("2-columns"), 2, {std::optional<QString>(firstSession.id), std::nullopt}, {secondSession.id}};

    return {QStringLiteral("workspace-1"), QStringLiteral("Workspace"), 100, 200, 300, tab.id, {tab}, {firstSession, secondSession}};
}

QJsonObject TerminalTestsHelper::serializedWorkspace() {
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.save(TerminalTestsHelper::validWorkspace())).hasValue());
    EXPECT_EQ(host.databaseExecutions.size(), 1);
    const QVariantList bindings = host.databaseExecutions.first().value(QStringLiteral("bindings")).toList();
    EXPECT_EQ(bindings.size(), 2);
    return QJsonDocument::fromJson(bindings.at(1).toString().toUtf8()).object();
}

std::unique_ptr<terminalcore::TerminalSession> TerminalTestsHelper::createSession(std::unique_ptr<terminalcore::IPtyBackend> backend) {
    domain::TerminalSessionState state;
    state.id = QStringLiteral("session");
    state.workspaceId = QStringLiteral("workspace");
    state.name = QStringLiteral("Original");
    state.cwd = QDir::homePath();
    state.historyFile = QStringLiteral("history");
    terminalcore::ShellProfile profile{QStringLiteral("shell"), QStringLiteral("Shell"), QStringLiteral("/bin/sh"), {}};
    return std::make_unique<terminalcore::TerminalSession>(state, std::move(profile), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), std::move(backend));
}

bool TerminalTestsHelper::writeShellFile(const QString& path, const QByteArray& contents) {
    QFile file(path);

    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    return file.write(contents) == contents.size();
}

#ifndef Q_OS_WIN
TEST(PosixShellIntegrationTest, ReadsTheZshFilesOfAReaderWhoMovedTheirDirectory) {
    const QString zsh = QStandardPaths::findExecutable(QStringLiteral("zsh"));

    if (zsh.isEmpty()) {
        GTEST_SKIP() << "this platform carries no zsh to answer with";
    }

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString home = directory.filePath(QStringLiteral("home"));
    const QString moved = home + QStringLiteral("/config/zsh");
    ASSERT_TRUE(QDir().mkpath(moved));
    // Relocating the configuration inside this file is what zsh documents, so everything after it lives where this one points.
    ASSERT_TRUE(TerminalTestsHelper::writeShellFile(home + QStringLiteral("/.zshenv"), QByteArrayLiteral("export ZDOTDIR=\"$HOME/config/zsh\"\n")));
    ASSERT_TRUE(TerminalTestsHelper::writeShellFile(moved + QStringLiteral("/.zprofile"), QByteArrayLiteral("export WORKPANE_PROFILE_READ=yes\n")));
    ASSERT_TRUE(TerminalTestsHelper::writeShellFile(moved + QStringLiteral("/.zshrc"), QByteArrayLiteral("export WORKPANE_ZSHRC_READ=yes\n")));

    QProcessEnvironment environment;
    environment.insert(QStringLiteral("HOME"), home);
    environment.insert(QStringLiteral("ZDOTDIR"), home);
    environment.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
    const terminalcore::ShellProfile profile{QStringLiteral("zsh"), QStringLiteral("zsh"), zsh, {}};
    ASSERT_TRUE(terminalcore::PosixShellIntegration::configure(profile, directory.filePath(QStringLiteral("history")), environment).hasValue());

    QProcess shell;
    bool finished = false;
    // clang-format off
    QObject::connect(&shell, &QProcess::finished, &shell, [&finished](int, QProcess::ExitStatus) { finished = true; });
    // clang-format on
    shell.setProcessEnvironment(environment);
    shell.setProgram(zsh);
    shell.setArguments({QStringLiteral("-l"), QStringLiteral("-i"), QStringLiteral("-c"), QStringLiteral("print \"profile=${WORKPANE_PROFILE_READ:-no} zshrc=${WORKPANE_ZSHRC_READ:-no}\"")});
    shell.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&finished]() { return finished; })) << shell.errorString().toStdString();
    // clang-format on

    // The files a reader really keeps are the ones their own startup file points at, rather than the ones the home directory happens to hold.
    const QByteArray answer = shell.readAllStandardOutput();
    EXPECT_TRUE(answer.contains(QByteArrayLiteral("profile=yes"))) << answer.toStdString();
    EXPECT_TRUE(answer.contains(QByteArrayLiteral("zshrc=yes"))) << answer.toStdString();
}
#endif

TEST(TerminalFontTests, OpensOnTheFirstDeclaredFamilyThatIsInstalled) {
    const QStringList& installed = ui::Components::monospacedFontFamilies();
    ASSERT_FALSE(installed.isEmpty());
    const QString opened = ui::Components::defaultMonospacedFontFamily();
    ASSERT_FALSE(opened.isEmpty());

    for (const auto& preferred : ui::Components::preferredMonospacedFontFamilies()) {
        if (installed.contains(preferred)) {
            EXPECT_EQ(opened, preferred);

            if (installed.first() != preferred) {
                EXPECT_NE(opened, installed.first()) << "the family is still the one that happens to sort first";
            }

            return;
        }
    }

    EXPECT_EQ(opened, installed.first());
}

TEST(TerminalFontTests, SizesEveryCellToWhatItsFontAsksForRatherThanPastIt) {
    test::TestPluginHost host;
    ui::TerminalWidget widget(host);
    const QString family = ui::Components::defaultMonospacedFontFamily();
    const int horizontalPadding = host.theme().metric(ui::ThemeMetric::TerminalHorizontalPadding) * 2;
    const int verticalPadding = host.theme().metric(ui::ThemeMetric::TerminalVerticalPadding) * 2;
    const int columns = host.theme().metric(ui::ThemeMetric::TerminalMinimumColumns);
    const int rows = host.theme().metric(ui::ThemeMetric::TerminalMinimumRows);
    ASSERT_GT(columns, 0);
    ASSERT_GT(rows, 0);

    for (const int pointSize : {8, 12, 18, 36}) {
        widget.setTerminalFont(family, pointSize);
        QFont font(family);
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        font.setPointSizeF(pointSize);
        const QFontMetricsF metrics(font);
        const QSize minimum = widget.minimumSizeHint();
        const qreal cellWidth = static_cast<qreal>(minimum.width() - horizontalPadding) / columns;
        const qreal cellHeight = static_cast<qreal>(minimum.height() - verticalPadding) / rows;

        EXPECT_LE(std::abs(cellWidth - metrics.horizontalAdvance(QLatin1Char('M'))), 0.5) << "the cell leaves a gap after every glyph at " << pointSize << " points";
        EXPECT_LE(std::abs(cellHeight - metrics.lineSpacing()), 0.5) << "the cell leaves a gap under every line at " << pointSize << " points";
    }
}

// Pasting asks nothing, so a host that would refuse every question is never given one to answer.
TEST(TerminalWidgetTests, PastesWhatTheClipboardHoldsWithoutAskingAnything) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    QList<FakePtyBackend*> backends;
    // clang-format off
    terminalcore::PtyBackendFactory factory = [&backends]() {
        auto backend = std::make_unique<FakePtyBackend>();
        backends.append(backend.get());
        return backend;
    };
    // clang-format on
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), std::move(factory));
    ASSERT_TRUE(manager.initialize().hasValue());
    auto* session = qobject_cast<terminalcore::TerminalSession*>(manager.sessionObject(manager.currentFocusedSessionId()));
    ASSERT_NE(session, nullptr);
    ASSERT_FALSE(backends.isEmpty());

    ui::TerminalWidget widget(host);
    widget.setSession(session);
    QApplication::clipboard()->setText(QStringLiteral("rm -rf /\n"));
    backends.first()->writes.clear();

    // clang-format off
    const auto written = [&backends]() {
        QByteArray joined;
        for (const auto& chunk : backends.first()->writes) {
            joined.append(chunk);
        }
        return joined;
    };
    // clang-format on

    // Text that would run reaches the shell exactly like text that merely wraps, and nobody is asked about either.
    host.confirmation = false;
    QTest::keyClick(&widget, Qt::Key_Insert, Qt::ShiftModifier);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&written]() { return written().contains(QByteArrayLiteral("rm -rf /")); }));
    // clang-format on
    EXPECT_TRUE(host.confirmedTitles.isEmpty());

    backends.first()->writes.clear();
    QApplication::clipboard()->setText(QStringLiteral("just words"));
    QTest::keyClick(&widget, Qt::Key_Insert, Qt::ShiftModifier);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&written]() { return written().contains(QByteArrayLiteral("just words")); }));
    // clang-format on
    EXPECT_TRUE(host.confirmedTitles.isEmpty());
    widget.setSession(nullptr);
}

TEST(TerminalTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    workpane::plugins::terminalplugin::TerminalPlugin plugin;
    workpane::test::TestCatalogs::expectCompleteCatalog(QStringLiteral("terminal"), plugin.translations());
}

// A toast never shows the diagnostic of the engine, so the two conditions a reader reaches carry a sentence of the catalog.
TEST(TerminalFailureTest, SaysWhatStoppedTheTerminalInTheLanguageOfTheReader) {
    test::TestPluginHost host;
    host.translations.insert(QStringLiteral("terminal.error.input-queue-full"), QStringLiteral("O terminal ainda esta lendo"));
    host.translations.insert(QStringLiteral("terminal.error.not-running"), QStringLiteral("O shell terminou"));

    EXPECT_EQ(plugins::terminalplugin::TerminalFailures::terminalFailureMessage({"terminal_input_queue_full", QStringLiteral("The terminal input queue is full"), {}}, host), QStringLiteral("O terminal ainda esta lendo"));
    EXPECT_EQ(plugins::terminalplugin::TerminalFailures::terminalFailureMessage({"terminal_not_running", QStringLiteral("The terminal process is not running"), {}}, host), QStringLiteral("O shell terminou"));

    // Starting a terminal reaches the reader too, so what stopped it reads in their language rather than in the words of the platform.
    host.translations.insert(QStringLiteral("terminal.error.shell-not-executable"), QStringLiteral("O shell nao pode ser executado"));
    host.translations.insert(QStringLiteral("terminal.error.workdir-missing"), QStringLiteral("O diretorio nao existe mais"));
    host.translations.insert(QStringLiteral("terminal.error.backend-failed"), QStringLiteral("O terminal parou de responder"));
    host.translations.insert(QStringLiteral("terminal.error.workspace-invalid"), QStringLiteral("O espaco de trabalho nao pode ser salvo"));
    EXPECT_EQ(plugins::terminalplugin::TerminalFailures::terminalFailureMessage({"shell_not_executable", QStringLiteral("The selected shell is not executable"), {}}, host), QStringLiteral("O shell nao pode ser executado"));
    EXPECT_EQ(plugins::terminalplugin::TerminalFailures::terminalFailureMessage({"terminal_working_directory_missing", QStringLiteral("The working directory does not exist"), {}}, host), QStringLiteral("O diretorio nao existe mais"));
    EXPECT_EQ(plugins::terminalplugin::TerminalFailures::terminalFailureMessage({"terminal_backend_failed", QStringLiteral("Broken pipe"), {}}, host), QStringLiteral("O terminal parou de responder"));
    EXPECT_EQ(plugins::terminalplugin::TerminalFailures::terminalFailureMessage({"terminal_workspace_invalid", QStringLiteral("The terminal workspace is invalid"), {}}, host), QStringLiteral("O espaco de trabalho nao pode ser salvo"));

    host.translations.insert(QStringLiteral("terminal.error.thread-unavailable"), QStringLiteral("O sistema recusou a thread"));
    EXPECT_EQ(plugins::terminalplugin::TerminalFailures::terminalFailureMessage({"terminal_thread_unavailable", QStringLiteral("The system refused a thread this terminal needs"), {}}, host), QStringLiteral("O sistema recusou a thread"));

    // A fault of the emulator names no condition the reader can answer, so it reads as one sentence of the catalog rather than as the text of the log.
    host.translations.insert(QStringLiteral("terminal.error.unexpected"), QStringLiteral("O terminal relatou uma falha"));
    const QString fault = plugins::terminalplugin::TerminalFailures::terminalFailureMessage({"ghostty_key_encoding_failed", QStringLiteral("The key event could not be encoded"), {}}, host);
    EXPECT_EQ(fault, QStringLiteral("O terminal relatou uma falha"));
    EXPECT_NE(fault, QStringLiteral("The key event could not be encoded")) << "the reader is shown the message the log was written with";
}

// The plugin that refuses knows the condition while the terminal owns what the reader is told, so the toast never quotes the other one.
TEST(TerminalPaneTest, TellsTheReaderInItsOwnWordsWhenADestinationRefuses) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    host.translations = {{QStringLiteral("terminal.plugin.title"), QStringLiteral("Terminal")}, {QStringLiteral("terminal.error.destination-open"), QStringLiteral("O plugin que recebe este diretorio nao pode abri-lo")}, {QStringLiteral("terminal.workspace.default-name"), QStringLiteral("Workspace")}};
    host.availableCapabilities.insert(QString::fromLatin1(plugins::openFolderCapability));
    // clang-format off
    host.capabilityHandler = [](const QString&, const QJsonObject&, QObject*, plugins::PluginReply reply) { reply(Result<QJsonObject>::failure({"code_editor_workspace_invalid", QStringLiteral("The code editor workspace is unavailable"), QStringLiteral("/gone")})); };
    // clang-format on
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);

    // clang-format off
    terminalcore::PtyBackendFactory factory = []() { return std::make_unique<FakePtyBackend>(); };
    // clang-format on
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), std::move(factory));
    ASSERT_TRUE(manager.initialize().hasValue());
    auto* session = qobject_cast<terminalcore::TerminalSession*>(manager.sessionObject(manager.currentFocusedSessionId()));
    ASSERT_NE(session, nullptr);
    ASSERT_FALSE(session->cwd().isEmpty());

    plugins::terminalplugin::TerminalPane pane(*session, host);
    host.notifications.clear();
    ASSERT_TRUE(QMetaObject::invokeMethod(&pane, "offerDirectory", Q_ARG(QString, QString::fromLatin1(plugins::openFolderCapability))));
    ASSERT_EQ(host.notifications.size(), 1);
    EXPECT_EQ(host.notifications.first().message, QStringLiteral("O plugin que recebe este diretorio nao pode abri-lo"));
    EXPECT_NE(host.notifications.first().message, QStringLiteral("The code editor workspace is unavailable")) << "the reader is shown the message the other plugin wrote for the log";
}

// A program that asked for the mouse receives every wheel notch, and a trackpad moves sideways as readily as it moves down.
// A modifier held on its own writes nothing, so the viewport must stay where the reader parked it while they copy.
TEST(TerminalWidgetTests, LeavesTheViewportParkedWhileAModifierIsHeldOnItsOwn) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    QList<FakePtyBackend*> backends;
    // clang-format off
    terminalcore::PtyBackendFactory factory = [&backends]() {
        auto backend = std::make_unique<FakePtyBackend>();
        backends.append(backend.get());
        return backend;
    };
    // clang-format on
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), std::move(factory));
    ASSERT_TRUE(manager.initialize().hasValue());
    auto* session = qobject_cast<terminalcore::TerminalSession*>(manager.sessionObject(manager.currentFocusedSessionId()));
    ASSERT_NE(session, nullptr);
    ASSERT_FALSE(backends.isEmpty());

    ui::TerminalWidget widget(host);
    widget.setSession(session);
    widget.resize(800, 600);

    QByteArray history;

    for (int line = 0; line < 200; ++line) {
        history.append(QByteArrayLiteral("line ") + QByteArray::number(line) + QByteArrayLiteral("\r\n"));
    }

    backends.first()->sendOutput(history);
    QCoreApplication::processEvents();

    session->scrollToBottom();
    const quint64 bottom = session->snapshot().scrollOffset;
    session->scrollViewport(-40);
    const quint64 parked = session->snapshot().scrollOffset;
    ASSERT_NE(parked, bottom) << "the output left no scrollback for the reader to park in";

    QKeyEvent held(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
    QApplication::sendEvent(&widget, &held);
    EXPECT_EQ(session->snapshot().scrollOffset, parked);

    QKeyEvent shifted(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    QApplication::sendEvent(&widget, &shifted);
    EXPECT_EQ(session->snapshot().scrollOffset, parked);

    // What the reader really types does bring the prompt back under them.
    QKeyEvent typed(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    QApplication::sendEvent(&widget, &typed);
    EXPECT_EQ(session->snapshot().scrollOffset, bottom);
}

// A shell binds the movement sequences rather than the combinations they are typed as, so those are what reach it.
TEST(TerminalWidgetTests, MovesByWordAndToTheEndsOfTheLineThroughTheSequencesAShellBinds) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    QList<FakePtyBackend*> backends;
    // clang-format off
    terminalcore::PtyBackendFactory factory = [&backends]() {
        auto backend = std::make_unique<FakePtyBackend>();
        backends.append(backend.get());
        return backend;
    };
    // clang-format on
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), std::move(factory));
    ASSERT_TRUE(manager.initialize().hasValue());
    auto* session = qobject_cast<terminalcore::TerminalSession*>(manager.sessionObject(manager.currentFocusedSessionId()));
    ASSERT_NE(session, nullptr);
    ASSERT_FALSE(backends.isEmpty());

    ui::TerminalWidget widget(host);
    widget.setSession(session);
    backends.first()->writes.clear();

    QKeyEvent wordLeft(QEvent::KeyPress, Qt::Key_Left, terminalcore::wordModifier);
    QKeyEvent wordRight(QEvent::KeyPress, Qt::Key_Right, terminalcore::wordModifier);
    QApplication::sendEvent(&widget, &wordLeft);
    QApplication::sendEvent(&widget, &wordRight);
    ASSERT_EQ(backends.first()->writes.size(), 2);
    EXPECT_EQ(backends.first()->writes.at(0), terminalcore::TerminalShortcuts::wordSequence(true));
    EXPECT_EQ(backends.first()->writes.at(1), terminalcore::TerminalShortcuts::wordSequence(false));

    // A POSIX shell reads the meta letter it binds, and PSReadLine reads the modified arrow it binds, so neither is given the sequence of the other.
#ifdef Q_OS_WIN
    EXPECT_EQ(terminalcore::TerminalShortcuts::wordSequence(true), QByteArrayLiteral("\033[1;5D"));
    EXPECT_EQ(terminalcore::TerminalShortcuts::wordSequence(false), QByteArrayLiteral("\033[1;5C"));
#else
    EXPECT_EQ(terminalcore::TerminalShortcuts::wordSequence(true), QByteArrayLiteral("\033b"));
    EXPECT_EQ(terminalcore::TerminalShortcuts::wordSequence(false), QByteArrayLiteral("\033f"));
#endif

    // An arrow nobody modified still reaches the shell as the arrow it is.
    backends.first()->writes.clear();
    QKeyEvent plainLeft(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QApplication::sendEvent(&widget, &plainLeft);
    ASSERT_EQ(backends.first()->writes.size(), 1);
    EXPECT_NE(backends.first()->writes.at(0), terminalcore::TerminalShortcuts::wordSequence(true));

#ifdef Q_OS_MACOS
    // Command reaches the ends of the line, which is what Home and End answer on the platforms that carry them.
    backends.first()->writes.clear();
    QKeyEvent lineStart(QEvent::KeyPress, Qt::Key_Left, terminalcore::applicationModifier);
    QKeyEvent lineEnd(QEvent::KeyPress, Qt::Key_Right, terminalcore::applicationModifier);
    QApplication::sendEvent(&widget, &lineStart);
    QApplication::sendEvent(&widget, &lineEnd);
    ASSERT_EQ(backends.first()->writes.size(), 2);
    EXPECT_EQ(backends.first()->writes.at(0), QByteArrayLiteral("\001"));
    EXPECT_EQ(backends.first()->writes.at(1), QByteArrayLiteral("\005"));
#endif
}

TEST(TerminalWidgetTests, ReportsEveryWheelNotchToAProgramReadingTheMouseOnBothAxes) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    plugins::terminalplugin::TerminalWorkspaceRepository repository(host);
    QList<FakePtyBackend*> backends;
    // clang-format off
    terminalcore::PtyBackendFactory factory = [&backends]() {
        auto backend = std::make_unique<FakePtyBackend>();
        backends.append(backend.get());
        return backend;
    };
    // clang-format on
    plugins::terminalplugin::workspace::WorkspaceManager manager(repository, host, directory.path(), *terminalcore::TerminalThemes::terminalTheme(QStringLiteral("balanced")), std::move(factory));
    ASSERT_TRUE(manager.initialize().hasValue());
    auto* session = qobject_cast<terminalcore::TerminalSession*>(manager.sessionObject(manager.currentFocusedSessionId()));
    ASSERT_NE(session, nullptr);
    ASSERT_FALSE(backends.isEmpty());

    ui::TerminalWidget widget(host);
    widget.resize(600, 400);
    widget.setSession(session);

    // The program asks for the mouse and for the format that names the cell in decimal.
    backends.first()->sendOutput(QByteArrayLiteral("\x1b[?1000h\x1b[?1006h"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([session]() { return session->programWantsMouse(); }));
    // clang-format on
    backends.first()->writes.clear();

    // clang-format off
    const auto written = [&backends]() {
        QByteArray joined;
        for (const auto& chunk : backends.first()->writes) {
            joined.append(chunk);
        }
        return joined;
    };
    const auto notch = [&widget](QPoint angle) {
        QWheelEvent event(QPointF(80, 64), widget.mapToGlobal(QPointF(80, 64)), QPoint(), angle, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&widget, &event);
    };
    // clang-format on

    notch(QPoint(0, 120));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&written]() { return written().contains(QByteArrayLiteral("\x1b[<64;")); })) << written().toStdString();
    // clang-format on

    backends.first()->writes.clear();
    notch(QPoint(0, -120));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&written]() { return written().contains(QByteArrayLiteral("\x1b[<65;")); })) << written().toStdString();
    // clang-format on

    // A sideways notch travels as the button the protocol reserves for it rather than being swallowed.
    backends.first()->writes.clear();
    notch(QPoint(120, 0));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&written]() { return written().contains(QByteArrayLiteral("\x1b[<66;")); })) << written().toStdString();
    // clang-format on

    backends.first()->writes.clear();
    notch(QPoint(-120, 0));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&written]() { return written().contains(QByteArrayLiteral("\x1b[<67;")); })) << written().toStdString();
    // clang-format on
}

// A path a drop delivers is written to the shell, so a drop carrying anything the shell would act on delivers nothing at all.
TEST(TerminalWidgetTests, DeliversADroppedPathAndRefusesADropThatWouldRunSomething) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString dropped = QDir(directory.path()).absoluteFilePath(QStringLiteral("a file.txt"));

    // clang-format off
    const auto delivered = [](const QList<QUrl>& urls) {
        QMimeData payload;
        payload.setUrls(urls);
        return ui::TerminalDrops::localPathsFromDrop(payload);
    };
    // clang-format on

    EXPECT_EQ(delivered({QUrl::fromLocalFile(dropped)}), QStringList{dropped});

    // A drop that carries no address at all delivers nothing.
    QMimeData empty;
    EXPECT_TRUE(ui::TerminalDrops::localPathsFromDrop(empty).isEmpty());

    // A line break would end the command the path was written into and start another, so the whole drop is refused.
    EXPECT_TRUE(delivered({QUrl::fromLocalFile(dropped), QUrl::fromLocalFile(directory.path() + QStringLiteral("/one\ntwo"))}).isEmpty());
    EXPECT_TRUE(delivered({QUrl::fromLocalFile(directory.path() + QStringLiteral("/one\rtwo"))}).isEmpty());

    // An address the shell has no path for is refused, and one bad entry refuses the drop it arrived in.
    EXPECT_TRUE(delivered({QUrl(QStringLiteral("https://example.com"))}).isEmpty());
    EXPECT_TRUE(delivered({QUrl::fromLocalFile(dropped), QUrl(QStringLiteral("https://example.com"))}).isEmpty());

    // What a refused drop would have written is what makes refusing it worth doing.
    terminalcore::ShellProfile profile;
    profile.id = QStringLiteral("zsh");
    EXPECT_EQ(terminalcore::ShellPaths::formatLocalPathsForShell(profile, {dropped}), QStringLiteral("'%1' ").arg(dropped));
}

// Clearing empties what was read and asks the shell to print its prompt again, which is what the `clear` command leaves behind.
TEST(TerminalSessionTest, ClearingTheBufferEmptiesTheHistoryAndAsksTheShellToRedrawItsPrompt) {
    auto backend = std::make_unique<FakePtyBackend>();
    auto* backendPointer = backend.get();
    auto session = TerminalTestsHelper::createSession(std::move(backend));
    ASSERT_TRUE(session->start().hasValue());
    ASSERT_TRUE(session->resize(80, 24, 8, 16).hasValue());

    backendPointer->sendOutput(QByteArrayLiteral("first line\r\nsecond line\r\n"));
    QCoreApplication::processEvents();
    ASSERT_FALSE(session->search(QStringLiteral("second line"), false, false, 10).isEmpty());
    backendPointer->writes.clear();

    ASSERT_TRUE(session->clearBuffer().hasValue());

    EXPECT_TRUE(session->search(QStringLiteral("second line"), false, false, 10).isEmpty());
    ASSERT_EQ(backendPointer->writes.size(), 1);
    EXPECT_EQ(backendPointer->writes.first(), QByteArrayLiteral("\f"));
}

// A shell that already ended has nothing to redraw, so clearing still empties what the reader was looking at.
TEST(TerminalSessionTest, ClearingABufferWhoseShellEndedStillEmptiesIt) {
    auto backend = std::make_unique<FakePtyBackend>();
    auto* backendPointer = backend.get();
    auto session = TerminalTestsHelper::createSession(std::move(backend));
    ASSERT_TRUE(session->start().hasValue());
    ASSERT_TRUE(session->resize(80, 24, 8, 16).hasValue());

    backendPointer->sendOutput(QByteArrayLiteral("first line\r\n"));
    QCoreApplication::processEvents();
    backendPointer->isRunning = false;
    backendPointer->writes.clear();

    ASSERT_TRUE(session->clearBuffer().hasValue());

    EXPECT_TRUE(session->search(QStringLiteral("first line"), false, false, 10).isEmpty());
    EXPECT_TRUE(backendPointer->writes.isEmpty());
}

// The key the reader presses is what clears the buffer, so the widget answers it rather than the menu alone.
TEST(TerminalWidgetTests, AnswersTheClearBufferKeyOfItsPlatform) {
    auto backend = std::make_unique<FakePtyBackend>();
    auto* backendPointer = backend.get();
    auto session = TerminalTestsHelper::createSession(std::move(backend));
    ASSERT_TRUE(session->start().hasValue());
    ASSERT_TRUE(session->resize(80, 24, 8, 16).hasValue());

    test::TestPluginHost host;
    ui::TerminalWidget widget(host);
    widget.setSession(session.get());
    backendPointer->writes.clear();

#ifdef Q_OS_MACOS
    QKeyEvent clearBuffer(QEvent::KeyPress, Qt::Key_K, terminalcore::applicationModifier);
#else
    QKeyEvent clearBuffer(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
#endif
    QApplication::sendEvent(&widget, &clearBuffer);

    ASSERT_EQ(backendPointer->writes.size(), 1);
    EXPECT_EQ(backendPointer->writes.first(), QByteArrayLiteral("\f"));
    widget.setSession(nullptr);
}

// A destination that leaves the application is intercepted here, because a case must never really open a browser.
class RecordingUrlHandler final : public QObject {
    Q_OBJECT

  public:
    QStringList opened;

  public slots:
    void handle(const QUrl& url) {
        opened.append(url.toString());
    }
};

// An address printed by a program opens where the reader asked for it rather than where the product prefers.
TEST(TerminalPaneTest, OpensAnAddressWhereTheReaderAskedForIt) {
    auto backend = std::make_unique<FakePtyBackend>();
    auto session = TerminalTestsHelper::createSession(std::move(backend));
    ASSERT_TRUE(session->start().hasValue());

    test::TestPluginHost host;
    host.availableCapabilities.insert(QString::fromLatin1(plugins::openPageCapability));
    plugins::terminalplugin::TerminalPane pane(*session, host);

    RecordingUrlHandler handler;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &handler, "handle");

    ASSERT_TRUE(QMetaObject::invokeMethod(&pane, "openAddress", Q_ARG(QString, QStringLiteral("https://example.com/one"))));
    ASSERT_EQ(host.capabilityInvocations.size(), 1);
    EXPECT_EQ(host.capabilityInvocations.at(0).name, QString::fromLatin1(plugins::openPageCapability));
    EXPECT_EQ(host.capabilityInvocations.at(0).payload.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.com/one"));
    EXPECT_TRUE(handler.opened.isEmpty());

    // Once the reader asks for the browser of their system, the plugin that browses is never invoked.
    pane.setOpenLinksInSystemBrowser(true);
    ASSERT_TRUE(QMetaObject::invokeMethod(&pane, "openAddress", Q_ARG(QString, QStringLiteral("https://example.com/two"))));
    EXPECT_EQ(host.capabilityInvocations.size(), 1);
    EXPECT_EQ(handler.opened, QStringList{QStringLiteral("https://example.com/two")});

    // A run with no plugin that browses still reaches the browser of the system.
    pane.setOpenLinksInSystemBrowser(false);
    host.availableCapabilities.clear();
    ASSERT_TRUE(QMetaObject::invokeMethod(&pane, "openAddress", Q_ARG(QString, QStringLiteral("https://example.com/three"))));
    EXPECT_EQ(host.capabilityInvocations.size(), 1);
    EXPECT_EQ(handler.opened.size(), 2);

    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

// Both integrations keep their startup files beside the history of the terminal, so one of them writing is one condition rather than two.
TEST(ShellIntegrationFilesTest, WritesWhatChangedAndLeavesTheRestAsTheReaderHasIt) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const auto written = terminalcore::ShellIntegrationFiles::write(directory.path(), QStringLiteral("shell"), {{QStringLiteral("first"), QByteArrayLiteral("one")}, {QStringLiteral("second"), QByteArrayLiteral("two")}});
    ASSERT_TRUE(written.hasValue());
    EXPECT_EQ(written.value(), QDir(directory.path()).filePath(QStringLiteral("shell")));

    const QString first = QDir(written.value()).filePath(QStringLiteral("first"));
    QFile stored(first);
    ASSERT_TRUE(stored.open(QIODevice::ReadOnly));
    EXPECT_EQ(stored.readAll(), QByteArrayLiteral("one"));
    stored.close();

    // A file nobody changed keeps the moment it was written, because rewriting it on every terminal is a change the reader never made.
    const QDateTime firstWrite = QFileInfo(first).lastModified();
    ASSERT_TRUE(terminalcore::ShellIntegrationFiles::write(directory.path(), QStringLiteral("shell"), {{QStringLiteral("first"), QByteArrayLiteral("one")}}).hasValue());
    EXPECT_EQ(QFileInfo(first).lastModified(), firstWrite);

    ASSERT_TRUE(terminalcore::ShellIntegrationFiles::write(directory.path(), QStringLiteral("shell"), {{QStringLiteral("first"), QByteArrayLiteral("changed")}}).hasValue());
    ASSERT_TRUE(stored.open(QIODevice::ReadOnly));
    EXPECT_EQ(stored.readAll(), QByteArrayLiteral("changed"));
    stored.close();

    // A directory that cannot be created is refused by name rather than answered with a path nobody can write to.
    const QString blocked = directory.filePath(QStringLiteral("blocked"));
    ASSERT_TRUE(TerminalTestsHelper::writeShellFile(blocked, QByteArrayLiteral("not a directory")));
    EXPECT_EQ(terminalcore::ShellIntegrationFiles::write(blocked, QStringLiteral("shell"), {{QStringLiteral("first"), QByteArrayLiteral("one")}}).error().code, QStringLiteral("shell_integration_directory_failed"));
}

#ifdef Q_OS_WIN

// Windows tells nobody which directory another process is standing in, so the shell marks its own and the session reads it from there.
TEST(WindowsShellIntegrationTest, WritesAPromptThatReportsTheDirectoryOfTheShell) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString historyFile = directory.filePath(QStringLiteral("history"));

    terminalcore::ShellProfile profile{QStringLiteral("pwsh"), QStringLiteral("pwsh"), QStringLiteral("pwsh.exe"), {}};
    ASSERT_TRUE(terminalcore::WindowsShellIntegration::configure(profile, historyFile).hasValue());

    const QString script = QDir(directory.path()).filePath(QStringLiteral("powershell/workpane.ps1"));
    ASSERT_TRUE(QFileInfo(script).isFile());
    ASSERT_EQ(profile.arguments.size(), 3);
    EXPECT_EQ(profile.arguments.at(0), QStringLiteral("-NoExit"));
    EXPECT_EQ(profile.arguments.at(1), QStringLiteral("-Command"));
    EXPECT_EQ(profile.arguments.at(2), QStringLiteral(". %1").arg(terminalcore::ShellPaths::quoteForPowerShell(script)));

    // A shell that is not PowerShell is left exactly as it was, because this prompt is the one PowerShell writes.
    terminalcore::ShellProfile commandPrompt{QStringLiteral("cmd"), QStringLiteral("cmd"), QStringLiteral("cmd.exe"), {}};
    ASSERT_TRUE(terminalcore::WindowsShellIntegration::configure(commandPrompt, historyFile).hasValue());
    EXPECT_TRUE(commandPrompt.arguments.isEmpty());

    const QString shell = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));

    if (shell.isEmpty()) {
        GTEST_SKIP() << "this platform carries no PowerShell to answer with";
    }

    QProcess powerShell;
    bool finished = false;
    // clang-format off
    QObject::connect(&powerShell, &QProcess::finished, &powerShell, [&finished](int, QProcess::ExitStatus) { finished = true; });
    // clang-format on
    powerShell.setProgram(shell);
    powerShell.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-Command"), QStringLiteral(". %1 ; Set-Location %2 ; prompt | Out-Null").arg(terminalcore::ShellPaths::quoteForPowerShell(script), terminalcore::ShellPaths::quoteForPowerShell(directory.path()))});
    powerShell.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&finished]() { return finished; })) << powerShell.errorString().toStdString();
    // clang-format on

    // The address the prompt marks is the one the session already decodes into the directory the reader is standing in.
    const QString answer = QString::fromUtf8(powerShell.readAllStandardOutput());
    const QUrl reported(answer.section(QStringLiteral("\033]7;"), 1).section(QChar(7), 0, 0));
    ASSERT_TRUE(reported.isLocalFile()) << answer.toStdString();
    EXPECT_EQ(QDir::cleanPath(reported.toLocalFile()), QDir::cleanPath(directory.path()));
}

#endif

// Both backends start their shell from one environment, so neither platform can describe this terminal differently.
TEST(TerminalEnvironmentTests, DeclaresTheTerminalToEveryPlatformInTheSameWords) {
    const QProcessEnvironment environment = terminalcore::TerminalEnvironment::sessionEnvironment();

    EXPECT_EQ(environment.value(QStringLiteral("TERM")), QStringLiteral("xterm-256color"));
    EXPECT_EQ(environment.value(QStringLiteral("COLORTERM")), QStringLiteral("truecolor"));
    EXPECT_EQ(environment.value(QStringLiteral("TERM_PROGRAM")), QStringLiteral("Workpane"));

    // What the reader configured for themselves is carried through, because the shell is theirs rather than ours.
    const QProcessEnvironment inherited = QProcessEnvironment::systemEnvironment();

    for (const auto& name : inherited.keys()) {
        if (name == QStringLiteral("TERM") || name == QStringLiteral("COLORTERM") || name == QStringLiteral("TERM_PROGRAM")) {
            continue;
        }
        EXPECT_EQ(environment.value(name), inherited.value(name)) << "the shell lost " << name.toStdString();
    }
}

} // namespace workpane

#include "TerminalTests.moc"
