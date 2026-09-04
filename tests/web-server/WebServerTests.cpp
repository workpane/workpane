#include "RequestLogModel.h"
#include "StaticFileResolver.h"
#include "TestFuture.h"
#include "TestPluginHost.h"
#include "TestTranslations.h"
#include "WebServerInstance.h"
#include "WebServerPlugin.h"
#include "WebServerTranslations.h"
#include "WebServerView.h"
#include "persistence/StateStore.h"

#include <QApplication>
#include <QDialog>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPromise>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace workpane {

TEST(RequestLogModelTest, AssignsMonotonicSequencesPagesCapsAndClearsEntries) {
    plugins::webserver::RequestLogModel model(3);

    for (int index = 0; index < 5; ++index) {
        model.append({0, QDateTime::fromMSecsSinceEpoch(index, QTimeZone::UTC), QStringLiteral("GET"), QStringLiteral("/%1").arg(index), 200 + index, index, index * 10, QStringLiteral("127.0.0.1")});
    }

    auto first = model.entriesSince(0, 2);
    ASSERT_EQ(first.entries.size(), 2);
    EXPECT_EQ(first.entries.at(0).sequence, 3U);
    EXPECT_EQ(first.entries.at(1).sequence, 4U);
    EXPECT_EQ(first.cursor, 4U);
    auto second = model.entriesSince(first.cursor, 2);
    ASSERT_EQ(second.entries.size(), 1);
    EXPECT_EQ(second.entries.first().sequence, 5U);
    EXPECT_EQ(second.cursor, 5U);
    EXPECT_TRUE(model.entriesSince(second.cursor, 2).entries.isEmpty());

    model.clear();
    EXPECT_TRUE(model.entriesSince(0, 10).entries.isEmpty());
    model.append({});
    EXPECT_EQ(model.entriesSince(0, 10).entries.first().sequence, 6U);
}

TEST(RequestLogModelTest, SupportsConcurrentWritersAndReaders) {
    plugins::webserver::RequestLogModel model(500);
    std::atomic_bool reading{true};
    // A GoogleTest assertion is only thread safe where pthreads are, so the reader records what it saw and the assertion happens once it has joined.
    std::atomic_bool readPastTheBound{false};
    // clang-format off
    const auto readEntries = [&model, &reading, &readPastTheBound]() {
        while (reading.load(std::memory_order_acquire)) {
            const auto batch = model.entriesSince(0, 500);
            if (batch.entries.size() > 500) {
                readPastTheBound.store(true, std::memory_order_release);
            }
        }
    };
    // clang-format on
    std::thread reader(readEntries);

    for (int index = 0; index < 1000; ++index) {
        model.append({});
    }

    reading.store(false, std::memory_order_release);
    reader.join();
    EXPECT_FALSE(readPastTheBound.load(std::memory_order_acquire));
    const auto batch = model.entriesSince(0, 500);
    ASSERT_EQ(batch.entries.size(), 500);
    EXPECT_EQ(batch.entries.first().sequence, 501U);
    EXPECT_EQ(batch.entries.last().sequence, 1000U);
}

class WebServerTestsHelper final {
  public:
    static void writeFile(const QString& path, const QByteArray& contents);
    static QByteArray request(plugins::webserver::WebServerInstance& server, const QByteArray& contents);
    static QJsonObject terminalSnapshot(const QString& activeId, const QString& directory);
    static void configureSnapshotReply(test::TestPluginHost& host, QJsonObject snapshot);
    static void configureWebDatabase(test::TestPluginHost& host, persistence::DatabaseRows configurations = {}, int splitRatio = 420);
};

TEST(StaticFileResolverTest, ResolvesFilesDirectoriesEncodingAndMimeTypes) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("index.html")), QByteArrayLiteral("html"));
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("index.htm")), QByteArrayLiteral("htm"));
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("hello world.txt")), QByteArrayLiteral("text"));
    ASSERT_TRUE(QDir().mkpath(directory.filePath(QStringLiteral("nested"))));
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("nested/index.htm")), QByteArrayLiteral("nested"));

    plugins::webserver::StaticFileResolver resolver(directory.path());
    ASSERT_TRUE(resolver.valid());
    const auto root = resolver.resolve(QByteArrayLiteral("/"));
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(root->size, 4);
    EXPECT_EQ(root->canonicalPath, QFileInfo(directory.filePath(QStringLiteral("index.html"))).canonicalFilePath());
    EXPECT_EQ(root->mimeType, QByteArrayLiteral("text/html"));
    const auto encoded = resolver.resolve(QByteArrayLiteral("/hello%20world.txt"));
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(encoded->size, 4);
    EXPECT_TRUE(encoded->mimeType.startsWith(QByteArrayLiteral("text/plain")));
    const auto nested = resolver.resolve(QByteArrayLiteral("nested/"));
    ASSERT_TRUE(nested.has_value());
    EXPECT_EQ(nested->size, 6);
    EXPECT_FALSE(plugins::webserver::StaticFileResolver::mimeType(QStringLiteral("unknown.workpane-extension")).isEmpty());
}
TEST(StaticFileResolverTest, RejectsInvalidRootsTraversalLinksAndUnreadableTargets) {
    plugins::webserver::StaticFileResolver missing(QStringLiteral("/missing/workpane/root"));
    EXPECT_FALSE(missing.valid());
    EXPECT_FALSE(missing.resolve(QByteArrayLiteral("/")).has_value());

    QTemporaryDir root;
    QTemporaryDir outside;
    ASSERT_TRUE(root.isValid());
    ASSERT_TRUE(outside.isValid());
    WebServerTestsHelper::writeFile(outside.filePath(QStringLiteral("secret.txt")), QByteArrayLiteral("secret"));
    ASSERT_TRUE(QFile::link(outside.filePath(QStringLiteral("secret.txt")), root.filePath(QStringLiteral("link.txt"))));
    ASSERT_TRUE(QDir().mkpath(root.filePath(QStringLiteral("empty"))));
    plugins::webserver::StaticFileResolver resolver(root.path());
    ASSERT_TRUE(resolver.valid());

    QList<QByteArray> rejected{QByteArray("/bad\0path", 9), QByteArrayLiteral("/%00"), QByteArrayLiteral("/..%2Fsecret.txt"), QByteArrayLiteral("/../secret.txt"), QByteArrayLiteral("/folder\\file"), QByteArrayLiteral("/missing"), QByteArrayLiteral("/empty/"), QByteArray(8193, 'a')};

    if (QFileInfo(root.filePath(QStringLiteral("link.txt"))).isSymLink()) {
        rejected.append(QByteArrayLiteral("/link.txt"));
    }

    for (const auto& path : rejected) {
        EXPECT_FALSE(resolver.resolve(path).has_value()) << path.toStdString();
    }
}
TEST(WebServerInstanceTest, ServesFilesAndReturnsProtocolErrorsWithRequestLogs) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("index.html")), QByteArrayLiteral("content"));
    plugins::webserver::WebServerInstance server;
    EXPECT_FALSE(server.start(QStringLiteral("/missing"), QStringLiteral("127.0.0.1"), 0));
    EXPECT_FALSE(server.start(directory.path(), QStringLiteral("invalid"), 0));
    ASSERT_TRUE(server.start(directory.path(), QStringLiteral("127.0.0.1"), 0));
    ASSERT_TRUE(server.running());
    ASSERT_GT(server.port(), 0);

    const QByteArray success = WebServerTestsHelper::request(server, QByteArrayLiteral("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    EXPECT_TRUE(success.startsWith(QByteArrayLiteral("HTTP/1.1 200 OK")));
    EXPECT_TRUE(success.endsWith(QByteArrayLiteral("content")));
    const QByteArray missing = WebServerTestsHelper::request(server, QByteArrayLiteral("GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    EXPECT_TRUE(missing.startsWith(QByteArrayLiteral("HTTP/1.1 404 Not Found")));
    const QByteArray invalid = WebServerTestsHelper::request(server, QByteArrayLiteral("POST / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    EXPECT_TRUE(invalid.startsWith(QByteArrayLiteral("HTTP/1.1 400 Bad Request")));
    const QByteArray oversized = WebServerTestsHelper::request(server, QByteArray(33 * 1024, 'x'));
    EXPECT_TRUE(oversized.startsWith(QByteArrayLiteral("HTTP/1.1 400 Bad Request")));

    const auto logs = server.requestLog().entriesSince(0, 10);
    ASSERT_EQ(logs.entries.size(), 4);
    EXPECT_EQ(logs.entries.at(0).status, 200);
    EXPECT_EQ(logs.entries.at(1).status, 404);
    EXPECT_EQ(logs.entries.at(2).status, 400);
    EXPECT_EQ(logs.entries.at(0).method, QStringLiteral("GET"));
    EXPECT_EQ(logs.entries.at(2).method, QStringLiteral("POST"));
    EXPECT_TRUE(logs.entries.at(0).timestamp.timeSpec() == Qt::UTC || logs.entries.at(0).timestamp.offsetFromUtc() == 0);

    // Every entry names who asked, because a log that identifies the caller of one request and not of the next explains nothing.
    for (const auto& entry : logs.entries) {
        EXPECT_EQ(entry.remoteAddress, QStringLiteral("127.0.0.1")) << entry.status;
    }
    server.clearRequestLog();
    EXPECT_TRUE(server.requestLog().entriesSince(0, 10).entries.isEmpty());

    server.stop();
    EXPECT_FALSE(server.running());
    EXPECT_EQ(server.port(), 0);
    server.stop();
}
// A file larger than one transfer chunk is written across several of them, which is what any real asset of a page is.
TEST(WebServerInstanceTest, ServesAFileThatSpansSeveralTransferChunks) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    QByteArray content;
    content.reserve(200000);

    for (int index = 0; content.size() < 200000; ++index) {
        content.append(QByteArray::number(index)).append('\n');
    }

    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("asset.bin")), content);
    plugins::webserver::WebServerInstance server;
    ASSERT_TRUE(server.start(directory.path(), QStringLiteral("127.0.0.1"), 0));

    const QByteArray answer = WebServerTestsHelper::request(server, QByteArrayLiteral("GET /asset.bin HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    ASSERT_TRUE(answer.startsWith(QByteArrayLiteral("HTTP/1.1 200 OK")));
    EXPECT_TRUE(answer.contains(QByteArrayLiteral("Content-Length: ") + QByteArray::number(content.size()) + QByteArrayLiteral("\r\n")));

    const qsizetype separator = answer.indexOf(QByteArrayLiteral("\r\n\r\n"));
    ASSERT_GT(separator, 0);

    // What the reader receives is the file itself, neither cut short by the last chunk nor carrying one twice.
    const QByteArray body = answer.mid(separator + 4);
    EXPECT_EQ(body.size(), content.size());
    EXPECT_EQ(body, content);

    server.stop();
}

TEST(WebServerInstanceTest, RefusesABurstLargerThanAnyRequestAndKeepsServing) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("index.html")), QByteArrayLiteral("content"));
    plugins::webserver::WebServerInstance server;
    ASSERT_TRUE(server.start(directory.path(), QStringLiteral("127.0.0.1"), 0));

    WebServerTestsHelper::request(server, QByteArray(4 * 1024 * 1024, 'x'));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !server.requestLog().entriesSince(0, 10).entries.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(server.requestLog().entriesSince(0, 10).entries.first().status, 400);

    const QByteArray served = WebServerTestsHelper::request(server, QByteArrayLiteral("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    EXPECT_TRUE(served.startsWith(QByteArrayLiteral("HTTP/1.1 200 OK")));
    EXPECT_TRUE(server.running());
}

// A client that opens a connection and never finishes its request holds it until the deadline the server declares.
TEST(WebServerInstanceTest, ClosesAConnectionThatNeverFinishesItsRequest) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("index.html")), QByteArrayLiteral("content"));
    plugins::webserver::WebServerInstance server;
    ASSERT_TRUE(server.start(directory.path(), QStringLiteral("127.0.0.1"), 0));

    QTcpSocket stalled;
    stalled.connectToHost(QHostAddress::LocalHost, server.port());
    ASSERT_TRUE(stalled.waitForConnected(2000));

    // The request opens and never ends, which is what a client holding a connection open looks like.
    stalled.write(QByteArrayLiteral("GET / HTTP/1.1\r\nHost: localhost\r\n"));
    ASSERT_TRUE(stalled.waitForBytesWritten(2000));

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&stalled]() { return stalled.state() != QAbstractSocket::ConnectedState; }, 20000)) << "the server held the connection past its deadline";
    // clang-format on

    // The server kept serving, so expiring one connection cost nothing to the next.
    const QByteArray answered = WebServerTestsHelper::request(server, QByteArrayLiteral("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    EXPECT_TRUE(answered.startsWith(QByteArrayLiteral("HTTP/1.1 200 OK"))) << answered.toStdString();
    EXPECT_TRUE(server.running());
    server.stop();
}

TEST(WebServerInstanceTest, KeepsServingThroughManyConnectionsIncludingOnesThatLeaveEarly) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("index.html")), QByteArray(64 * 1024, 'a'));
    plugins::webserver::WebServerInstance server;
    ASSERT_TRUE(server.start(directory.path(), QStringLiteral("127.0.0.1"), 0));

    int served = 0;

    for (int round = 0; round < 60; ++round) {
        // A client that opens a connection and leaves without speaking must not keep the server from answering the next one.
        QTcpSocket abandoned;
        abandoned.connectToHost(QHostAddress::LocalHost, server.port());
        ASSERT_TRUE(abandoned.waitForConnected(2000));
        abandoned.abort();

        const QByteArray answered = WebServerTestsHelper::request(server, QByteArrayLiteral("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
        if (answered.startsWith(QByteArrayLiteral("HTTP/1.1 200 OK"))) {
            ++served;
        }
    }

    EXPECT_EQ(served, 60);
    EXPECT_TRUE(server.running());
    // The log is bounded, so a long run of requests never grows it past the page a reader asks for.
    EXPECT_LE(server.requestLog().entriesSince(0, 1000).entries.size(), 1000);
    server.stop();
}

TEST(WebServerInstanceTest, AnswersEveryHostileRequestAndKeepsServing) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("index.html")), QByteArrayLiteral("content"));
    plugins::webserver::WebServerInstance server;
    ASSERT_TRUE(server.start(directory.path(), QStringLiteral("127.0.0.1"), 0));

    const QList<QByteArray> hostile{
        QByteArray{}, QByteArrayLiteral("\r\n\r\n"), QByteArrayLiteral("GET\r\n\r\n"), QByteArrayLiteral("GET /\r\n\r\n"), QByteArrayLiteral("GET / HTTP/9.9\r\n\r\n"), QByteArrayLiteral("get / http/1.1\r\n\r\n"), QByteArrayLiteral("GET ../../etc/passwd HTTP/1.1\r\n\r\n"), QByteArrayLiteral("GET /../../etc/passwd HTTP/1.1\r\n\r\n"), QByteArrayLiteral("GET /%2e%2e%2f HTTP/1.1\r\n\r\n"), QByteArrayLiteral("GET / HTTP/1.1\r\nHost:\r\n\r\n"), QByteArray("GET /\0x HTTP/1.1\r\n\r\n", 20), QByteArray("\0\0\0\0\r\n\r\n", 8), QByteArray::fromHex("c328c328c328") + QByteArrayLiteral("\r\n\r\n"), QByteArrayLiteral("GET ") + QByteArray(8192, 'a') + QByteArrayLiteral(" HTTP/1.1\r\n\r\n"), QByteArrayLiteral("GET / HTTP/1.1\r\n") + QByteArray(8192, 'h') + QByteArrayLiteral("\r\n\r\n"),
    };

    for (const auto& request : hostile) {
        WebServerTestsHelper::request(server, request);
        ASSERT_TRUE(server.running()) << request.left(40).toStdString();
    }

    // Pseudo-random requests are seeded, so a failure here reproduces exactly.
    quint32 seed = 0xfeed4321;
    for (int round = 0; round < 60; ++round) {
        QByteArray request;
        const int length = static_cast<int>(seed % 200) + 1;
        for (int index = 0; index < length; ++index) {
            seed = seed * 1664525U + 1013904223U;
            request.append(static_cast<char>((seed >> 16) & 0xFF));
        }
        request.append(QByteArrayLiteral("\r\n\r\n"));
        WebServerTestsHelper::request(server, request);
        ASSERT_TRUE(server.running());
    }

    const QByteArray served = WebServerTestsHelper::request(server, QByteArrayLiteral("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    EXPECT_TRUE(served.startsWith(QByteArrayLiteral("HTTP/1.1 200 OK")));
    server.stop();
}

TEST(WebServerInstanceTest, RestartsOnAnotherPortAndRejectsOccupiedPorts) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("index.html")), QByteArrayLiteral("content"));
    QTcpServer occupied;
    ASSERT_TRUE(occupied.listen(QHostAddress::LocalHost, 0));

    plugins::webserver::WebServerInstance server;
    EXPECT_FALSE(server.start(directory.path(), QStringLiteral("127.0.0.1"), occupied.serverPort()));
    EXPECT_FALSE(server.running());
    ASSERT_TRUE(server.start(directory.path(), QStringLiteral("127.0.0.1"), 0));
    const quint16 firstPort = server.port();
    ASSERT_TRUE(server.start(directory.path(), QStringLiteral("127.0.0.1"), 0));
    EXPECT_GT(server.port(), 0);
    EXPECT_TRUE(server.running());
    EXPECT_NE(firstPort, occupied.serverPort());
}
TEST(WebServerPluginTest, DeclaresMetadataRestoresStateAndSynchronizesTerminals) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    host.dataPath = directory.path();
    WebServerTestsHelper::configureWebDatabase(host, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Project Preview")}, {QStringLiteral("root"), directory.path()}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 8080}, {QStringLiteral("terminal_id"), QStringLiteral("terminal-1")}}}, 500);
    WebServerTestsHelper::configureSnapshotReply(host, WebServerTestsHelper::terminalSnapshot(QStringLiteral("terminal-1"), directory.path()));

    plugins::webserver::WebServerPlugin plugin;
    EXPECT_EQ(plugin.id(), QStringLiteral("web-server"));
    EXPECT_EQ(plugin.titleKey(), QStringLiteral("web-server.plugin.title"));
    EXPECT_TRUE(plugin.dependencies().isEmpty());
    EXPECT_TRUE(plugin.translations().contains(QStringLiteral("en")));
    EXPECT_FALSE(plugin.styleSheet(host.theme()).isEmpty());
    ASSERT_EQ(plugin.navigationItems(host.theme()).size(), 1);
    EXPECT_FALSE(plugin.navigationItems(host.theme()).first().icon.isNull());
    ASSERT_EQ(plugin.settingsGroups().size(), 1);
    EXPECT_EQ(plugin.settingsGroups().first().id, QStringLiteral("web-server"));
    EXPECT_EQ(plugin.settingsGroups().first().sections.first().id, QStringLiteral("general"));

    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QApplication::processEvents();
    ASSERT_EQ(host.appliedMigrations.size(), 1);
    EXPECT_TRUE(host.appliedMigrations.first().statements.first().contains(QStringLiteral("id TEXT PRIMARY KEY")));
    EXPECT_TRUE(host.appliedMigrations.first().statements.first().contains(QStringLiteral("name TEXT NOT NULL")));
    EXPECT_EQ(plugin.initialize(host).error().code, QStringLiteral("web_server_already_initialized"));
    EXPECT_EQ(plugin.activeTerminalId(), QStringLiteral("terminal-1"));
    EXPECT_EQ(plugin.terminalData(QStringLiteral("terminal-1")).value(QStringLiteral("name")), QStringLiteral("Shell"));
    EXPECT_EQ(plugin.splitRatio(), 500);
    EXPECT_TRUE(plugin.webServerConfigured(QStringLiteral("server-1")));
    EXPECT_EQ(plugin.webServerName(QStringLiteral("server-1")), QStringLiteral("Project Preview"));
    EXPECT_EQ(plugin.webServerRoot(QStringLiteral("server-1")), directory.path());
    EXPECT_EQ(plugin.webServerHost(QStringLiteral("server-1")), QStringLiteral("127.0.0.1"));
    EXPECT_EQ(plugin.webServerPort(QStringLiteral("server-1")), 8080);
    EXPECT_EQ(plugin.webServerTerminalId(QStringLiteral("server-1")), QStringLiteral("terminal-1"));
    EXPECT_EQ(plugin.serverIdForTerminal(QStringLiteral("terminal-1")), QStringLiteral("server-1"));
    EXPECT_FALSE(plugin.webServerRunning(QStringLiteral("server-1")));
    EXPECT_EQ(plugin.configuredWebServers().size(), 1);
    EXPECT_EQ(host.capabilityInvocations.size(), 1);

    std::unique_ptr<QWidget> navigation(plugin.createNavigationView(QStringLiteral("manager"), nullptr));
    std::unique_ptr<QWidget> settings(plugin.createSettingsSection(QStringLiteral("web-server"), QStringLiteral("general"), nullptr));
    EXPECT_NE(navigation, nullptr);
    EXPECT_NE(settings, nullptr);
    EXPECT_EQ(plugin.createNavigationView(QStringLiteral("unknown"), nullptr), nullptr);
    EXPECT_EQ(plugin.createSettingsSection(QStringLiteral("web-server"), QStringLiteral("unknown"), nullptr), nullptr);
    plugin.shutdown();
}
TEST(WebServerPluginTest, OperatesWithoutTerminalAndRemovesIndependentConfigurationsTransactionally) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Independent Preview")}, {QStringLiteral("root"), directory.path()}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 8080}, {QStringLiteral("terminal_id"), QVariant{}}}});

    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QApplication::processEvents();
    EXPECT_TRUE(host.capabilityInvocations.isEmpty());
    EXPECT_TRUE(plugin.activeTerminalId().isEmpty());
    EXPECT_EQ(plugin.webServerName(QStringLiteral("server-1")), QStringLiteral("Independent Preview"));
    EXPECT_TRUE(plugin.webServerTerminalId(QStringLiteral("server-1")).isEmpty());

    std::unique_ptr<QWidget> navigation(plugin.createNavigationView(QStringLiteral("manager"), nullptr));
    ASSERT_NE(navigation, nullptr);

    // A row that can be edited opens its editor on a double click, which is the same editor its own action opens.
    auto* table = navigation->findChild<QTableWidget*>(QStringLiteral("webServerTable"));
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);
    const QPointer<QWidget> rowActions = table->cellWidget(0, 4);
    ASSERT_FALSE(rowActions.isNull());

    // The editor is opened without taking the event loop, so the double click returns with the form already up.
    table->selectRow(0);
    emit table->doubleClicked(table->model()->index(0, 0));
    auto* editor = navigation->findChild<QDialog*>(QStringLiteral("webServerDialog"));
    ASSERT_NE(editor, nullptr);
    EXPECT_FALSE(rowActions.isNull());

    // Rebuilding the rows while that form is up is safe, because the click that opened it has already returned.
    QMetaObject::invokeMethod(navigation.get(), "refreshInstances", Qt::DirectConnection, Q_ARG(QString, QString{}));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(table->rowCount(), 1);
    editor->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(navigation->findChild<QDialog*>(QStringLiteral("webServerDialog")), nullptr);

    QPushButton* createButton = nullptr;
    QPushButton* terminalButton = nullptr;

    for (auto* button : navigation->findChildren<QPushButton*>()) {
        if (button->text() == host.translate(QStringLiteral("web-server.manager.new-server"))) {
            createButton = button;
        }
        if (button->text() == host.translate(QStringLiteral("web-server.manager.from-terminal"))) {
            terminalButton = button;
        }
    }

    ASSERT_NE(createButton, nullptr);
    ASSERT_NE(terminalButton, nullptr);
    EXPECT_FALSE(createButton->isHidden());
    EXPECT_TRUE(terminalButton->isHidden());
    const QImage createIcon = createButton->icon().pixmap(32, 32).toImage();
    bool foundOpaquePixel = false;

    for (int y = 0; y < createIcon.height(); ++y) {
        for (int x = 0; x < createIcon.width(); ++x) {
            const QColor color = createIcon.pixelColor(x, y);
            if (color.alpha() < 200) {
                continue;
            }
            foundOpaquePixel = true;
            EXPECT_EQ(color.red(), 255);
            EXPECT_EQ(color.green(), 255);
            EXPECT_EQ(color.blue(), 255);
        }
    }

    EXPECT_TRUE(foundOpaquePixel);

    host.executeError = Error{QStringLiteral("write_failed"), QStringLiteral("Write failed"), {}};
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.removeWebServer(QStringLiteral("server-1"))).error().code, QStringLiteral("write_failed"));
    EXPECT_TRUE(plugin.webServerConfigured(QStringLiteral("server-1")));
    host.executeError.reset();
    EXPECT_TRUE(test::TestFutures::awaitFuture(plugin.removeWebServer(QStringLiteral("server-1"))).hasValue());
    EXPECT_FALSE(plugin.webServerConfigured(QStringLiteral("server-1")));
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.removeWebServer(QStringLiteral("server-1"))).error().code, QStringLiteral("web_server_configuration_missing"));

    // A reader who confirmed a removal is told why it did not happen rather than watching the row stay where it was.
    ASSERT_FALSE(host.notifications.isEmpty());
    EXPECT_EQ(host.notifications.last().message, plugins::webserver::translations::WebServerCatalog::english().value(QStringLiteral("web-server.error.configuration-unavailable")));
    EXPECT_EQ(host.notifications.last().severity, plugins::AlertSeverity::Error);
}
TEST(WebServerPluginTest, RejectsInvalidPersistentStateAndStorageFailures) {
    test::TestPluginHost migrationFailureHost;
    migrationFailureHost.migrationError.emplace(QStringLiteral("migration_failed"), QStringLiteral("Migration failed"), QString{});
    plugins::webserver::WebServerPlugin migrationFailure;
    EXPECT_EQ(migrationFailure.initialize(migrationFailureHost).error().code, QStringLiteral("migration_failed"));

    test::TestPluginHost readFailureHost;
    readFailureHost.queryError = Error{QStringLiteral("read_failed"), QStringLiteral("Read failed"), {}};
    plugins::webserver::WebServerPlugin readFailure;
    EXPECT_EQ(readFailure.initialize(readFailureHost).error().code, QStringLiteral("read_failed"));

    const QList<persistence::DatabaseRows> invalidConfigurations{{{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), QStringLiteral("/tmp")}, {QStringLiteral("bind_host"), QStringLiteral("invalid")}, {QStringLiteral("port"), 8080}}}, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), QStringLiteral("/tmp")}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 0}}}, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QString{}}, {QStringLiteral("root"), QStringLiteral("/tmp")}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 8080}}}, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral(" Preview ")}, {QStringLiteral("root"), QStringLiteral("/tmp")}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 8080}}}, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), QStringLiteral("relative")}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 8080}}}, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), QStringLiteral("/tmp")}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 8080.5}}}};

    for (const auto& configurations : invalidConfigurations) {
        test::TestPluginHost host;
        WebServerTestsHelper::configureWebDatabase(host, configurations);
        plugins::webserver::WebServerPlugin plugin;
        EXPECT_FALSE(plugin.initialize(host).hasValue());
    }

    // A ratio outside its range and a ratio that is not whole are the declared default, and the plugin still opens.
    test::TestPluginHost invalidSettingsHost;
    WebServerTestsHelper::configureWebDatabase(invalidSettingsHost, {}, 149);
    plugins::webserver::WebServerPlugin invalidSettings;
    ASSERT_TRUE(invalidSettings.initialize(invalidSettingsHost).hasValue());
    EXPECT_EQ(invalidSettings.splitRatio(), 420);
    EXPECT_TRUE(invalidSettingsHost.notifications.isEmpty());
    invalidSettings.shutdown();

    test::TestPluginHost fractionalSettingsHost;
    WebServerTestsHelper::configureWebDatabase(fractionalSettingsHost);
    fractionalSettingsHost.settingsDocument = {{QStringLiteral("splitRatio"), 420.5}};
    plugins::webserver::WebServerPlugin fractionalSettings;
    ASSERT_TRUE(fractionalSettings.initialize(fractionalSettingsHost).hasValue());
    EXPECT_EQ(fractionalSettings.splitRatio(), 420);
    fractionalSettings.shutdown();

    // A key nobody declares changes nothing, so a document written by another version still opens.
    test::TestPluginHost unknownSettingsHost;
    WebServerTestsHelper::configureWebDatabase(unknownSettingsHost);
    unknownSettingsHost.settingsDocument = {{QStringLiteral("unknown"), 1}};
    plugins::webserver::WebServerPlugin unknownSettings;
    ASSERT_TRUE(unknownSettings.initialize(unknownSettingsHost).hasValue());
    EXPECT_EQ(unknownSettings.splitRatio(), 420);
    unknownSettings.shutdown();
}
// A double reproduces the statement and not the semantics that answer it, so a configuration is written to a real database and read from it.
TEST(WebServerPluginTest, KeepsItsConfigurationsThroughARealDatabase) {
    QTemporaryDir root;
    QTemporaryDir data;
    ASSERT_TRUE(root.isValid());
    ASSERT_TRUE(data.isValid());
    WebServerTestsHelper::writeFile(root.filePath(QStringLiteral("index.html")), QByteArrayLiteral("content"));
    persistence::StateStore store(data.filePath(QStringLiteral("workpane.sqlite3")));
    ASSERT_TRUE(store.initialize().hasValue());

    const QString served = QFileInfo(root.path()).canonicalFilePath();
    QTcpServer portProbe;
    ASSERT_TRUE(portProbe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = portProbe.serverPort();
    portProbe.close();
    {
        test::TestPluginHost host;
        host.translations = plugins::webserver::translations::WebServerCatalog::english();
        host.useDatabase(store, QStringLiteral("web-server"));
        plugins::webserver::WebServerPlugin plugin;
        ASSERT_TRUE(plugin.initialize(host).hasValue());
        ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.configureAndStartWebServer(QStringLiteral("server-1"), QStringLiteral("Preview"), served, QStringLiteral("127.0.0.1"), port)).hasValue());
        EXPECT_EQ(plugin.webServerPort(QStringLiteral("server-1")), port);
        plugin.shutdown();
    }

    // A second start reads what the first one wrote, which is what a restart really does.
    test::TestPluginHost host;
    host.translations = plugins::webserver::translations::WebServerCatalog::english();
    host.useDatabase(store, QStringLiteral("web-server"));
    plugins::webserver::WebServerPlugin reopened;
    ASSERT_TRUE(reopened.initialize(host).hasValue());

    EXPECT_TRUE(reopened.webServerConfigured(QStringLiteral("server-1")));
    EXPECT_EQ(reopened.webServerName(QStringLiteral("server-1")), QStringLiteral("Preview"));
    EXPECT_EQ(reopened.webServerRoot(QStringLiteral("server-1")), served);
    EXPECT_EQ(reopened.webServerHost(QStringLiteral("server-1")), QStringLiteral("127.0.0.1"));
    EXPECT_EQ(reopened.webServerPort(QStringLiteral("server-1")), port);

    // A configuration is stopped when the product opens, because nothing runs until the reader asks for it.
    EXPECT_FALSE(reopened.webServerRunning(QStringLiteral("server-1")));
    reopened.shutdown();
}

TEST(WebServerPluginTest, ValidatesConfigurationRunsServerAndPreservesStateOnFailures) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    WebServerTestsHelper::writeFile(directory.filePath(QStringLiteral("index.html")), QByteArrayLiteral("content"));
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host);
    WebServerTestsHelper::configureSnapshotReply(host, WebServerTestsHelper::terminalSnapshot(QStringLiteral("terminal-1"), directory.path()));
    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QApplication::processEvents();

    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.configureAndStartWebServer(QStringLiteral("server-1"), {}, directory.path(), QStringLiteral("127.0.0.1"), 8080)).error().code, QStringLiteral("web_server_start_failed"));
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.configureAndStartWebServer(QStringLiteral("server-1"), QStringLiteral("Preview"), directory.path(), QStringLiteral("127.0.0.1"), 0)).error().code, QStringLiteral("web_server_start_failed"));
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.configureAndStartWebServer(QStringLiteral("server-1"), QStringLiteral("Preview"), QStringLiteral("/missing"), QStringLiteral("127.0.0.1"), 8080)).error().code, QStringLiteral("web_server_start_failed"));
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.configureAndStartWebServer(QStringLiteral("server-1"), QStringLiteral("Preview"), directory.path(), QStringLiteral("invalid"), 8080)).error().code, QStringLiteral("web_server_start_failed"));
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.startWebServer(QStringLiteral("server-1"))).error().code, QStringLiteral("web_server_start_failed"));

    host.executeError = Error{QStringLiteral("write_failed"), QStringLiteral("Write failed"), {}};
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.configureAndStartWebServer(QStringLiteral("server-1"), QStringLiteral("Preview"), directory.path(), QStringLiteral("127.0.0.1"), 8080)).error().code, QStringLiteral("write_failed"));
    EXPECT_FALSE(plugin.webServerConfigured(QStringLiteral("server-1")));
    host.executeError.reset();

    QTcpServer portProbe;
    ASSERT_TRUE(portProbe.listen(QHostAddress::LocalHost, 0));
    const quint16 availablePort = portProbe.serverPort();
    portProbe.close();
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.configureAndStartWebServer(QStringLiteral("server-1"), QStringLiteral("Preview"), directory.path(), QStringLiteral("127.0.0.1"), availablePort)).hasValue());
    ASSERT_TRUE(plugin.webServerRunning(QStringLiteral("server-1")));
    EXPECT_TRUE(test::TestFutures::awaitFuture(plugin.startWebServer(QStringLiteral("server-1"))).hasValue());
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.configureAndStartWebServer(QStringLiteral("server-1"), QStringLiteral("Preview"), directory.path(), QStringLiteral("127.0.0.1"), availablePort)).error().code, QStringLiteral("web_server_start_failed"));

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, plugin.webServerPort(QStringLiteral("server-1")));
    ASSERT_TRUE(socket.waitForConnected(2000));
    socket.write("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    socket.flush();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.requestLogEntriesSince(QStringLiteral("server-1"), 0, 10).entries.size() == 1; }));
    // clang-format on
    EXPECT_TRUE(test::TestFutures::awaitFuture(plugin.clearRequestLog(QStringLiteral("server-1"))));
    EXPECT_TRUE(plugin.requestLogEntriesSince(QStringLiteral("server-1"), 0, 10).entries.isEmpty());
    EXPECT_FALSE(test::TestFutures::awaitFuture(plugin.clearRequestLog(QStringLiteral("missing"))));

    plugin.stopWebServer(QStringLiteral("server-1"));
    EXPECT_FALSE(plugin.webServerRunning(QStringLiteral("server-1")));
    plugin.stopWebServer(QStringLiteral("server-1"));
    plugin.shutdown();
}
TEST(WebServerPluginTest, SerializesStartsAndCancelsThePendingRuntimeOperation) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host);
    auto persistence = std::make_shared<QPromise<Result<void>>>();
    persistence->start();
    bool persistenceDeferred = false;
    // clang-format off
    host.executeFutureHandler = [persistence, &persistenceDeferred](const QString& statement, const QVariantList&) {
        if (statement.startsWith(QStringLiteral("INSERT INTO web_server_configurations"))) {
            persistenceDeferred = true;
            return persistence->future();
        }
        return QtFuture::makeReadyValueFuture(Result<void>::success());
    };
    // clang-format on
    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QTcpServer portProbe;
    ASSERT_TRUE(portProbe.listen(QHostAddress::LocalHost, 0));
    const quint16 availablePort = portProbe.serverPort();
    portProbe.close();
    auto firstStart = plugin.configureAndStartWebServer(QStringLiteral("server-1"), QStringLiteral("Preview"), directory.path(), QStringLiteral("127.0.0.1"), availablePort);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return persistenceDeferred; }));
    // clang-format on
    const auto duplicate = test::TestFutures::awaitFuture(plugin.configureAndStartWebServer(QStringLiteral("server-1"), QStringLiteral("Preview"), directory.path(), QStringLiteral("127.0.0.1"), availablePort));
    ASSERT_FALSE(duplicate.hasValue());
    EXPECT_EQ(duplicate.error().code, QStringLiteral("web_server_start_failed"));
    plugin.stopWebServer(QStringLiteral("server-1"));
    persistence->addResult(Result<void>::success());
    persistence->finish();
    const auto cancelled = test::TestFutures::awaitFuture(firstStart);
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.error().code, QStringLiteral("web_server_start_cancelled"));
    EXPECT_FALSE(plugin.webServerRunning(QStringLiteral("server-1")));
}
// A terminal that is no longer there takes its link and nothing else, because a server is configured independently of the terminal it was created from.
TEST(WebServerPluginTest, DropsALinkWhoseTerminalIsGoneAndKeepsTheServerItBelongedTo) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), directory.path()}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 8080}, {QStringLiteral("terminal_id"), QStringLiteral("terminal-1")}}});

    // The workspace the product comes back to no longer holds the terminal that server was created from.
    const QJsonObject withoutIt{{QStringLiteral("activeTerminalId"), QStringLiteral("terminal-2")}, {QStringLiteral("terminals"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("terminal-2")}, {QStringLiteral("name"), QStringLiteral("Another")}, {QStringLiteral("cwd"), directory.path()}}}}};
    WebServerTestsHelper::configureSnapshotReply(host, withoutIt);
    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(plugin.webServerConfigured(QStringLiteral("server-1")));
    EXPECT_EQ(plugin.webServerTerminalId(QStringLiteral("server-1")), QStringLiteral("terminal-1"));
    QApplication::processEvents();

    // The link is gone and the server is not, with its name, its root and its port exactly as they were.
    EXPECT_TRUE(plugin.webServerConfigured(QStringLiteral("server-1")));
    EXPECT_TRUE(plugin.webServerTerminalId(QStringLiteral("server-1")).isEmpty());
    EXPECT_EQ(plugin.webServerName(QStringLiteral("server-1")), QStringLiteral("Preview"));
    EXPECT_EQ(plugin.webServerPort(QStringLiteral("server-1")), 8080);

    // What is written back keeps the configuration rather than removing it.
    ASSERT_FALSE(host.databaseExecutions.isEmpty());
    EXPECT_TRUE(host.databaseExecutions.last().value(QStringLiteral("statement")).toString().startsWith(QStringLiteral("INSERT INTO web_server_configurations")));
}

TEST(WebServerPluginTest, ValidatesEventsSnapshotsRemovalAndSettingsPersistence) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), directory.path()}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 8080}, {QStringLiteral("terminal_id"), QStringLiteral("terminal-1")}}});
    WebServerTestsHelper::configureSnapshotReply(host, WebServerTestsHelper::terminalSnapshot(QStringLiteral("terminal-1"), directory.path()));
    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QApplication::processEvents();
    host.savedSettings.clear();

    plugin.setSplitRatio(600);
    EXPECT_EQ(plugin.splitRatio(), 600);
    ASSERT_EQ(host.savedSettings.size(), 1);
    EXPECT_EQ(host.savedSettings.first().value(QStringLiteral("splitRatio")).toInt(), 600);
    // clang-format off
    host.settingsFutureHandler = [](const QJsonObject&) { return QtFuture::makeReadyValueFuture(Result<void>::failure({"write_failed", "Write failed", {}})); };
    // clang-format on
    plugin.setSplitRatio(700);
    EXPECT_EQ(plugin.splitRatio(), 600);
    ASSERT_FALSE(host.notifications.isEmpty());
    host.settingsFutureHandler = nullptr;

    const qsizetype notificationCount = host.notifications.size();
    plugin.handleEvent(QStringLiteral("other"), QStringLiteral("terminal.closed"), {});
    EXPECT_EQ(host.notifications.size(), notificationCount);
    plugin.handleEvent(QStringLiteral("terminal"), QStringLiteral("terminal.workspace.changed"), {{QStringLiteral("unexpected"), true}});
    plugin.handleEvent(QStringLiteral("terminal"), QStringLiteral("terminal.closed"), {});
    plugin.handleEvent(QStringLiteral("terminal"), QStringLiteral("terminal.closed"), {{QStringLiteral("terminalId"), QString{}}});
    EXPECT_EQ(host.notifications.size(), notificationCount + 3);

    plugin.handleEvent(QStringLiteral("terminal"), QStringLiteral("terminal.closed"), {{QStringLiteral("terminalId"), QStringLiteral("terminal-1")}});
    ASSERT_FALSE(host.databaseExecutions.isEmpty());
    EXPECT_TRUE(host.databaseExecutions.last().value(QStringLiteral("statement")).toString().startsWith(QStringLiteral("INSERT INTO web_server_configurations")));
    EXPECT_TRUE(plugin.terminalData(QStringLiteral("terminal-1")).isEmpty());
    EXPECT_TRUE(plugin.webServerConfigured(QStringLiteral("server-1")));
    EXPECT_TRUE(plugin.webServerTerminalId(QStringLiteral("server-1")).isEmpty());

    std::optional<Result<QJsonObject>> response;
    // clang-format off
    plugin.handleRequest(QStringLiteral("terminal"), QStringLiteral("unknown"), {}, [&response](Result<QJsonObject> result) { response = std::move(result); });
    // clang-format on
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->error().code, QStringLiteral("plugin_message_topic_unknown"));
}
TEST(WebServerPluginTest, CancelsPendingPersistenceCallbacksDuringShutdown) {
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host);
    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    auto operation = std::make_shared<QPromise<Result<void>>>();
    operation->start();
    // clang-format off
    host.executeFutureHandler = [operation](const QString&, const QVariantList&) { return operation->future(); };
    // clang-format on

    plugin.setSplitRatio(600);
    plugin.shutdown();
    operation->addResult(Result<void>::failure({"write_failed", "Write failed", {}}));
    operation->finish();
    QApplication::processEvents();

    EXPECT_TRUE(host.notifications.isEmpty());
}
TEST(WebServerPluginTest, OpensTheFormForAFolderAndNeverConfiguresTheSameRootTwice) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString canonicalRoot = QFileInfo(directory.path()).canonicalFilePath();
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), canonicalRoot}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 45123}, {QStringLiteral("terminal_id"), QVariant{}}}});

    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QApplication::processEvents();
    QSignalSpy formRequested(&plugin, &plugins::webserver::WebServerPlugin::folderRequested);

    QVector<Result<QJsonObject>> answers;
    // clang-format off
    const auto collect = [&answers](Result<QJsonObject> answer) { answers.append(std::move(answer)); };
    // clang-format on

    // A folder a configuration already serves opens the form of that server rather than a second one for the same root.
    plugin.handleRequest(QStringLiteral("ai"), QString::fromLatin1(plugins::serveFolderCapability), {{QStringLiteral("path"), directory.path()}}, collect);
    ASSERT_EQ(answers.size(), 1);
    EXPECT_TRUE(answers.at(0).hasValue());
    ASSERT_EQ(formRequested.count(), 1);
    EXPECT_EQ(formRequested.at(0).at(0).toString(), QStringLiteral("server-1"));
    EXPECT_EQ(formRequested.at(0).at(1).toString(), canonicalRoot);
    EXPECT_EQ(host.revealedNavigation, QStringList({QStringLiteral("manager")}));

    // A folder nobody serves opens the form of a server that does not exist yet, and nothing is configured until that form is confirmed.
    QTemporaryDir fresh;
    ASSERT_TRUE(fresh.isValid());
    plugin.handleRequest(QStringLiteral("ai"), QString::fromLatin1(plugins::serveFolderCapability), {{QStringLiteral("path"), fresh.path()}}, collect);
    ASSERT_EQ(answers.size(), 2);
    EXPECT_TRUE(answers.at(1).hasValue());
    ASSERT_EQ(formRequested.count(), 2);
    EXPECT_NE(formRequested.at(1).at(0).toString(), QStringLiteral("server-1"));
    EXPECT_EQ(formRequested.at(1).at(1).toString(), QFileInfo(fresh.path()).canonicalFilePath());
    EXPECT_EQ(plugin.configuredWebServers().size(), 1);

    // A path that names no directory is refused, so no form opens and the view is not revealed for nothing.
    plugin.handleRequest(QStringLiteral("ai"), QString::fromLatin1(plugins::serveFolderCapability), {{QStringLiteral("path"), QDir(directory.path()).filePath(QStringLiteral("absent"))}}, collect);
    ASSERT_EQ(answers.size(), 3);
    EXPECT_FALSE(answers.at(2).hasValue());
    // The reason travels in the language of the shell, because the caller shows it and only this plugin knows what failed.
    EXPECT_EQ(answers.at(2).error().message, host.translate(QStringLiteral("web-server.error.root-invalid")));
    EXPECT_EQ(formRequested.count(), 2);
    EXPECT_EQ(host.revealedNavigation.size(), 2);

    plugin.shutdown();
}

TEST(WebServerViewTest, OpensThePreFilledFormWhenAFolderArrivesFromAnotherPlugin) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString canonicalRoot = QFileInfo(directory.path()).canonicalFilePath();
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host, {});

    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QApplication::processEvents();
    std::unique_ptr<QWidget> navigation(plugin.createNavigationView(QStringLiteral("manager"), nullptr));
    ASSERT_NE(navigation, nullptr);

    QVector<Result<QJsonObject>> answers;
    // clang-format off
    const auto collect = [&answers](Result<QJsonObject> answer) { answers.append(std::move(answer)); };
    // clang-format on
    plugin.handleRequest(QStringLiteral("ai"), QString::fromLatin1(plugins::serveFolderCapability), {{QStringLiteral("path"), directory.path()}}, collect);
    ASSERT_EQ(answers.size(), 1);
    EXPECT_TRUE(answers.at(0).hasValue());

    auto* dialog = navigation->findChild<QDialog*>(QStringLiteral("webServerDialog"));
    ASSERT_NE(dialog, nullptr);
    bool carriesRoot = false;
    bool carriesName = false;

    for (auto* field : dialog->findChildren<QLineEdit*>()) {
        carriesRoot = carriesRoot || field->text() == canonicalRoot;
        carriesName = carriesName || field->text() == QFileInfo(canonicalRoot).fileName();
    }

    EXPECT_TRUE(carriesRoot);
    EXPECT_TRUE(carriesName);

    // Nothing is configured until the form is confirmed, so a rejected form leaves no server behind.
    dialog->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(plugin.configuredWebServers().isEmpty());
    EXPECT_EQ(navigation->findChild<QDialog*>(QStringLiteral("webServerDialog")), nullptr);

    navigation.reset();
    plugin.shutdown();
}

TEST(WebServerDialogTest, ClosesItselfWhenTheServerItConfiguredIsRunning) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host, {});

    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QApplication::processEvents();

    plugins::webserver::WebServerDialog dialog(QStringLiteral("server-1"), {}, directory.path(), plugin);
    auto* start = dialog.findChild<QPushButton*>(QStringLiteral("webServerStartButton"));
    ASSERT_NE(start, nullptr);
    QSignalSpy accepted(&dialog, &QDialog::accepted);

    // The stop of the form is the destructive red of the stop in the list, so one action does not read as two.
    auto* stop = dialog.findChild<QPushButton*>(QStringLiteral("webServerStopButton"));
    ASSERT_NE(stop, nullptr);
    const QImage stopIcon = stop->icon().pixmap(32, 32).toImage();
    const QImage danger = ui::IconCatalog::icon(ui::IconName::Stop, host.theme().color(ui::ThemeColor::Danger)).pixmap(32, 32).toImage();
    EXPECT_EQ(stopIcon, danger);

    // A server that answered is configured and running, so the form has nothing left to ask.
    QTcpServer probe;
    ASSERT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 free = probe.serverPort();
    probe.close();
    auto* port = dialog.findChild<QSpinBox*>(QStringLiteral("webServerPortField"));
    ASSERT_NE(port, nullptr);
    port->setValue(free);
    start->click();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return accepted.count() == 1; }));
    // clang-format on
    EXPECT_TRUE(plugin.webServerConfigured(QStringLiteral("server-1")));

    plugin.stopWebServer(QStringLiteral("server-1"));
    plugin.shutdown();
}

// The dialog reports a browser dispatch it could not make, so the row action beside it says the same thing rather than doing nothing at all.
TEST(WebServerViewTest, SaysTheAddressCouldNotBeOpenedWhenTheSystemBrowserRefusesIt) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), directory.path()}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 45124}, {QStringLiteral("terminal_id"), QVariant{}}}});

    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QApplication::processEvents();

    std::unique_ptr<QWidget> navigation(plugin.createNavigationView(QStringLiteral("manager"), nullptr));
    ASSERT_NE(navigation, nullptr);
    host.notifications.clear();

    // A server that is not running has no address, so the dispatch fails without ever reaching the desktop of the machine running this case.
    QToolButton source(navigation.get());
    source.setProperty("serverId", QStringLiteral("server-1"));
    ASSERT_TRUE(QObject::connect(&source, SIGNAL(clicked(bool)), navigation.get(), SLOT(openServer())));
    source.click();
    QApplication::processEvents();

    ASSERT_EQ(host.notifications.size(), 1);
    EXPECT_EQ(host.notifications.constFirst().message, host.translate(QStringLiteral("web-server.error.open")));
    EXPECT_EQ(host.notifications.constFirst().severity, plugins::AlertSeverity::Error);

    // A click that reaches the slot any other way has no sender, so it asks for nothing instead of dereferencing one.
    host.notifications.clear();
    ASSERT_TRUE(QMetaObject::invokeMethod(navigation.get(), "openServer", Qt::DirectConnection));
    EXPECT_TRUE(host.notifications.isEmpty());
}

TEST(WebServerViewTest, KeepsARowActionAliveWhileItIsDeliveringItsOwnClick) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), directory.path()}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), 45123}, {QStringLiteral("terminal_id"), QVariant{}}}});

    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    QApplication::processEvents();

    std::unique_ptr<QWidget> navigation(plugin.createNavigationView(QStringLiteral("manager"), nullptr));
    ASSERT_NE(navigation, nullptr);
    auto* table = navigation->findChild<QTableWidget*>(QStringLiteral("webServerTable"));
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);

    // Starting a server announces its pending state at once, so the row is rebuilt while the action that asked for it is still on the stack.
    const QPointer<QWidget> rowActions = table->cellWidget(0, 4);
    ASSERT_FALSE(rowActions.isNull());

    // A row action follows the selection of its row, so a glyph never sits on the accent in a colour that reads through it.
    QToolButton* edit = nullptr;

    for (auto* button : rowActions->findChildren<QToolButton*>()) {
        if (button->toolTip() == host.translate(QStringLiteral("web-server.dialog.edit"))) {
            edit = button;
        }
    }

    ASSERT_NE(edit, nullptr);
    table->clearSelection();
    QApplication::processEvents();
    EXPECT_EQ(edit->icon().pixmap(32, 32).toImage(), ui::IconCatalog::icon(ui::IconName::Edit, host.theme().color(ui::ThemeColor::TextMuted)).pixmap(32, 32).toImage());
    table->selectRow(0);
    QApplication::processEvents();
    EXPECT_EQ(edit->icon().pixmap(32, 32).toImage(), ui::IconCatalog::icon(ui::IconName::Edit, host.theme().color(ui::ThemeColor::OnAccent)).pixmap(32, 32).toImage());

    // What the status cell draws and the ink it is written in follow the selection as well, otherwise a colour reads through the accent.
    QTableWidgetItem* status = table->item(0, 0);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->icon().pixmap(32, 32).toImage(), ui::IconCatalog::icon(ui::IconName::WebServer, host.theme().color(ui::ThemeColor::OnAccent)).pixmap(32, 32).toImage());
    EXPECT_EQ(status->foreground().color(), host.theme().color(ui::ThemeColor::OnAccent));

    // A row rebuilt while it stays selected keeps its actions readable, which is what the state of a server changing does to it.
    table->setItem(0, 1, new QTableWidgetItem(QStringLiteral("Preview")));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([edit, &host]() { return edit->icon().pixmap(32, 32).toImage() == ui::IconCatalog::icon(ui::IconName::Edit, host.theme().color(ui::ThemeColor::OnAccent)).pixmap(32, 32).toImage(); }));
    // clang-format on
    table->clearSelection();
    QApplication::processEvents();
    EXPECT_EQ(edit->icon().pixmap(32, 32).toImage(), ui::IconCatalog::icon(ui::IconName::Edit, host.theme().color(ui::ThemeColor::TextMuted)).pixmap(32, 32).toImage());

    QToolButton* start = nullptr;

    for (auto* button : rowActions->findChildren<QToolButton*>()) {
        if (button->toolTip() == host.translate(QStringLiteral("web-server.manager.start-server"))) {
            start = button;
        }
    }

    ASSERT_NE(start, nullptr);
    start->click();
    EXPECT_FALSE(rowActions.isNull());

    // The rebuild still happens, and the replaced action is released once the click has returned to the event loop.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(rowActions.isNull());
    EXPECT_NE(table->cellWidget(0, 4), nullptr);

    plugin.stopWebServer(QStringLiteral("server-1"));
    navigation.reset();
    plugin.shutdown();
}
TEST(WebServerDialogTest, ExpandsInsteadOfCompressingControlsWhenAnErrorAppears) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QTcpServer occupied;
    ASSERT_TRUE(occupied.listen(QHostAddress::LocalHost, 0));
    test::TestPluginHost host;
    WebServerTestsHelper::configureWebDatabase(host, {{{QStringLiteral("id"), QStringLiteral("server-1")}, {QStringLiteral("name"), QStringLiteral("Preview")}, {QStringLiteral("root"), directory.path()}, {QStringLiteral("bind_host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), occupied.serverPort()}, {QStringLiteral("terminal_id"), QVariant{}}}});
    WebServerTestsHelper::configureSnapshotReply(host, WebServerTestsHelper::terminalSnapshot(QStringLiteral("terminal-1"), directory.path()));
    plugins::webserver::WebServerPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    plugins::webserver::WebServerDialog dialog(QStringLiteral("server-1"), {}, {}, plugin);
    dialog.show();
    QApplication::processEvents();
    const int initialHeight = dialog.height();
    auto* error = dialog.findChild<QLabel*>(QStringLiteral("webServerError"));
    ASSERT_NE(error, nullptr);
    EXPECT_FALSE(error->isVisible());

    auto* startButton = dialog.findChild<QPushButton*>(QStringLiteral("webServerStartButton"));
    ASSERT_NE(startButton, nullptr);
    const QList<int> controlHeights{startButton->height()};
    QTest::mouseClick(startButton, Qt::LeftButton);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return error->isVisible(); }, 3000));
    // clang-format on
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return dialog.height() > initialHeight; }, 3000));
    // clang-format on
    EXPECT_EQ(startButton->height(), controlHeights.first());
}

void WebServerTestsHelper::writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(contents), contents.size());
}

QByteArray WebServerTestsHelper::request(plugins::webserver::WebServerInstance& server, const QByteArray& contents) {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.port());

    if (!socket.waitForConnected(2000)) {
        return {};
    }

    QByteArray response;
    QEventLoop responseLoop;
    QTimer timeout;
    timeout.setSingleShot(true);
    // clang-format off
    QObject::connect(&socket, &QTcpSocket::readyRead, &responseLoop, [&socket, &response]() { response.append(socket.readAll()); });
    QObject::connect(&socket, &QTcpSocket::disconnected, &responseLoop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &responseLoop, &QEventLoop::quit);
    // clang-format on

    socket.write(contents);
    socket.flush();
    timeout.start(3000);

    if (socket.state() != QAbstractSocket::UnconnectedState) {
        responseLoop.exec();
    }

    response.append(socket.readAll());
    return response;
}

QJsonObject WebServerTestsHelper::terminalSnapshot(const QString& activeId, const QString& directory) {
    return {{QStringLiteral("activeTerminalId"), activeId}, {QStringLiteral("terminals"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("terminal-1")}, {QStringLiteral("name"), QStringLiteral("Shell")}, {QStringLiteral("cwd"), directory}}}}};
}

void WebServerTestsHelper::configureSnapshotReply(test::TestPluginHost& host, QJsonObject snapshot) {
    host.availableCapabilities.insert(QString::fromLatin1(plugins::terminalSnapshotCapability));
    // clang-format off
    host.capabilityHandler = [snapshot = std::move(snapshot)](const QString&, const QJsonObject&, QObject*, plugins::PluginReply reply) { reply(Result<QJsonObject>::success(snapshot)); };
    // clang-format on
}

void WebServerTestsHelper::configureWebDatabase(test::TestPluginHost& host, persistence::DatabaseRows configurations, int splitRatio) {
    host.translations = plugins::webserver::translations::WebServerCatalog::english();
    host.settingsDocument = {{QStringLiteral("splitRatio"), splitRatio}};
    // clang-format off
    host.queryHandler = [configurations = std::move(configurations)](const QString& statement, const QVariantList&) {
        if (statement.contains(QStringLiteral("web_server_configurations"))) {
            return Result<persistence::DatabaseRows>::success(configurations);
        }
        return Result<persistence::DatabaseRows>::failure({"unexpected_query", "The test received an unexpected database query", statement});
    };
    // clang-format on
}
} // namespace workpane

TEST(WebServerTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    workpane::plugins::webserver::WebServerPlugin plugin;
    workpane::test::TestCatalogs::expectCompleteCatalog(QStringLiteral("web-server"), plugin.translations());
}
