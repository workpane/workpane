#include "SystemInformation.h"
#include "SystemInformationPlugin.h"
#include "SystemInformationTranslations.h"
#include "TestFuture.h"
#include "TestPluginHost.h"
#include "TestTranslations.h"
#include "ui/Icons.h"

#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace workpane::plugins::systeminformation {

class TestProvider final : public SystemInformationProvider {
  public:
    explicit TestProvider(std::function<Result<SystemSnapshot>(const std::atomic_bool&)> handler) : m_handler(std::move(handler)) {}

    Result<SystemSnapshot> collect(const std::atomic_bool& cancelled) override {
        ++calls;
        return m_handler(cancelled);
    }

    std::atomic_int calls{0};

  private:
    std::function<Result<SystemSnapshot>(const std::atomic_bool&)> m_handler;
};

TEST(SystemInformationProviderTest, CollectsAValidSnapshotFromTheCurrentMachine) {
    auto collection = SystemCollection::collectSystemInformation(std::make_shared<HwinfoSystemInformationProvider>());
    const auto result = test::TestFutures::awaitFuture(collection.future);
    ASSERT_TRUE(result.hasValue()) << result.error().code.toStdString() << " " << result.error().detail.toStdString();
    const auto& snapshot = result.value();
    EXPECT_TRUE(snapshot.capturedAtUtc.isValid());
    EXPECT_EQ(snapshot.capturedAtUtc.timeSpec(), Qt::UTC);
    EXPECT_TRUE(snapshot.operatingSystem.architectureBits == 0 || snapshot.operatingSystem.architectureBits == 32 || snapshot.operatingSystem.architectureBits == 64);
    EXPECT_LE(snapshot.memory.availableBytes, snapshot.memory.totalBytes);

    for (const auto& processor : snapshot.processors) {
        EXPECT_GE(processor.logicalCoreCount, processor.physicalCoreCount);
    }

    if (snapshot.processorUsage.utilization.has_value()) {
        EXPECT_GE(snapshot.processorUsage.utilization.value(), 0.0);
        EXPECT_LE(snapshot.processorUsage.utilization.value(), 1.0);
    }

#if defined(Q_OS_MACOS)
    EXPECT_FALSE(snapshot.processorUsage.utilization.has_value());
    EXPECT_TRUE(snapshot.processorUsage.threadUtilization.isEmpty());
    EXPECT_TRUE(snapshot.processorUsage.threadFrequencyHz.isEmpty());
#endif

    // clang-format off
    const auto printable = [](const QString& value) { return std::all_of(value.cbegin(), value.cend(), [](QChar character) { return character.isPrint(); }); };
    // clang-format on
    const QStringList operatingSystemValues{snapshot.operatingSystem.hostName, snapshot.operatingSystem.name, snapshot.operatingSystem.version, snapshot.operatingSystem.kernel};

    for (const auto& value : operatingSystemValues) {
        EXPECT_TRUE(printable(value)) << value.toStdString();
        EXPECT_EQ(value, value.trimmed());
    }

    for (const auto& processor : snapshot.processors) {
        EXPECT_TRUE(printable(processor.vendor)) << processor.vendor.toStdString();
        EXPECT_TRUE(printable(processor.model)) << processor.model.toStdString();
    }
}

// A reader diagnosing a surface that looks too large needs the resolution and the scale, and the window system is the only thing that knows them.
TEST(SystemInformationProviderTest, ReadsEveryScreenTheWindowSystemOffers) {
    const QVector<Display> displays = SystemCollection::connectedDisplays();
    ASSERT_EQ(displays.size(), QGuiApplication::screens().size());

    int primaries = 0;

    for (const auto& display : displays) {
        EXPECT_GT(display.widthPixels, 0);
        EXPECT_GT(display.heightPixels, 0);
        EXPECT_GT(display.devicePixelRatio, 0.0);
        EXPECT_GT(display.logicalDotsPerInch, 0.0);
        primaries += display.primary ? 1 : 0;
    }

    // Exactly one screen is the primary one whenever the window system offers any.
    EXPECT_EQ(primaries, displays.isEmpty() ? 0 : 1);
}

TEST(SystemInformationProviderTest, CancelsBeforeAccessingHardware) {
    HwinfoSystemInformationProvider provider;
    std::atomic_bool cancelled{true};
    const auto result = provider.collect(cancelled);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, QStringLiteral("systeminformation_collection_cancelled"));
}

TEST(SystemInformationPluginTest, CancelsAnActiveWorkerBeforePluginUnload) {
    test::TestPluginHost host;
    QSemaphore started;
    std::atomic_bool observedCancellation{false};
    // clang-format off
    auto provider = std::make_shared<TestProvider>([&started, &observedCancellation](const std::atomic_bool& cancelled) { started.release(); while (!cancelled.load(std::memory_order_relaxed)) { std::this_thread::yield(); } observedCancellation.store(true); return Result<SystemSnapshot>::failure({"cancelled", "Cancelled", {}}); });
    // clang-format on
    SystemInformationPlugin plugin(provider);
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(plugin.refresh().hasValue());
    ASSERT_TRUE(started.tryAcquire(1, 3000));
    plugin.shutdown();
    EXPECT_TRUE(observedCancellation.load());
    EXPECT_FALSE(plugin.isRefreshing());
    EXPECT_FALSE(plugin.snapshot().has_value());
}

class SystemInformationTestsHelper final {
  public:
    static SystemSnapshot sampleSnapshot();
    static void installEnglishTranslations(test::TestPluginHost& host);
};

TEST(SystemInformationProviderTest, CollectsOnAWorkerAndMapsProviderExceptions) {
    QThread* callerThread = QThread::currentThread();
    std::atomic_bool usedWorkerThread{false};
    // clang-format off
    auto provider = std::make_shared<TestProvider>([callerThread, &usedWorkerThread](const std::atomic_bool&) { usedWorkerThread.store(QThread::currentThread() != callerThread); return Result<SystemSnapshot>::success(SystemInformationTestsHelper::sampleSnapshot()); });
    // clang-format on
    auto collection = SystemCollection::collectSystemInformation(provider);
    const auto result = test::TestFutures::awaitFuture(collection.future);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(usedWorkerThread.load());
    EXPECT_EQ(result.value().processors.first().model, QStringLiteral("Test Processor"));
    EXPECT_EQ(provider->calls.load(), 1);

    auto unavailableCollection = SystemCollection::collectSystemInformation(nullptr);
    EXPECT_EQ(test::TestFutures::awaitFuture(unavailableCollection.future).error().code, QStringLiteral("systeminformation_provider_unavailable"));

    // clang-format off
    auto throwingProvider = std::make_shared<TestProvider>([](const std::atomic_bool&) -> Result<SystemSnapshot> { throw std::runtime_error("provider failure"); });
    // clang-format on
    auto failedCollection = SystemCollection::collectSystemInformation(throwingProvider);
    const auto failure = test::TestFutures::awaitFuture(failedCollection.future);
    ASSERT_FALSE(failure.hasValue());
    EXPECT_EQ(failure.error().code, QStringLiteral("systeminformation_collection_failed"));
    EXPECT_EQ(failure.error().detail, QStringLiteral("provider failure"));
}
TEST(SystemInformationPluginTest, PublishesCompleteMetadataAndRejectsInvalidLifecycleOperations) {
    test::TestPluginHost host;
    // clang-format off
    auto provider = std::make_shared<TestProvider>([](const std::atomic_bool&) { return Result<SystemSnapshot>::success(SystemInformationTestsHelper::sampleSnapshot()); });
    // clang-format on
    SystemInformationPlugin plugin(provider);
    EXPECT_EQ(plugin.id(), QStringLiteral("system-information"));
    EXPECT_EQ(plugin.titleKey(), QStringLiteral("system-information.plugin.title"));
    EXPECT_TRUE(plugin.dependencies().isEmpty());
    EXPECT_EQ(plugin.databaseSchemaVersion(), 0);
    EXPECT_TRUE(plugin.settingsGroups().isEmpty());
    const auto catalog = plugin.translations();
    ASSERT_TRUE(catalog.contains(QStringLiteral("en")));
    ASSERT_TRUE(catalog.contains(QStringLiteral("pt")));
    EXPECT_EQ(catalog.value(QStringLiteral("pt")).keys(), catalog.value(QStringLiteral("en")).keys());
    EXPECT_EQ(catalog.value(QStringLiteral("en")).value(QStringLiteral("system-information.field.host-name")), QStringLiteral("Host name"));
    EXPECT_FALSE(plugin.styleSheet(host.theme()).isEmpty());
    ASSERT_EQ(plugin.navigationItems(host.theme()).size(), 1);
    EXPECT_EQ(plugin.navigationItems(host.theme()).first().id, QStringLiteral("overview"));
    EXPECT_EQ(plugin.navigationItems(host.theme()).first().placement, NavigationPlacement::Secondary);
    EXPECT_FALSE(plugin.navigationItems(host.theme()).first().icon.isNull());
    EXPECT_EQ(plugin.refresh().error().code, QStringLiteral("systeminformation_not_initialized"));
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_EQ(plugin.initialize(host).error().code, QStringLiteral("systeminformation_already_initialized"));
    EXPECT_EQ(plugin.createNavigationView(QStringLiteral("unknown"), nullptr), nullptr);
    EXPECT_EQ(plugin.createSettingsSection(QStringLiteral("unknown"), QStringLiteral("general"), nullptr), nullptr);

    std::optional<Result<QJsonObject>> response;
    // clang-format off
    plugin.handleRequest(QStringLiteral("test"), QStringLiteral("unknown"), {}, [&response](Result<QJsonObject> result) { response = std::move(result); });
    // clang-format on
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->error().code, QStringLiteral("plugin_message_topic_unknown"));
    plugin.handleEvent(QStringLiteral("test"), QStringLiteral("event"), {});
    plugin.shutdown();
    EXPECT_EQ(plugin.createNavigationView(QStringLiteral("overview"), nullptr), nullptr);

    SystemInformationPlugin unavailable(nullptr);
    EXPECT_EQ(unavailable.initialize(host).error().code, QStringLiteral("systeminformation_provider_unavailable"));
}
TEST(SystemInformationPluginTest, PreventsConcurrentRefreshesAndPublishesOneCompleteSnapshot) {
    test::TestPluginHost host;
    QSemaphore started;
    QSemaphore release;
    // clang-format off
    auto provider = std::make_shared<TestProvider>([&started, &release](const std::atomic_bool&) { started.release(); release.acquire(); return Result<SystemSnapshot>::success(SystemInformationTestsHelper::sampleSnapshot()); });
    // clang-format on
    SystemInformationPlugin plugin(provider);
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QSignalSpy refreshStates(&plugin, &SystemInformationPlugin::refreshStateChanged);
    QSignalSpy snapshots(&plugin, &SystemInformationPlugin::snapshotChanged);
    ASSERT_TRUE(plugin.refresh().hasValue());
    ASSERT_TRUE(started.tryAcquire(1, 3000));
    EXPECT_TRUE(plugin.isRefreshing());
    EXPECT_EQ(plugin.refresh().error().code, QStringLiteral("systeminformation_refresh_in_progress"));
    release.release();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !plugin.isRefreshing(); }, 3000));
    // clang-format on
    ASSERT_TRUE(plugin.snapshot().has_value());
    EXPECT_EQ(plugin.snapshot()->memory.totalBytes, 34359738368ULL);
    EXPECT_EQ(refreshStates.count(), 2);
    EXPECT_EQ(snapshots.count(), 1);
    EXPECT_EQ(provider->calls.load(), 1);
    plugin.shutdown();
}
TEST(SystemInformationPluginTest, ReportsCollectionFailuresWithoutPublishingPartialState) {
    test::TestPluginHost host;
    SystemInformationTestsHelper::installEnglishTranslations(host);
    // clang-format off
    auto provider = std::make_shared<TestProvider>([](const std::atomic_bool&) { return Result<SystemSnapshot>::failure({"test_collection_failed", "Collection failed", "details"}); });
    // clang-format on
    SystemInformationPlugin plugin(provider);
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QSignalSpy snapshots(&plugin, &SystemInformationPlugin::snapshotChanged);
    ASSERT_TRUE(plugin.refresh().hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !plugin.isRefreshing(); }, 3000));
    // clang-format on
    EXPECT_FALSE(plugin.snapshot().has_value());
    EXPECT_EQ(snapshots.count(), 0);
    ASSERT_EQ(host.notifications.size(), 1);
    EXPECT_EQ(host.notifications.first().severity, AlertSeverity::Error);
    ASSERT_EQ(host.logs.size(), 1);
    EXPECT_EQ(host.logs.first().level, LogLevel::Error);
    EXPECT_EQ(host.logs.first().details.value(QStringLiteral("code")).toString(), QStringLiteral("test_collection_failed"));
    plugin.shutdown();
}
TEST(SystemInformationViewTest, RendersThemedHardwareSectionsAndRefreshState) {
    test::TestPluginHost host;
    SystemInformationTestsHelper::installEnglishTranslations(host);
    // clang-format off
    auto provider = std::make_shared<TestProvider>([](const std::atomic_bool&) { return Result<SystemSnapshot>::success(SystemInformationTestsHelper::sampleSnapshot()); });
    // clang-format on
    SystemInformationPlugin plugin(provider);
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("overview"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(1000, 760);
    view->show();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.snapshot().has_value(); }, 3000));
    // clang-format on
    QApplication::processEvents();
    auto* refresh = view->findChild<QPushButton*>(QStringLiteral("systemInformationRefresh"));
    ASSERT_NE(refresh, nullptr);
    EXPECT_TRUE(refresh->isEnabled());
    EXPECT_FALSE(refresh->icon().isNull());
    EXPECT_GE(view->findChildren<QFrame*>(QStringLiteral("systemInformationCard")).size(), 9);
    EXPECT_GE(view->findChildren<QProgressBar*>(QStringLiteral("systemInformationProgress")).size(), 3);
    bool foundProcessor = false;
    bool foundDisk = false;

    for (const auto* label : view->findChildren<QLabel*>(QStringLiteral("systemInformationValue"))) {
        foundProcessor = foundProcessor || label->text().contains(QStringLiteral("Test Processor"));
        foundDisk = foundDisk || label->text().contains(QStringLiteral("Test SSD"));
    }

    EXPECT_TRUE(foundProcessor);
    EXPECT_TRUE(foundDisk);
    auto* updated = view->findChild<QLabel*>(QStringLiteral("systemInformationUpdated"));
    ASSERT_NE(updated, nullptr);
    EXPECT_TRUE(updated->text().startsWith(QStringLiteral("Updated")));
    plugin.shutdown();
}
TEST(SystemInformationViewTest, PresentsCollectionFailureAndRestoresRefreshAction) {
    test::TestPluginHost host;
    SystemInformationTestsHelper::installEnglishTranslations(host);
    // clang-format off
    auto provider = std::make_shared<TestProvider>([](const std::atomic_bool&) { return Result<SystemSnapshot>::failure({"test_collection_failed", "Collection failed", {}}); });
    // clang-format on
    SystemInformationPlugin plugin(provider);
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("overview"), nullptr));
    ASSERT_NE(view, nullptr);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !plugin.isRefreshing(); }, 3000));
    // clang-format on
    auto* refresh = view->findChild<QPushButton*>(QStringLiteral("systemInformationRefresh"));
    ASSERT_NE(refresh, nullptr);
    EXPECT_TRUE(refresh->isEnabled());
    auto* state = view->findChild<QLabel*>(QStringLiteral("mutedLabel"));
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->text(), QStringLiteral("The hardware and operating system data could not be collected"));
    ASSERT_EQ(host.notifications.size(), 1);
    EXPECT_EQ(host.notifications.first().severity, AlertSeverity::Error);
    plugin.shutdown();
}

SystemSnapshot SystemInformationTestsHelper::sampleSnapshot() {
    SystemSnapshot snapshot;
    snapshot.capturedAtUtc = QDateTime::fromString(QStringLiteral("2026-08-15T12:30:00.000Z"), Qt::ISODateWithMs);
    snapshot.operatingSystem = {QStringLiteral("test-host"), QStringLiteral("Test OS"), QStringLiteral("1.0"), QStringLiteral("test-kernel"), 64, QStringLiteral("little-endian")};
    Processor processor;
    processor.id = 0;
    processor.vendor = QStringLiteral("Test Vendor");
    processor.model = QStringLiteral("Test Processor");
    processor.physicalCoreCount = 8;
    processor.logicalCoreCount = 16;
    processor.flags = {QStringLiteral("feature-a"), QStringLiteral("feature-b")};
    processor.cores = {{0, 32768, 32768, 1048576, 16777216, 4000000000ULL, true}};
    snapshot.processors = {processor};
    snapshot.processorUsage = {0.42, {0.25, 0.59}, {3200000000LL, 3400000000LL}};
    snapshot.memory = {34359738368ULL, 8589934592ULL, 17179869184ULL, {{0, QStringLiteral("Memory Vendor"), QStringLiteral("Physical Memory"), QStringLiteral("Module Model"), QStringLiteral("SERIAL"), 17179869184ULL, 3200000000ULL}}};
    snapshot.graphicsProcessors = {{0, QStringLiteral("Graphics Vendor"), QStringLiteral("Test GPU"), QStringLiteral("2.0"), QStringLiteral("1234"), QStringLiteral("5678"), 8589934592ULL, 4294967296ULL, 1500000000ULL, 2048}};
    snapshot.mainboard = {QStringLiteral("Board Vendor"), QStringLiteral("Board Model"), QStringLiteral("A1"), QStringLiteral("BOARD-SERIAL")};
    snapshot.disks = {{0, QStringLiteral("Disk Vendor"), QStringLiteral("Test SSD"), QStringLiteral("DISK-SERIAL"), QStringLiteral("NVME"), 1000000000000ULL, {{QStringLiteral("/"), 500000000000ULL}}}};
    snapshot.batteries = {{0, QStringLiteral("Battery Vendor"), QStringLiteral("Test Battery"), QStringLiteral("BATTERY-SERIAL"), QStringLiteral("Li-ion"), QStringLiteral("charging"), 100, 80, 80.0}};
    snapshot.networkInterfaces = {{QStringLiteral("en0"), QStringLiteral("Ethernet"), QStringLiteral("00:11:22:33:44:55"), QStringLiteral("192.0.2.1"), QStringLiteral("2001:db8::1")}};
    return snapshot;
}

void SystemInformationTestsHelper::installEnglishTranslations(test::TestPluginHost& host) {
    const auto values = translations::SystemInformationCatalog::catalog().value(QStringLiteral("en"));

    for (auto entry = values.constBegin(); entry != values.constEnd(); ++entry) {
        host.translations.insert(entry.key(), entry.value());
    }
}
} // namespace workpane::plugins::systeminformation

TEST(SystemInformationTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    workpane::plugins::systeminformation::SystemInformationPlugin plugin;
    workpane::test::TestCatalogs::expectCompleteCatalog(QStringLiteral("system-information"), plugin.translations());
}
