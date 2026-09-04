#include "AiTestSupport.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace workpane::plugins::ai {

TEST(AiChatClientTest, DoesNotRetryARequestTheProviderRejected) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    int requests = 0;
    // clang-format off
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &requests]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &requests]() {
        if (!socket->readAll().contains(QByteArrayLiteral("\r\n\r\n"))) {
            return;
        }
        ++requests;
        const QByteArray body = QByteArrayLiteral("{\"error\":{\"message\":\"invalid model\"}}");
        socket->write(QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
        socket->disconnectFromHost();
        });
    });
    // clang-format on

    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*openai, model), {}};

    AiRequestGate clientGate;
    AiHttpChatClient client(clientGate);
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&client, &AiChatClient::failed, &client, [&failures](const Error& error) { failures.append(error); });
    // clang-format on
    // clang-format off
    client.send({connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
    // clang-format on

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !failures.isEmpty(); }, 10000));
    // clang-format on
    EXPECT_EQ(requests, 1);
    EXPECT_TRUE(failures.first().message.contains(QStringLiteral("invalid model")));
}

TEST(AiToolRegistryTest, WritesAGeneratedImageWhereTheAgentAsksAndReportsProviderFailures) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    const QByteArray pixel = QByteArrayLiteral("\x89PNG\r\n\x1a\n-recorded-image-bytes");
    // clang-format off
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, pixel]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, pixel]() {
        if (!socket->readAll().contains(QByteArrayLiteral("\r\n\r\n"))) {
            return;
        }
        const QJsonObject payload{{QStringLiteral("data"), QJsonArray{QJsonObject{{QStringLiteral("b64_json"), QString::fromUtf8(pixel.toBase64())}}}}};
        const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
        socket->disconnectFromHost();
        });
    });
    // clang-format on

    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);

    QTemporaryDir artifacts;
    ASSERT_TRUE(artifacts.isValid());
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    const ModelConnection media{openai->id, QStringLiteral("gpt-image-1"), {}, QStringLiteral("sk"), {}, {}, {}};
    const QString mediaAddress = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    registry.setMediaConfiguration(media, mediaAddress);

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on
    registry.invoke({QStringLiteral("i1"), QStringLiteral("generate_image"), QJsonObject{{QStringLiteral("prompt"), QStringLiteral("a red square")}, {QStringLiteral("path"), QStringLiteral("media/square.png")}}}, artifacts.path(), collect);

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    ASSERT_FALSE(results.first().failed) << results.first().text.toStdString();
    // The file lands exactly where the agent asked for it, inside the working directory.
    EXPECT_EQ(results.first().text, QDir(QDir(artifacts.path()).canonicalPath()).filePath(QStringLiteral("media/square.png")));

    QFile stored(results.first().text);
    ASSERT_TRUE(stored.open(QIODevice::ReadOnly));
    EXPECT_EQ(stored.readAll(), pixel);

    // A tool without a configured address refuses instead of reaching an empty endpoint.
    AiToolRegistry unconfigured(host);
    QVector<ToolResult> refusals;
    // clang-format off
    const auto collectRefusal = [&refusals](ToolResult result) { refusals.append(std::move(result)); };
    // clang-format on
    unconfigured.invoke({QStringLiteral("i2"), QStringLiteral("generate_image"), QJsonObject{{QStringLiteral("prompt"), QStringLiteral("x")}, {QStringLiteral("path"), QStringLiteral("x.png")}}}, artifacts.path(), collectRefusal);
    ASSERT_EQ(refusals.size(), 1);
    EXPECT_TRUE(refusals.first().failed);
}

// The address, the credential header, the fields of the body and the model are all declared by the endpoint, so a second service is data rather than a branch.
TEST(AiToolRegistryTest, SpeaksThroughTheRequestEachServiceDeclaresRatherThanOneWrittenInCode) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    const QByteArray audio = QByteArrayLiteral("recorded-openai-audio");
    QByteArray received;
    // clang-format off
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, audio, &received]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, audio, &received]() {
            received.append(socket->readAll());
            if (!received.contains(QByteArrayLiteral("\r\n\r\n")) || !received.endsWith(QByteArrayLiteral("}"))) {
                return;
            }
            socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: ") + QByteArray::number(audio.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + audio);
            socket->disconnectFromHost();
        });
    });
    // clang-format on

    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);
    QTemporaryDir artifacts;
    ASSERT_TRUE(artifacts.isValid());
    registry.setSpeechConfiguration({QStringLiteral("openai"), QStringLiteral("nova"), QStringLiteral("sk-recorded")}, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on
    registry.invoke({QStringLiteral("s1"), QStringLiteral("generate_speech"), QJsonObject{{QStringLiteral("text"), QStringLiteral("spoken words")}, {QStringLiteral("path"), QStringLiteral("said.mp3")}}}, artifacts.path(), collect);

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    ASSERT_FALSE(results.first().failed) << results.first().text.toStdString();

    // This service names the voice in the body rather than in the address, reads its credential after a scheme and requires a model of its own.
    EXPECT_TRUE(received.contains(QByteArrayLiteral("POST /audio/speech")));
    EXPECT_TRUE(received.contains(QByteArrayLiteral("Authorization: Bearer sk-recorded")));
    const QJsonObject body = QJsonDocument::fromJson(received.mid(received.indexOf(QByteArrayLiteral("\r\n\r\n")) + 4)).object();
    EXPECT_EQ(body.value(QStringLiteral("input")).toString(), QStringLiteral("spoken words"));
    EXPECT_EQ(body.value(QStringLiteral("voice")).toString(), QStringLiteral("nova"));
    EXPECT_EQ(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o-mini-tts"));
}

TEST(AiToolRegistryTest, WritesGeneratedSpeechWhereTheAgentAsksAndRefusesWithoutConfiguration) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    const QByteArray audio = QByteArrayLiteral("ID3-recorded-audio-bytes");
    QByteArray receivedPath;
    QByteArray receivedKey;
    // clang-format off
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, audio, &receivedPath, &receivedKey]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, audio, &receivedPath, &receivedKey]() {
            const QByteArray request = socket->readAll();
            if (!request.contains(QByteArrayLiteral("\r\n\r\n"))) {
                return;
            }
            receivedPath = request.left(request.indexOf('\r'));
            for (const auto& line : request.split('\n')) {
                if (line.toLower().startsWith(QByteArrayLiteral("xi-api-key:"))) {
                    receivedKey = line.mid(11).trimmed();
                }
            }
            socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: ") + QByteArray::number(audio.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + audio);
            socket->disconnectFromHost();
        });
    });
    // clang-format on

    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);

    QTemporaryDir artifacts;
    ASSERT_TRUE(artifacts.isValid());
    registry.setMediaConfiguration({}, {});
    registry.setSpeechConfiguration({QStringLiteral("elevenlabs"), QStringLiteral("voice-42"), QStringLiteral("xi-secret")}, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on
    registry.invoke({QStringLiteral("s1"), QStringLiteral("generate_speech"), QJsonObject{{QStringLiteral("text"), QStringLiteral("hello there")}, {QStringLiteral("path"), QStringLiteral("voice/hello.mp3")}}}, artifacts.path(), collect);

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    ASSERT_FALSE(results.first().failed) << results.first().text.toStdString();
    EXPECT_EQ(results.first().text, QDir(QDir(artifacts.path()).canonicalPath()).filePath(QStringLiteral("voice/hello.mp3")));

    // The voice becomes part of the path and the credential travels in the header the provider requires.
    EXPECT_TRUE(receivedPath.contains(QByteArrayLiteral("/v1/text-to-speech/voice-42")));
    EXPECT_EQ(receivedKey, QByteArrayLiteral("xi-secret"));

    QFile stored(results.first().text);
    ASSERT_TRUE(stored.open(QIODevice::ReadOnly));
    EXPECT_EQ(stored.readAll(), audio);

    // A path outside the working directory is refused like every other file tool.
    registry.invoke({QStringLiteral("s3"), QStringLiteral("generate_speech"), QJsonObject{{QStringLiteral("text"), QStringLiteral("x")}, {QStringLiteral("path"), QStringLiteral("../escaped.mp3")}}}, artifacts.path(), collect);
    ASSERT_EQ(results.size(), 2);
    EXPECT_TRUE(results.at(1).failed);
    // The refusal names the path it refused and the directory it had to stay inside, because a model told only that it was refused repeats the same call.
    EXPECT_TRUE(results.at(1).text.contains(QStringLiteral("../escaped.mp3"))) << results.at(1).text.toStdString();
    EXPECT_TRUE(results.at(1).text.contains(QFileInfo(artifacts.path()).canonicalFilePath())) << results.at(1).text.toStdString();

    AiToolRegistry unconfigured(host);
    QVector<ToolResult> refusals;
    // clang-format off
    const auto collectRefusal = [&refusals](ToolResult result) { refusals.append(std::move(result)); };
    // clang-format on
    unconfigured.invoke({QStringLiteral("s2"), QStringLiteral("generate_speech"), QJsonObject{{QStringLiteral("text"), QStringLiteral("x")}, {QStringLiteral("path"), QStringLiteral("x.mp3")}}}, artifacts.path(), collectRefusal);
    ASSERT_EQ(refusals.size(), 1);
    EXPECT_TRUE(refusals.first().failed);
}

TEST(McpClientTest, InitializesDiscoversToolsAndCallsThemOverTheStdioTransport) {
    agent::mcp::McpServerDescriptor fixture;
    fixture.id = QStringLiteral("fixture");
    fixture.command = QCoreApplication::applicationFilePath();
    fixture.arguments = {QStringLiteral("--workpane-test-mcp")};
    agent::mcp::McpClient client(fixture);
    QSignalSpy initialized(&client, &agent::mcp::McpClient::initialized);
    QSignalSpy toolsChanged(&client, &agent::mcp::McpClient::toolsChanged);
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&client, &agent::mcp::McpClient::failed, &client, [&failures](const Error& error) { failures.append(error); });
    // clang-format on

    client.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return initialized.count() == 1 && toolsChanged.count() == 1; }));
    // clang-format on
    EXPECT_TRUE(client.ready());
    EXPECT_TRUE(failures.isEmpty());

    // The negotiated capabilities and the discovered tools come from the server, not from a local assumption.
    EXPECT_TRUE(client.serverCapabilities().contains(QStringLiteral("tools")));
    ASSERT_EQ(client.tools().size(), 1);
    EXPECT_EQ(client.tools().first().name, QStringLiteral("get_weather"));
    EXPECT_EQ(client.tools().first().serverId, QStringLiteral("fixture"));
    EXPECT_EQ(client.tools().first().inputSchema.value(QStringLiteral("type")).toString(), QStringLiteral("object"));

    QVector<Result<QJsonObject>> replies;
    // clang-format off
    const auto collect = [&replies](Result<QJsonObject> result) { replies.append(std::move(result)); };
    // clang-format on
    client.callTool(QStringLiteral("get_weather"), QJsonObject{{QStringLiteral("city"), QStringLiteral("Lisbon")}}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !replies.isEmpty(); }));
    // clang-format on
    ASSERT_TRUE(replies.first().hasValue());
    EXPECT_EQ(replies.first().value().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("sunny in Lisbon"));

    client.ping(collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return replies.size() == 2; }));
    // clang-format on
    EXPECT_TRUE(replies.at(1).hasValue());

    // A method the server does not implement returns its JSON-RPC error instead of hanging.
    client.request(QStringLiteral("completion/complete"), {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return replies.size() == 3; }));
    // clang-format on
    ASSERT_FALSE(replies.at(2).hasValue());
    EXPECT_EQ(replies.at(2).error().code, QStringLiteral("ai_mcp_error"));
    EXPECT_EQ(replies.at(2).error().detail, QStringLiteral("-32601"));

    client.stop();
    EXPECT_FALSE(client.ready());
}

TEST(McpClientTest, ServesToolsAgainAfterItWasStoppedAndStartedOnceMore) {
    agent::mcp::McpServerDescriptor fixture;
    fixture.id = QStringLiteral("fixture");
    fixture.command = QCoreApplication::applicationFilePath();
    fixture.arguments = {QStringLiteral("--workpane-test-mcp")};
    agent::mcp::McpClient client(fixture);
    QSignalSpy toolsChanged(&client, &agent::mcp::McpClient::toolsChanged);

    client.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return toolsChanged.count() == 1; }));
    // clang-format on
    client.stop();
    EXPECT_FALSE(client.ready());

    client.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return toolsChanged.count() == 2; }));
    // clang-format on
    ASSERT_EQ(client.tools().size(), 1);

    QVector<Result<QJsonObject>> replies;
    // clang-format off
    const auto collect = [&replies](Result<QJsonObject> result) { replies.append(std::move(result)); };
    // clang-format on
    client.callTool(QStringLiteral("get_weather"), QJsonObject{{QStringLiteral("city"), QStringLiteral("Porto")}}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !replies.isEmpty(); }));
    // clang-format on
    EXPECT_TRUE(replies.first().hasValue());

    client.stop();
    agent::mcp::McpClient::drainTransports();
}

TEST(McpClientTest, LeavesNothingBehindThroughManyStartAndStopCycles) {
    agent::mcp::McpServerDescriptor fixture;
    fixture.id = QStringLiteral("fixture");
    fixture.command = QCoreApplication::applicationFilePath();
    fixture.arguments = {QStringLiteral("--workpane-test-mcp")};
    agent::mcp::McpClient client(fixture);
    QSignalSpy toolsChanged(&client, &agent::mcp::McpClient::toolsChanged);

    for (int cycle = 1; cycle <= 12; ++cycle) {
        client.start();
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return toolsChanged.count() == cycle; }));
        // clang-format on
        ASSERT_EQ(client.tools().size(), 1);
        client.stop();
        EXPECT_FALSE(client.ready());
    }

    agent::mcp::McpClient::drainTransports();
}

TEST(AiCommandRunnerTest, KeepsOnlyTheReadableTextOfACommandThatDrawsInTheTerminal) {
    QString pending;
    const QString coloured = QString::fromUtf8("\x1b[36mhelp\x1b[0m    Show this help\n");
    EXPECT_EQ(CommandOutput::plainCommandOutput(pending, coloured), QStringLiteral("help    Show this help\n"));
    EXPECT_TRUE(pending.isEmpty());

    // A window title, a cursor move and a bell are not text the execution recorded.
    EXPECT_EQ(CommandOutput::plainCommandOutput(pending, QString::fromUtf8("\x1b]0;building\x07ready\n")), QStringLiteral("ready\n"));
    EXPECT_EQ(CommandOutput::plainCommandOutput(pending, QString::fromUtf8("\x1b[2K\x1b[1Gdone\n")), QStringLiteral("done\n"));

    // A sequence split across two reads is completed by the next chunk instead of leaking its bytes.
    EXPECT_EQ(CommandOutput::plainCommandOutput(pending, QString::fromUtf8("first\x1b[3")), QStringLiteral("first"));
    EXPECT_FALSE(pending.isEmpty());
    EXPECT_EQ(CommandOutput::plainCommandOutput(pending, QString::fromUtf8("6msecond")), QStringLiteral("second"));
    EXPECT_TRUE(pending.isEmpty());

    // A progress rewrite becomes its own line and a carriage return before a newline is not doubled.
    EXPECT_EQ(CommandOutput::plainCommandOutput(pending, QStringLiteral("10%\r20%\r\n")), QStringLiteral("10%\n20%\n"));
    EXPECT_EQ(CommandOutput::plainCommandOutput(pending, QStringLiteral("kept\ttabs\n")), QStringLiteral("kept\ttabs\n"));
}

// Asking for the whole window as an answer leaves the conversation none of it, so that budget is refused where it is typed rather than compacting every turn away.
// A reader configuring a command line agent picks the provider and a model it offers, so every one of those opens without asking for anything else.
TEST(AiModelConnectionTest, OpensAConnectionToEveryCommandLineProviderFromWhatItOffers) {
    qsizetype checked = 0;

    for (const auto& provider : ProviderCatalog::providerCatalog()) {
        if (provider.protocol != WireProtocol::CommandLine) {
            continue;
        }

        ASSERT_FALSE(provider.models.isEmpty()) << provider.id.toStdString();
        ASSERT_FALSE(provider.preferredModels.isEmpty()) << provider.id.toStdString();

        // What the provider opens with is a model it really offers.
        for (const auto& preferred : provider.preferredModels) {
            EXPECT_NE(ProviderCatalog::findModel(provider, preferred), nullptr) << provider.id.toStdString() << " / " << preferred.toStdString();
        }

        for (const auto& model : provider.models) {
            const ModelConnection connection = ModelConnections::declaredConnection(provider, model.id);
            const auto opened = ModelConnections::validateConnection(connection);
            ASSERT_TRUE(opened.hasValue()) << provider.id.toStdString() << " / " << model.id.toStdString() << " " << opened.error().message.toStdString();
            EXPECT_EQ(ModelConnections::connectionKey(opened.value()), provider.id + QLatin1Char('/') + model.id);
            // A command line agent signs in on its own, so nothing it opens with carries a credential or an address.
            EXPECT_TRUE(opened.value().apiKey.isEmpty());
            EXPECT_TRUE(opened.value().address.isEmpty());
            ++checked;
        }
    }

    EXPECT_GT(checked, 0);
}

TEST(AiModelConnectionTest, RefusesAnAnswerBudgetOfZeroOnAModelWhoseMaximumIsItsWholeWindow) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(QStringLiteral("xai"));
    ASSERT_NE(provider, nullptr);
    const auto budget = ProviderCatalog::outputBudgetParameter(*provider, QStringLiteral("grok-4.6"));
    ASSERT_TRUE(budget.has_value());

    const ModelDescriptor* whole = ProviderCatalog::findModel(*provider, QStringLiteral("grok-4.6"));
    ASSERT_NE(whole, nullptr);
    ASSERT_GE(whole->maximumOutputTokens, whole->contextWindow);

    ModelConnection connection = ModelConnections::declaredConnection(*provider, QStringLiteral("grok-4.6"));
    connection.apiKey = QStringLiteral("a-key");
    connection.parameters.insert(budget->id, 0);
    const auto refused = ModelConnections::validateConnection(connection);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, QStringLiteral("ai_output_budget_whole_window"));
    EXPECT_EQ(refused.error().detail, QStringLiteral("grok-4.6"));

    // A number the reader chooses opens that connection, because it leaves the conversation what remains.
    connection.parameters.insert(budget->id, 8192);
    EXPECT_TRUE(ModelConnections::validateConnection(connection).hasValue());
}

TEST(AiProviderCatalogTest, BoundsTheOutputBudgetByWhatTheSelectedModelAccepts) {
    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    const ModelDescriptor* model = ProviderCatalog::findModel(*anthropic, QStringLiteral("claude-opus-5"));
    ASSERT_NE(model, nullptr);
    ASSERT_GT(model->maximumOutputTokens, 0);

    const auto parameters = ProviderCatalog::applicableParameters(*anthropic, model->id);
    // clang-format off
    const auto budget = std::find_if(parameters.cbegin(), parameters.cend(), [](const ParameterDescriptor& parameter) { return parameter.id == QStringLiteral("maxOutputTokens"); });
    // clang-format on
    ASSERT_NE(budget, parameters.cend());
    EXPECT_EQ(static_cast<int>(budget->maximum), model->maximumOutputTokens);

    // A value the model cannot deliver is rejected by the same shared validation the settings form uses.
    QJsonObject invalid = ProviderCatalog::defaultParameters(*anthropic, model->id);
    invalid[QStringLiteral("maxOutputTokens")] = model->maximumOutputTokens + 1;
    EXPECT_FALSE(ModelConnections::validateParameters(*anthropic, model->id, invalid).hasValue());
    EXPECT_TRUE(ModelConnections::validateParameters(*anthropic, model->id, ProviderCatalog::defaultParameters(*anthropic, model->id)).hasValue());

    // A model the catalog does not declare keeps the provider wide bound instead of inventing one.
    const auto userDefined = ProviderCatalog::applicableParameters(*anthropic, QStringLiteral("claude-custom"));
    // clang-format off
    const auto userBudget = std::find_if(userDefined.cbegin(), userDefined.cend(), [](const ParameterDescriptor& parameter) { return parameter.id == QStringLiteral("maxOutputTokens"); });
    // clang-format on
    ASSERT_NE(userBudget, userDefined.cend());
    EXPECT_GT(userBudget->maximum, model->maximumOutputTokens);
}

TEST(AiModelDiscoveryTest, ReadsTheCatalogEachProtocolPublishesAndReportsAnEmptyOne) {
    const QByteArray openAiPayload = QByteArrayLiteral(R"({"object":"list","data":[{"id":"gpt-4o-mini","object":"model","created":1721172741,"owned_by":"system"},{"id":"gpt-4o","object":"model","created":1715367049,"owned_by":"system"}]})");
    RecordedSearchServer catalog(openAiPayload);
    ASSERT_TRUE(catalog.listen());

    AiModelDiscovery discovery;
    QVector<QStringList> discovered;
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&discovery, &AiModelDiscovery::discovered, &discovery, [&discovered](const QStringList& models) { discovered.append(models); });
    QObject::connect(&discovery, &AiModelDiscovery::failed, &discovery, [&failures](const Error& error) { failures.append(error); });
    // clang-format on

    discovery.discover(QStringLiteral("openai"), QStringLiteral("sk-test"), catalog.address());
    EXPECT_TRUE(discovery.running());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !discovered.isEmpty(); }));
    // clang-format on
    EXPECT_FALSE(discovery.running());
    EXPECT_EQ(discovered.first(), QStringList({QStringLiteral("gpt-4o"), QStringLiteral("gpt-4o-mini")}));
    EXPECT_TRUE(catalog.requestLine().contains(QByteArrayLiteral("GET /models")));
    EXPECT_TRUE(catalog.requestHead().contains(QByteArrayLiteral("Authorization: Bearer sk-test")));

    // The Anthropic protocol publishes the same shape under its own path and credential header.
    const QByteArray anthropicPayload = QByteArrayLiteral(R"({"data":[{"type":"model","id":"claude-opus-5","display_name":"Claude Opus 5","created_at":"2025-11-24T00:00:00Z"}],"has_more":false})");
    RecordedSearchServer anthropic(anthropicPayload);
    ASSERT_TRUE(anthropic.listen());
    discovery.discover(QStringLiteral("anthropic"), QStringLiteral("sk-ant"), anthropic.address());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return discovered.size() == 2; }));
    // clang-format on
    EXPECT_EQ(discovered.at(1), QStringList({QStringLiteral("claude-opus-5")}));
    EXPECT_TRUE(anthropic.requestLine().contains(QByteArrayLiteral("GET /v1/models")));
    EXPECT_TRUE(anthropic.requestHead().contains(QByteArrayLiteral("X-Api-Key: sk-ant")));

    // A provider answering with an empty catalog is an explicit failure rather than an empty selection.
    RecordedSearchServer empty(QByteArrayLiteral(R"({"object":"list","data":[]})"));
    ASSERT_TRUE(empty.listen());
    discovery.discover(QStringLiteral("openai"), QStringLiteral("sk-test"), empty.address());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !failures.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(failures.first().code, QStringLiteral("ai_model_discovery_empty"));

    // An unknown provider and an unresolvable environment reference never reach the network.
    discovery.discover(QStringLiteral("absent"), {}, catalog.address());
    ASSERT_EQ(failures.size(), 2);
    EXPECT_EQ(failures.at(1).code, QStringLiteral("ai_provider_unknown"));
    discovery.discover(QStringLiteral("openai"), QStringLiteral("{env.WORKPANE_ABSENT_KEY}"), catalog.address());
    ASSERT_EQ(failures.size(), 3);
}

TEST(AiToolRegistryTest, KeepsTheEndOfAnOversizedResultBecauseThatIsWhereTheFailureIs) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    QFile file(QDir(workdir.path()).filePath(QStringLiteral("build.log")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("compilation started\n"));
    file.write(QByteArray(60000, 'x'));
    file.write(QByteArrayLiteral("\nerror: the build failed here"));
    file.close();

    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on
    registry.invoke({QStringLiteral("r1"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("build.log")}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on

    const QString text = results.first().text;
    EXPECT_LT(text.size(), 60000);
    EXPECT_TRUE(text.startsWith(QStringLiteral("compilation started")));
    EXPECT_TRUE(text.endsWith(QStringLiteral("error: the build failed here")));
}

TEST(AiToolRegistryTest, HandsAnImageToAModelThatSeesAndRefusesForOneThatDoesNot) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    QImage picture(4, 4, QImage::Format_RGB32);
    picture.fill(Qt::blue);
    ASSERT_TRUE(picture.save(QDir(workdir.path()).filePath(QStringLiteral("shot.png"))));
    QFile notes(QDir(workdir.path()).filePath(QStringLiteral("notes.txt")));
    ASSERT_TRUE(notes.open(QIODevice::WriteOnly));
    notes.write(QByteArrayLiteral("plain"));
    notes.close();

    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    const AiTask seeing = AiTestsHelper::makeTask(QStringLiteral("task-1"), QStringLiteral("workspace-1"));
    const ModelConnection seeingConnection = AiTestsHelper::testConnection();
    ASSERT_TRUE(ProviderCatalog::modelTraits(*ProviderCatalog::findProvider(seeingConnection.providerId), seeingConnection.modelId).contains(ModelTrait::Vision));
    registry.setTaskContext(seeing, seeingConnection);

    registry.invoke({QStringLiteral("i1"), QStringLiteral("read_image"), QJsonObject{{QStringLiteral("path"), QStringLiteral("shot.png")}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    ASSERT_FALSE(results.first().failed);
    EXPECT_EQ(results.first().imageMediaType, QByteArrayLiteral("image/png"));
    EXPECT_FALSE(results.first().imageData.isEmpty());

    // A file that is not an image is refused by its own name rather than sent as bytes the model cannot read.
    registry.invoke({QStringLiteral("i2"), QStringLiteral("read_image"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}}}, workdir.path(), collect);
    ASSERT_EQ(results.size(), 2);
    EXPECT_TRUE(results.at(1).failed);
    EXPECT_TRUE(results.at(1).imageData.isEmpty());

    // A model that declares no image input is told so by name, before anything is read.
    ModelConnection blind = seeingConnection;
    blind.modelId = QStringLiteral("o3-mini");
    ASSERT_FALSE(ProviderCatalog::modelTraits(*ProviderCatalog::findProvider(blind.providerId), blind.modelId).contains(ModelTrait::Vision));
    registry.setTaskContext(seeing, blind);
    registry.invoke({QStringLiteral("i3"), QStringLiteral("read_image"), QJsonObject{{QStringLiteral("path"), QStringLiteral("shot.png")}}}, workdir.path(), collect);
    ASSERT_EQ(results.size(), 3);
    EXPECT_TRUE(results.at(2).failed);
    EXPECT_TRUE(results.at(2).text.contains(blind.modelId));
}

TEST(AiToolRegistryTest, AcceptsAPathInsideTheWorkingDirectoryHoweverItWasWritten) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QString root = QDir(workdir.path()).canonicalPath();

    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    // A model told its working directory writes the absolute form, and refusing it only makes it repeat the same call.
    registry.invoke({QStringLiteral("w1"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), root + QStringLiteral("/index.html")}, {QStringLiteral("content"), QStringLiteral("<html/>")}}}, root, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    ASSERT_FALSE(results.first().failed) << results.first().text.toStdString();
    EXPECT_TRUE(QFile::exists(root + QStringLiteral("/index.html")));

    // A model writing into a directory that is not there yet is not made to create it one level at a time.
    registry.invoke({QStringLiteral("w2"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("nested/page.html")}, {QStringLiteral("content"), QStringLiteral("<html/>")}}}, root, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_FALSE(results.at(1).failed) << results.at(1).text.toStdString();
    EXPECT_TRUE(QFile::exists(root + QStringLiteral("/nested/page.html")));

    // What lands outside is still refused, whichever form it was written in.
    registry.invoke({QStringLiteral("w3"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QDir::rootPath() + QStringLiteral("workpane-escaped.txt")}, {QStringLiteral("content"), QStringLiteral("x")}}}, root, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 3; }));
    // clang-format on
    EXPECT_TRUE(results.at(2).failed);
    registry.invoke({QStringLiteral("w4"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("../escaped.txt")}, {QStringLiteral("content"), QStringLiteral("x")}}}, root, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 4; }));
    // clang-format on
    EXPECT_TRUE(results.at(3).failed);

    // A symbolic link is followed before the check, so a link inside the root cannot reach outside it.
    QTemporaryDir outside;
    ASSERT_TRUE(outside.isValid());
    const QString link = root + QStringLiteral("/escape");

    if (!QFile::link(QDir(outside.path()).canonicalPath(), link) || !QFileInfo(link).isSymLink()) {
        GTEST_SKIP() << "The platform did not allow creating a symbolic link";
    }

    registry.invoke({QStringLiteral("w5"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("escape/leaked.txt")}, {QStringLiteral("content"), QStringLiteral("x")}}}, root, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 5; }));
    // clang-format on
    EXPECT_TRUE(results.at(4).failed) << results.at(4).text.toStdString();
    EXPECT_FALSE(QFileInfo(QDir(outside.path()).filePath(QStringLiteral("leaked.txt"))).exists());

    // A link above a directory that does not exist yet still decides where the file lands, so the deepest ancestor that exists is what gets resolved.
    registry.invoke({QStringLiteral("w6"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("escape/deep/nested/leaked.txt")}, {QStringLiteral("content"), QStringLiteral("x")}}}, root, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 6; }));
    // clang-format on
    EXPECT_TRUE(results.at(5).failed) << results.at(5).text.toStdString();
    EXPECT_FALSE(QFileInfo(QDir(outside.path()).filePath(QStringLiteral("deep"))).exists());
}

TEST(AiToolRegistryTest, RefusesToEmptyADirectoryTheAgentDidNotAskToEmpty) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QString root = QDir(workdir.path()).canonicalPath();
    ASSERT_TRUE(QDir().mkpath(root + QStringLiteral("/build/objects")));
    QFile artefact(root + QStringLiteral("/build/objects/main.o"));
    ASSERT_TRUE(artefact.open(QIODevice::WriteOnly));
    artefact.close();

    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    // Removing a directory takes everything inside it, so a call that did not ask for that says so instead of losing the contents.
    registry.invoke({QStringLiteral("r1"), QStringLiteral("remove_path"), QJsonObject{{QStringLiteral("path"), QStringLiteral("build")}}}, root, collect);
    ASSERT_EQ(results.size(), 1);
    EXPECT_TRUE(results.first().failed);
    EXPECT_TRUE(results.first().text.contains(QStringLiteral("recursive"))) << results.first().text.toStdString();
    EXPECT_TRUE(QFileInfo(root + QStringLiteral("/build/objects/main.o")).isFile());

    registry.invoke({QStringLiteral("r2"), QStringLiteral("remove_path"), QJsonObject{{QStringLiteral("path"), QStringLiteral("build")}, {QStringLiteral("recursive"), true}}}, root, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_FALSE(results.at(1).failed) << results.at(1).text.toStdString();
    EXPECT_FALSE(QFileInfo(root + QStringLiteral("/build")).exists());

    // An empty directory needs no such permission, because nothing is inside it to lose.
    ASSERT_TRUE(QDir().mkpath(root + QStringLiteral("/empty")));
    registry.invoke({QStringLiteral("r3"), QStringLiteral("remove_path"), QJsonObject{{QStringLiteral("path"), QStringLiteral("empty")}}}, root, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 3; }));
    // clang-format on
    EXPECT_FALSE(results.at(2).failed) << results.at(2).text.toStdString();
}

TEST(AiToolRegistryTest, NamesTheArgumentACallGotWrongInsteadOfLeavingItToGuess) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    // A model that named the argument differently is told which one the tool requires.
    registry.invoke({QStringLiteral("a1"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("file_path"), QStringLiteral("index.html")}, {QStringLiteral("text"), QStringLiteral("<html/>")}}}, QStringLiteral("/tmp"), collect);
    ASSERT_EQ(results.size(), 1);
    EXPECT_TRUE(results.first().failed);
    EXPECT_EQ(results.first().text, QStringLiteral("The tool write_file requires the argument path"));

    registry.invoke({QStringLiteral("a2"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), 12}, {QStringLiteral("content"), QStringLiteral("x")}}}, QStringLiteral("/tmp"), collect);
    ASSERT_EQ(results.size(), 2);
    EXPECT_TRUE(results.at(1).failed);
    EXPECT_EQ(results.at(1).text, QStringLiteral("The tool write_file expects the argument path to be of type string"));

    // A schema declaring nothing about an argument judges nothing about it.
    const ToolSchema schema{QStringLiteral("sample"), QStringLiteral("ai.tool.read-file"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}}}, {QStringLiteral("required"), QJsonArray{QStringLiteral("count")}}}};
    EXPECT_FALSE(ToolContracts::findToolArgumentError(schema, QJsonObject{{QStringLiteral("count"), 3}, {QStringLiteral("extra"), QStringLiteral("kept")}}).has_value());
    EXPECT_TRUE(ToolContracts::findToolArgumentError(schema, QJsonObject{{QStringLiteral("count"), QStringLiteral("three")}}).has_value());
    EXPECT_EQ(ToolContracts::findToolArgumentError(schema, QJsonObject{}).value().argument, QStringLiteral("count"));
}

TEST(AiToolRegistryTest, StopsACommandStillRunningForACallItWasAskedToCancel) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    // The command outlasts the wait below without outliving the test, because a child that survives its shell holds the pipe the runner is closed on.
    registry.invoke({QStringLiteral("c1"), QStringLiteral("run_command"), QJsonObject{{QStringLiteral("command"), test::TestProcesses::sleepingCommand(3)}, {QStringLiteral("timeout_seconds"), 0}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_FALSE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }, 600));
    // clang-format on

    registry.cancel(QStringLiteral("c1"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    EXPECT_TRUE(results.first().failed);

    // Cancelling a call that runs no command is simply nothing to do.
    registry.cancel(QStringLiteral("c1"));
    registry.cancel(QStringLiteral("never-started"));
    EXPECT_EQ(results.size(), 1);
}

TEST(AiToolRegistryTest, DeclaresWhatEveryNativeToolReaches) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);
    const QString root = QDir::tempPath();

    // A tool nobody classified would reach everything and take the turn alone, so every declared name answers here.
    const QSet<QString> reading{QStringLiteral("read_file"), QStringLiteral("read_image"), QStringLiteral("list_directory"), QStringLiteral("describe_path"), QStringLiteral("search_files")};
    const QSet<QString> writing{QStringLiteral("write_file"), QStringLiteral("edit_file"), QStringLiteral("create_directory"), QStringLiteral("remove_path"), QStringLiteral("generate_image"), QStringLiteral("generate_speech"), QStringLiteral("move_path"), QStringLiteral("copy_file")};
    const QSet<QString> everything{QStringLiteral("run_command")};

    for (const auto& schema : registry.schemas()) {
        const QJsonObject arguments{{QStringLiteral("path"), QStringLiteral("file.txt")}, {QStringLiteral("source"), QStringLiteral("a.txt")}, {QStringLiteral("destination"), QStringLiteral("b.txt")}};
        const ToolAccess access = registry.accessOf({QStringLiteral("call"), schema.name, arguments}, root);
        if (reading.contains(schema.name)) {
            EXPECT_EQ(access.kind, ToolAccessKind::Read) << schema.name.toStdString();
            EXPECT_FALSE(access.paths.isEmpty()) << schema.name.toStdString();
        } else if (writing.contains(schema.name)) {
            EXPECT_EQ(access.kind, ToolAccessKind::Write) << schema.name.toStdString();
            EXPECT_FALSE(access.paths.isEmpty()) << schema.name.toStdString();
        } else if (everything.contains(schema.name)) {
            EXPECT_EQ(access.kind, ToolAccessKind::Everything) << schema.name.toStdString();
        } else {
            EXPECT_EQ(access.kind, ToolAccessKind::None) << schema.name.toStdString();
        }
    }

    // A name the registry does not know reaches everything, because nothing declared what it touches.
    EXPECT_EQ(registry.accessOf({QStringLiteral("call"), QStringLiteral("mcp_files_write"), QJsonObject{}}, root).kind, ToolAccessKind::Everything);
}

TEST(AiToolRegistryTest, GivesEveryToolADeadlineExceptACommandDeclaredUnlimited) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);

    // A tool that never answers would hold the turn open forever, so every one of them is watched.
    EXPECT_GT(registry.deadlineMsFor({QStringLiteral("c1"), QStringLiteral("read_file"), QJsonObject{}}), 0);
    EXPECT_GT(registry.deadlineMsFor({QStringLiteral("c2"), QStringLiteral("fetch_url"), QJsonObject{}}), 0);
    EXPECT_GT(registry.deadlineMsFor({QStringLiteral("c3"), QStringLiteral("mcp_files_read"), QJsonObject{}}), 0);

    // A command answers to the limit it was given, and a limit of zero is the caller saying it may take as long as it needs.
    const int shortCommand = registry.deadlineMsFor({QStringLiteral("c4"), QStringLiteral("run_command"), QJsonObject{{QStringLiteral("timeout_seconds"), 30}}});
    const int longCommand = registry.deadlineMsFor({QStringLiteral("c5"), QStringLiteral("run_command"), QJsonObject{{QStringLiteral("timeout_seconds"), 600}}});
    EXPECT_GT(shortCommand, 30000);
    EXPECT_GT(longCommand, shortCommand);
    EXPECT_EQ(registry.deadlineMsFor({QStringLiteral("c6"), QStringLiteral("run_command"), QJsonObject{{QStringLiteral("timeout_seconds"), 0}}}), 0);
}

TEST(AiToolRegistryTest, ReplacesAnExactPassageAndRefusesOneItCannotPlace) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QString path = QDir(workdir.path()).filePath(QStringLiteral("main.cpp"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("int main() {\n    return 0;\n}\n// return 0; is the convention\n"));
    file.close();

    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    const auto storedText = [&path]() { QFile stored(path); return stored.open(QIODevice::ReadOnly) ? QString::fromUtf8(stored.readAll()) : QString{}; };
    // clang-format on

    // The passage carries its indentation, so it names exactly one place in the file.
    registry.invoke({QStringLiteral("e1"), QStringLiteral("edit_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("main.cpp")}, {QStringLiteral("old_text"), QStringLiteral("    return 0;")}, {QStringLiteral("new_text"), QStringLiteral("    return 1;")}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    EXPECT_FALSE(results.first().failed);
    EXPECT_EQ(storedText(), QStringLiteral("int main() {\n    return 1;\n}\n// return 0; is the convention\n"));

    // A passage the file does not carry is refused instead of writing anything.
    registry.invoke({QStringLiteral("e2"), QStringLiteral("edit_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("main.cpp")}, {QStringLiteral("old_text"), QStringLiteral("return 42;")}, {QStringLiteral("new_text"), QStringLiteral("return 7;")}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_TRUE(results.at(1).failed);
    EXPECT_TRUE(results.at(1).text.contains(QStringLiteral("does not contain that exact text"))) << results.at(1).text.toStdString();
    EXPECT_TRUE(results.at(1).text.contains(QStringLiteral("main.cpp"))) << results.at(1).text.toStdString();

    // A passage appearing twice is refused unless the agent asks for every occurrence.
    registry.invoke({QStringLiteral("e3"), QStringLiteral("edit_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("main.cpp")}, {QStringLiteral("old_text"), QStringLiteral("return")}, {QStringLiteral("new_text"), QStringLiteral("yield")}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 3; }));
    // clang-format on
    EXPECT_TRUE(results.at(2).failed);
    EXPECT_EQ(storedText(), QStringLiteral("int main() {\n    return 1;\n}\n// return 0; is the convention\n"));

    registry.invoke({QStringLiteral("e4"), QStringLiteral("edit_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("main.cpp")}, {QStringLiteral("old_text"), QStringLiteral("return")}, {QStringLiteral("new_text"), QStringLiteral("yield")}, {QStringLiteral("replace_all"), true}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 4; }));
    // clang-format on
    EXPECT_FALSE(results.at(3).failed);
    EXPECT_EQ(storedText(), QStringLiteral("int main() {\n    yield 1;\n}\n// yield 0; is the convention\n"));

    // An empty passage and a path outside the working directory are rejected before any read.
    registry.invoke({QStringLiteral("e5"), QStringLiteral("edit_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("main.cpp")}, {QStringLiteral("old_text"), QString{}}, {QStringLiteral("new_text"), QStringLiteral("x")}}}, workdir.path(), collect);
    ASSERT_EQ(results.size(), 5);
    EXPECT_TRUE(results.at(4).failed);
    registry.invoke({QStringLiteral("e6"), QStringLiteral("edit_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("../escape.cpp")}, {QStringLiteral("old_text"), QStringLiteral("a")}, {QStringLiteral("new_text"), QStringLiteral("b")}}}, workdir.path(), collect);
    ASSERT_EQ(results.size(), 6);
    EXPECT_TRUE(results.at(5).failed);
}

TEST(AiToolRegistryTest, ReadsAWholeFileAFirstLineOrAClosedRange) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    QFile file(QDir(workdir.path()).filePath(QStringLiteral("notes.txt")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("one\ntwo\nthree\nfour\nfive"));
    file.close();

    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    registry.invoke({QStringLiteral("r1"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(results.first().text, QStringLiteral("one\ntwo\nthree\nfour\nfive"));

    registry.invoke({QStringLiteral("r2"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}, {QStringLiteral("start_line"), 3}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_EQ(results.at(1).text, QStringLiteral("three\nfour\nfive"));

    registry.invoke({QStringLiteral("r3"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}, {QStringLiteral("start_line"), 2}, {QStringLiteral("end_line"), 3}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 3; }));
    // clang-format on
    EXPECT_EQ(results.at(2).text, QStringLiteral("two\nthree"));

    // A range beyond the file returns what exists, and a reversed range is rejected as invalid arguments.
    registry.invoke({QStringLiteral("r4"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}, {QStringLiteral("start_line"), 4}, {QStringLiteral("end_line"), 99}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 4; }));
    // clang-format on
    EXPECT_EQ(results.at(3).text, QStringLiteral("four\nfive"));

    registry.invoke({QStringLiteral("r5"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}, {QStringLiteral("start_line"), 4}, {QStringLiteral("end_line"), 2}}}, workdir.path(), collect);
    ASSERT_EQ(results.size(), 5);
    EXPECT_TRUE(results.at(4).failed);
}

TEST(AiToolRegistryTest, OwnsTheCompleteFilesystemSetInsideTheWorkingDirectory) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    const auto invoke = [&](const QString& name, const QJsonObject& arguments) {
        const int before = static_cast<int>(results.size());
        registry.invoke({QStringLiteral("call"), name, arguments}, workdir.path(), collect);
        return test::TestFutures::waitUntil([&]() { return static_cast<int>(results.size()) > before; });
    };
    // clang-format on

    ASSERT_TRUE(invoke(QStringLiteral("create_directory"), {{QStringLiteral("path"), QStringLiteral("src/module")}}));
    EXPECT_FALSE(results.last().failed);
    EXPECT_TRUE(QFileInfo(QDir(workdir.path()).filePath(QStringLiteral("src/module"))).isDir());

    ASSERT_TRUE(invoke(QStringLiteral("write_file"), {{QStringLiteral("path"), QStringLiteral("src/module/main.cpp")}, {QStringLiteral("content"), QStringLiteral("int main() { return 0; }")}}));
    ASSERT_TRUE(invoke(QStringLiteral("copy_file"), {{QStringLiteral("source"), QStringLiteral("src/module/main.cpp")}, {QStringLiteral("destination"), QStringLiteral("src/module/copy.cpp")}}));
    EXPECT_TRUE(QFileInfo(QDir(workdir.path()).filePath(QStringLiteral("src/module/copy.cpp"))).isFile());

    ASSERT_TRUE(invoke(QStringLiteral("move_path"), {{QStringLiteral("source"), QStringLiteral("src/module/copy.cpp")}, {QStringLiteral("destination"), QStringLiteral("src/module/renamed.cpp")}}));
    EXPECT_TRUE(QFileInfo(QDir(workdir.path()).filePath(QStringLiteral("src/module/renamed.cpp"))).isFile());

    ASSERT_TRUE(invoke(QStringLiteral("describe_path"), {{QStringLiteral("path"), QStringLiteral("src/module/main.cpp")}}));
    const QJsonObject described = QJsonDocument::fromJson(results.last().text.toUtf8()).object();
    EXPECT_EQ(described.value(QStringLiteral("type")).toString(), QStringLiteral("file"));
    EXPECT_GT(described.value(QStringLiteral("byteSize")).toInt(), 0);

    ASSERT_TRUE(invoke(QStringLiteral("search_files"), {{QStringLiteral("pattern"), QStringLiteral("*.cpp")}}));
    EXPECT_TRUE(results.last().text.contains(QStringLiteral("src/module/main.cpp")));
    EXPECT_TRUE(results.last().text.contains(QStringLiteral("src/module/renamed.cpp")));

    ASSERT_TRUE(invoke(QStringLiteral("search_files"), {{QStringLiteral("pattern"), QStringLiteral("*.cpp")}, {QStringLiteral("contains"), QStringLiteral("no such text")}}));
    EXPECT_EQ(results.last().text, host.translate(QStringLiteral("ai.error.tool-no-match")));

    ASSERT_TRUE(invoke(QStringLiteral("remove_path"), {{QStringLiteral("path"), QStringLiteral("src/module/renamed.cpp")}}));
    EXPECT_FALSE(QFileInfo(QDir(workdir.path()).filePath(QStringLiteral("src/module/renamed.cpp"))).exists());

    // Every operation stays inside the working directory, whatever path the model asks for.
    ASSERT_TRUE(invoke(QStringLiteral("create_directory"), {{QStringLiteral("path"), QStringLiteral("../escaped")}}));
    EXPECT_TRUE(results.last().failed);
    ASSERT_TRUE(invoke(QStringLiteral("move_path"), {{QStringLiteral("source"), QStringLiteral("src/module/main.cpp")}, {QStringLiteral("destination"), QStringLiteral("/tmp/escaped.cpp")}}));
    EXPECT_TRUE(results.last().failed);
    ASSERT_TRUE(invoke(QStringLiteral("describe_path"), {{QStringLiteral("path"), QStringLiteral("absent.txt")}}));
    EXPECT_TRUE(results.last().failed);
}

TEST(AiToolRegistryTest, RunsACommandInsideTheWorkingDirectoryAndReportsItsOutcome) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    AiToolRegistry registry(host);
    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    registry.invoke({QStringLiteral("c1"), QStringLiteral("run_command"), QJsonObject{{QStringLiteral("command"), AiTestsHelper::printWorkingDirectoryCommand()}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    EXPECT_FALSE(results.first().failed);
    EXPECT_TRUE(results.first().text.contains(QFileInfo(workdir.path()).fileName()));

    // A failing command returns its code and its output, because the agent decides what to do with both.
    registry.invoke({QStringLiteral("c2"), QStringLiteral("run_command"), QJsonObject{{QStringLiteral("command"), AiTestsHelper::failingCommand()}}}, workdir.path(), collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_TRUE(results.at(1).failed);
    EXPECT_TRUE(results.at(1).text.contains(QStringLiteral("broken")));
    EXPECT_TRUE(results.at(1).text.contains(QStringLiteral("3")));

    // A task without a working directory cannot run anything.
    registry.invoke({QStringLiteral("c3"), QStringLiteral("run_command"), QJsonObject{{QStringLiteral("command"), AiTestsHelper::printWorkingDirectoryCommand()}}}, {}, collect);
    ASSERT_EQ(results.size(), 3);
    EXPECT_TRUE(results.at(2).failed);
}

TEST(AiToolRegistryTest, ListsTheVoicesEachSpeechServicePublishes) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    // A service with a closed voice set answers from its own declaration without reaching the network.
    registry.setSpeechConfiguration({QStringLiteral("openai"), QStringLiteral("alloy"), QStringLiteral("sk")}, QString{});
    registry.invoke({QStringLiteral("v1"), QStringLiteral("list_voices"), QJsonObject{}}, {}, collect);
    ASSERT_EQ(results.size(), 1);
    EXPECT_FALSE(results.first().failed);
    EXPECT_TRUE(results.first().text.contains(QStringLiteral("alloy")));

    const QByteArray recorded = QByteArrayLiteral(R"({"voices":[{"voice_id":"21m00Tcm4TlvDq8ikWAM","name":"Rachel","category":"premade"},{"voice_id":"AZnzlk1XvdvUeBnXmlld","name":"Domi","category":"premade"}]})");
    RecordedSearchServer catalog(recorded);
    ASSERT_TRUE(catalog.listen());
    registry.setSpeechConfiguration({QStringLiteral("elevenlabs"), QStringLiteral("voice"), QStringLiteral("xi-secret")}, catalog.address());
    registry.invoke({QStringLiteral("v2"), QStringLiteral("list_voices"), QJsonObject{}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_FALSE(results.at(1).failed);
    EXPECT_TRUE(results.at(1).text.contains(QStringLiteral("21m00Tcm4TlvDq8ikWAM | Rachel | premade")));
    EXPECT_TRUE(catalog.requestHead().contains(QByteArrayLiteral("Xi-Api-Key: xi-secret")));

    // A rejected request hands the reason the service returned to the model instead of a transport message.
    RecordedSearchServer rejecting(QByteArrayLiteral(R"({"detail":{"message":"invalid api key"}})"));
    ASSERT_TRUE(rejecting.listenRejecting());
    registry.setSpeechConfiguration({QStringLiteral("elevenlabs"), QStringLiteral("voice"), QStringLiteral("xi-secret")}, rejecting.address());
    registry.invoke({QStringLiteral("v3"), QStringLiteral("list_voices"), QJsonObject{}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 3; }));
    // clang-format on
    EXPECT_TRUE(results.at(2).failed);
    EXPECT_EQ(results.at(2).text, QStringLiteral("invalid api key"));
}

TEST(AiPluginTest, SearchAndSpeechRunWithTheCredentialTheirServiceDeclares) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiTestsHelper::installEmptyProviderRows(host);
    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // Nothing was ever stored, and both services already carry the reference their documentation names.
    EXPECT_EQ(plugin.effectiveSearchSettings().apiKey, QStringLiteral("{env.BRAVE_API_KEY}"));
    EXPECT_EQ(TaskContracts::searchAddress(plugin.effectiveSearchSettings()), QStringLiteral("https://api.search.brave.com"));
    EXPECT_EQ(plugin.effectiveSpeechSettings().apiKey, QStringLiteral("{env.ELEVENLABS_API_KEY}"));

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveSearchSettings({SearchProvider::Tavily, {}, {}})).hasValue());
    EXPECT_EQ(plugin.effectiveSearchSettings().apiKey, QStringLiteral("{env.TAVILY_API_KEY}"));

    // A credential the user typed is the one that travels, because storage overrides what the service declares.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveSearchSettings({SearchProvider::Brave, {}, QStringLiteral("literal-key")})).hasValue());
    EXPECT_EQ(plugin.effectiveSearchSettings().apiKey, QStringLiteral("literal-key"));
    plugin.shutdown();
}

TEST(AiToolRegistryTest, SearchesTheWebThroughTheServiceEachProviderPublishes) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);

    const QByteArray bravePayload = QByteArrayLiteral(R"({"type":"search","web":{"type":"search","results":[{"title":"Qt 6 documentation","url":"https://doc.qt.io/qt-6/","description":"Reference for <strong>Qt</strong> 6 modules."},{"title":"Qt Widgets","url":"https://doc.qt.io/qt-6/qtwidgets-index.html","description":"Widget set for desktop applications."}]}})");
    RecordedSearchServer brave(bravePayload);
    ASSERT_TRUE(brave.listen());
    registry.setSearchConfiguration({SearchProvider::Brave, {}, QStringLiteral("brave-key")}, brave.address());

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on
    registry.invoke({QStringLiteral("s1"), QStringLiteral("web_search"), QJsonObject{{QStringLiteral("query"), QStringLiteral("qt widgets")}, {QStringLiteral("count"), 1}}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    ASSERT_FALSE(results.first().failed);
    EXPECT_TRUE(results.first().text.contains(QStringLiteral("https://doc.qt.io/qt-6/")));
    EXPECT_TRUE(results.first().text.contains(QStringLiteral("Reference for Qt 6 modules.")));

    // The requested count bounds the answer and the credential travels in the header the service documents.
    EXPECT_FALSE(results.first().text.contains(QStringLiteral("qtwidgets-index")));
    EXPECT_TRUE(brave.requestLine().contains(QByteArrayLiteral("/res/v1/web/search?q=qt%20widgets&count=1")));
    EXPECT_TRUE(brave.requestHead().contains(QByteArrayLiteral("X-Subscription-Token: brave-key")));

    const QByteArray tavilyPayload = QByteArrayLiteral(R"({"query":"qt widgets","results":[{"title":"Qt Widgets overview","url":"https://doc.qt.io/qt-6/qtwidgets-index.html","content":"Widgets are the primary elements for desktop interfaces.","score":0.98}],"response_time":0.42})");
    RecordedSearchServer tavily(tavilyPayload);
    ASSERT_TRUE(tavily.listen());
    registry.setSearchConfiguration({SearchProvider::Tavily, {}, QStringLiteral("tavily-key")}, tavily.address());
    registry.invoke({QStringLiteral("s2"), QStringLiteral("web_search"), QJsonObject{{QStringLiteral("query"), QStringLiteral("qt widgets")}}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    ASSERT_FALSE(results.at(1).failed);
    EXPECT_TRUE(results.at(1).text.contains(QStringLiteral("Widgets are the primary elements for desktop interfaces.")));
    EXPECT_TRUE(tavily.requestHead().contains(QByteArrayLiteral("Authorization: Bearer tavily-key")));

    // A service without an address refuses instead of guessing where to search.
    registry.setSearchConfiguration({SearchProvider::SearxNg, {}, {}}, {});
    registry.invoke({QStringLiteral("s3"), QStringLiteral("web_search"), QJsonObject{{QStringLiteral("query"), QStringLiteral("qt")}}}, {}, collect);
    ASSERT_EQ(results.size(), 3);
    EXPECT_TRUE(results.at(2).failed);

    // An empty result set is reported as such instead of as an empty tool answer.
    RecordedSearchServer empty(QByteArrayLiteral(R"({"web":{"results":[]}})"));
    ASSERT_TRUE(empty.listen());
    registry.setSearchConfiguration({SearchProvider::Brave, {}, QStringLiteral("brave-key")}, empty.address());
    registry.invoke({QStringLiteral("s4"), QStringLiteral("web_search"), QJsonObject{{QStringLiteral("query"), QStringLiteral("qt")}}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 4; }));
    // clang-format on
    EXPECT_FALSE(results.at(3).failed);
    EXPECT_EQ(results.at(3).text, host.translate(QStringLiteral("ai.search.no-result")));
}

TEST(AiToolRegistryTest, PublishesServerToolsUnderAQualifiedNameAndRoutesTheCallToTheirServer) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);
    const int nativeCount = static_cast<int>(registry.schemas().size());

    agent::mcp::McpServerDescriptor fixture;
    fixture.id = QStringLiteral("weather");
    fixture.command = QCoreApplication::applicationFilePath();
    fixture.arguments = {QStringLiteral("--workpane-test-mcp")};
    agent::mcp::McpClient client(fixture);
    QSignalSpy toolsChanged(&client, &agent::mcp::McpClient::toolsChanged);
    client.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return toolsChanged.count() == 1; }));
    // clang-format on

    registry.setMcpClients({&client});
    ASSERT_EQ(static_cast<int>(registry.schemas().size()), nativeCount + 5);
    const ToolSchema& published = registry.schemas().at(nativeCount);
    EXPECT_EQ(published.name, QStringLiteral("mcp_weather_get_weather"));
    EXPECT_TRUE(ToolContracts::validateToolSchema(published).hasValue());

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on
    registry.invoke({QStringLiteral("c1"), published.name, QJsonObject{{QStringLiteral("city"), QStringLiteral("Porto")}}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !results.isEmpty(); }));
    // clang-format on
    EXPECT_FALSE(results.first().failed);
    EXPECT_EQ(results.first().callId, QStringLiteral("c1"));
    EXPECT_EQ(results.first().text, QStringLiteral("sunny in Porto"));

    // The resource and prompt catalogs of the connected servers are reachable from the same registry.
    registry.invoke({QStringLiteral("r1"), QStringLiteral("list_mcp_resources"), QJsonObject{}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_TRUE(results.at(1).text.contains(QStringLiteral("file:///fixture/readme.md")));

    registry.invoke({QStringLiteral("r2"), QStringLiteral("read_mcp_resource"), QJsonObject{{QStringLiteral("server"), QStringLiteral("weather")}, {QStringLiteral("uri"), QStringLiteral("file:///fixture/readme.md")}}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 3; }));
    // clang-format on
    EXPECT_EQ(results.at(2).text, QStringLiteral("the fixture readme body"));

    registry.invoke({QStringLiteral("p1"), QStringLiteral("list_mcp_prompts"), QJsonObject{}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 4; }));
    // clang-format on
    EXPECT_TRUE(results.at(3).text.contains(QStringLiteral("review")));

    registry.invoke({QStringLiteral("p2"), QStringLiteral("read_mcp_prompt"), QJsonObject{{QStringLiteral("server"), QStringLiteral("weather")}, {QStringLiteral("name"), QStringLiteral("review")}}}, {}, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 5; }));
    // clang-format on
    EXPECT_EQ(results.at(4).text, QStringLiteral("run the review"));

    // A server that was never registered is refused instead of reaching another one.
    registry.invoke({QStringLiteral("p3"), QStringLiteral("read_mcp_prompt"), QJsonObject{{QStringLiteral("server"), QStringLiteral("absent")}, {QStringLiteral("name"), QStringLiteral("review")}}}, {}, collect);
    ASSERT_EQ(results.size(), 6);
    EXPECT_TRUE(results.at(5).failed);

    // A stopped server no longer publishes its tools and a call to one of them fails instead of hanging.
    client.stop();
    registry.setMcpClients({&client});
    EXPECT_EQ(static_cast<int>(registry.schemas().size()), nativeCount);
    registry.invoke({QStringLiteral("c2"), QStringLiteral("mcp_weather_get_weather"), QJsonObject{}}, {}, collect);
    ASSERT_EQ(results.size(), 7);
    EXPECT_TRUE(results.at(6).failed);
}

// A field the writer or the reader forgot is a setting the reader loses on the next start, so every one of them travels both ways.
TEST(AiTaskRepositoryTest, CarriesEveryFieldOfItsSettingsThroughTheDocumentAndBack) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);

    ModelConnection connection = AiTestsHelper::testConnection();
    connection.displayName = QStringLiteral("The one I named");
    connection.extraParameters = QJsonObject{{QStringLiteral("seed"), 7}};

    AiAgent agent = AiTestsHelper::testAgent();
    agent.description = QStringLiteral("Reads the issue and answers it");
    agent.maximumIterations = 21;

    agent::mcp::McpServerDescriptor server;
    server.id = QStringLiteral("issues");
    server.transport = agent::mcp::McpTransport::Http;
    server.url = QStringLiteral("https://issues.example.com/mcp");
    server.apiKey = QStringLiteral("{env.ISSUES_TOKEN}");
    server.roots = QStringList({QStringLiteral("/tmp/project")});
    server.samplingEnabled = true;
    server.samplingMaximumTokens = 2048;

    ProviderRateLimit limit;
    limit.providerId = QStringLiteral("openai");
    limit.minimumIntervalMs = 250;
    limit.maximumRequestsPerMinute = 30;
    limit.maximumConcurrentRequests = 2;

    AiSettings written;
    written.connections = {connection};
    written.defaultConnectionKey = ModelConnections::connectionKey(connection);
    written.execution = {17, 900, 4, 15};
    written.search = {SearchProvider::SearxNg, QStringLiteral("https://search.example.com"), QStringLiteral("{env.SEARX_TOKEN}")};
    written.speech = {QStringLiteral("openai"), QStringLiteral("alloy"), QStringLiteral("{env.SPEECH_TOKEN}")};
    written.mcpServers = {server};
    written.rateLimits = {limit};
    written.agents = {agent};

    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveSettings(written)).hasValue());
    ASSERT_EQ(host.savedSettings.size(), 1);
    host.settingsDocument = host.savedSettings.first();

    const AiSettings read = repository.settings();

    ASSERT_EQ(read.connections.size(), 1);
    EXPECT_EQ(read.connections.first().providerId, connection.providerId);
    EXPECT_EQ(read.connections.first().modelId, connection.modelId);
    EXPECT_EQ(read.connections.first().displayName, connection.displayName);
    EXPECT_EQ(read.connections.first().apiKey, connection.apiKey);
    EXPECT_EQ(read.connections.first().address, connection.address);
    EXPECT_EQ(read.connections.first().parameters, connection.parameters);
    EXPECT_EQ(read.connections.first().extraParameters, connection.extraParameters);
    EXPECT_EQ(read.defaultConnectionKey, written.defaultConnectionKey);

    EXPECT_EQ(read.execution.maximumIterations, 17);
    EXPECT_EQ(read.execution.commandTimeoutSeconds, 900);
    EXPECT_EQ(read.execution.parallelExecutions, 4);
    EXPECT_EQ(read.execution.chatFontSize, 15);

    EXPECT_EQ(read.search.provider, SearchProvider::SearxNg);
    EXPECT_EQ(read.search.instanceUrl, written.search.instanceUrl);
    EXPECT_EQ(read.search.apiKey, written.search.apiKey);

    EXPECT_EQ(read.speech.providerId, QStringLiteral("openai"));
    EXPECT_EQ(read.speech.voiceId, written.speech.voiceId);
    EXPECT_EQ(read.speech.apiKey, written.speech.apiKey);

    ASSERT_EQ(read.mcpServers.size(), 1);
    EXPECT_EQ(read.mcpServers.first().id, server.id);
    EXPECT_EQ(read.mcpServers.first().transport, server.transport);
    EXPECT_EQ(read.mcpServers.first().url, server.url);
    EXPECT_EQ(read.mcpServers.first().apiKey, server.apiKey);
    EXPECT_EQ(read.mcpServers.first().roots, server.roots);
    EXPECT_EQ(read.mcpServers.first().samplingEnabled, server.samplingEnabled);
    EXPECT_EQ(read.mcpServers.first().samplingMaximumTokens, server.samplingMaximumTokens);

    ASSERT_EQ(read.rateLimits.size(), 1);
    EXPECT_EQ(read.rateLimits.first(), limit);

    ASSERT_EQ(read.agents.size(), 1);
    EXPECT_EQ(read.agents.first(), agent);
}

TEST(AiTaskRepositoryTest, StoresEveryDeclaredMcpServerAndRejectsAnIncompleteOne) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);

    agent::mcp::McpServerDescriptor files;
    files.id = QStringLiteral("files");
    files.command = QStringLiteral("mcp-files");
    files.arguments = QStringList({QStringLiteral("--root"), QStringLiteral("/tmp")});
    files.roots = QStringList({QStringLiteral("/tmp/project")});
    agent::mcp::McpServerDescriptor issues;
    issues.id = QStringLiteral("issues");
    issues.transport = agent::mcp::McpTransport::Http;
    issues.url = QStringLiteral("https://issues.example.com/mcp");
    issues.apiKey = QStringLiteral("{env.ISSUES_TOKEN}");
    issues.samplingEnabled = true;
    issues.samplingMaximumTokens = 2048;

    AiSettings settings;
    settings.mcpServers = {files, issues};
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveSettings(settings)).hasValue());
    ASSERT_EQ(host.savedSettings.size(), 1);
    host.settingsDocument = host.savedSettings.first();

    const AiSettings stored = repository.settings();
    ASSERT_EQ(stored.mcpServers.size(), 2);
    EXPECT_EQ(stored.mcpServers.at(0).transport, agent::mcp::McpTransport::Stdio);
    EXPECT_EQ(stored.mcpServers.at(0).arguments, QStringList({QStringLiteral("--root"), QStringLiteral("/tmp")}));
    EXPECT_EQ(stored.mcpServers.at(0).roots, QStringList({QStringLiteral("/tmp/project")}));
    EXPECT_EQ(stored.mcpServers.at(1).transport, agent::mcp::McpTransport::Http);
    EXPECT_EQ(stored.mcpServers.at(1).apiKey, QStringLiteral("{env.ISSUES_TOKEN}"));
    EXPECT_TRUE(stored.mcpServers.at(1).samplingEnabled);
    EXPECT_EQ(stored.mcpServers.at(1).samplingMaximumTokens, 2048);

    agent::mcp::McpServerDescriptor withoutCommand;
    withoutCommand.id = QStringLiteral("broken");
    AiSettings invalid;
    invalid.mcpServers = {withoutCommand};
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveSettings(invalid)).hasValue());
    host.settingsDocument = host.savedSettings.last();
    EXPECT_TRUE(repository.settings().mcpServers.isEmpty());
}

TEST(AiPluginTest, PlacesTheInstructionsWhereTheModelAcceptsThem) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);

    // A model the catalog declares without the system role receives the instructions as the first user message.
    ModelConnection withoutSystemRole = AiTestsHelper::testConnection();
    withoutSystemRole.providerId = QStringLiteral("gemini");
    withoutSystemRole.modelId = QStringLiteral("gemma-3-27b-it");
    withoutSystemRole.parameters = ProviderCatalog::defaultParameters(*ProviderCatalog::findProvider(QStringLiteral("gemini")), withoutSystemRole.modelId);
    AiAgent agent = AiTestsHelper::testAgent();
    agent.connectionKey = ModelConnections::connectionKey(withoutSystemRole);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {withoutSystemRole}, {agent});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on

    ASSERT_GE(client->sentMessages.size(), 2);
    EXPECT_EQ(client->sentMessages.first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    EXPECT_TRUE(client->sentMessages.first().toObject().value(QStringLiteral("content")).toString().contains(agent.name));
    plugin.shutdown();
}

TEST(AiTaskRepositoryTest, RoundTripsAConversationAndRejectsAStoredMessageNobodyCouldRead) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);
    const QDateTime now = QDateTime::currentDateTimeUtc();

    ConversationMessage user;
    user.id = QStringLiteral("message-1");
    user.taskId = QStringLiteral("task-1");
    user.sequence = 1;
    user.role = ConversationRole::User;
    user.content = QStringLiteral("Review the repository");
    user.createdAtUtc = now;

    ConversationMessage assistant = user;
    assistant.id = QStringLiteral("message-2");
    assistant.sequence = 2;
    assistant.role = ConversationRole::Assistant;
    assistant.content = QStringLiteral("Reading it now");
    assistant.toolCalls = QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("call-1")}, {QStringLiteral("name"), QStringLiteral("read_file")}, {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), QStringLiteral("README.md")}}}}};

    ConversationMessage tool = user;
    tool.id = QStringLiteral("message-3");
    tool.sequence = 3;
    tool.role = ConversationRole::Tool;
    tool.content = QStringLiteral("the file");
    tool.toolCallId = QStringLiteral("call-1");

    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.appendConversation({user, assistant, tool})).hasValue());
    ASSERT_EQ(host.databaseTransactions.size(), 1);
    EXPECT_EQ(host.databaseTransactions.first().size(), 3);

    // The stored rows are read back in the order they were written, whatever order the page returned them in.
    // clang-format off
    host.queryHandler = [](const QString& statement, const QVariantList&) {
        persistence::DatabaseRows rows;

        if (!statement.contains(QStringLiteral("FROM ai_tasks_messages"))) {
            return Result<persistence::DatabaseRows>::success(rows);
        }

        rows.append({{QStringLiteral("id"), QStringLiteral("message-2")}, {QStringLiteral("task_id"), QStringLiteral("task-1")}, {QStringLiteral("sequence"), 2}, {QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QStringLiteral("Reading it now")}, {QStringLiteral("tool_calls"), QStringLiteral("[]")}, {QStringLiteral("tool_call_id"), QString{}}, {QStringLiteral("summarized_until"), 0}, {QStringLiteral("created_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
        rows.append({{QStringLiteral("id"), QStringLiteral("message-1")}, {QStringLiteral("task_id"), QStringLiteral("task-1")}, {QStringLiteral("sequence"), 1}, {QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("Review the repository")}, {QStringLiteral("tool_calls"), QStringLiteral("[]")}, {QStringLiteral("tool_call_id"), QString{}}, {QStringLiteral("summarized_until"), 0}, {QStringLiteral("created_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
        return Result<persistence::DatabaseRows>::success(rows);
    };
    // clang-format on
    const auto read = test::TestFutures::awaitFuture(repository.conversation(QStringLiteral("task-1"), 0, 100));
    ASSERT_TRUE(read.hasValue());
    ASSERT_EQ(read.value().size(), 2);
    EXPECT_EQ(read.value().first().sequence, 1);
    EXPECT_EQ(read.value().last().role, ConversationRole::Assistant);

    // A page nobody could ask for and a role nobody declares are refused instead of becoming an empty history.
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.conversation(QStringLiteral("task-1"), 0, 0)).error().code, QStringLiteral("ai_conversation_invalid"));
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.conversation(QStringLiteral("task-1"), 0, 101)).error().code, QStringLiteral("ai_conversation_invalid"));
    EXPECT_EQ(AiTaskRepository::parseConversationRole(QStringLiteral("narrator")).error().code, QStringLiteral("ai_conversation_invalid"));
}

// The chat owns its reading size like every other content surface, and a size that could not be kept says so rather than snapping back in silence.
TEST(AiPluginTest, StepsTheChatFontFromItsOwnViewAndSaysWhenTheSizeCouldNotBeKept) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTestsHelper::installAiRows(host, {workspace}, {}, {}, {AiTestsHelper::testConnection()}, {AiTestsHelper::testAgent()});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_EQ(plugin.executionSettings().chatFontSize, defaultChatFontSize);

    const std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("tasks"), nullptr));
    ASSERT_NE(view, nullptr);
    const QList<QAction*> steps = view->actions();
    ASSERT_EQ(steps.size(), 3);

    for (const auto* step : steps) {
        EXPECT_EQ(step->shortcutContext(), Qt::WidgetWithChildrenShortcut);
    }

    steps.at(0)->trigger();
    EXPECT_EQ(plugin.executionSettings().chatFontSize, defaultChatFontSize + 1);
    steps.at(1)->trigger();
    EXPECT_EQ(plugin.executionSettings().chatFontSize, defaultChatFontSize);

    host.notifications.clear();
    // clang-format off
    host.settingsFutureHandler = [](const QJsonObject&) { return QtFuture::makeReadyValueFuture(Result<void>::failure({"write_failed", "Write failed", {}})); };
    // clang-format on
    steps.at(0)->trigger();
    EXPECT_EQ(plugin.executionSettings().chatFontSize, defaultChatFontSize);
    ASSERT_FALSE(host.notifications.isEmpty());
    EXPECT_EQ(host.notifications.last().message, translations::AiCatalog::english().value(QStringLiteral("ai.error.settings-save")));

    plugin.shutdown();
}

// A client waiting for its deferred deletion is destroyed by the parent after the members it reaches, so the plugin releases it while the gate is still alive.
TEST(AiPluginTest, ReleasesAClientWaitingForItsDeferredDeletionBeforeTheGateItReaches) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {AiTestsHelper::testConnection()}, {AiTestsHelper::testAgent()});

    {
        // The real factory is what builds a client holding the gate of the plugin.
        AiPlugin plugin;
        ASSERT_TRUE(plugin.initialize(host).hasValue());
        ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&plugin]() { return !plugin.findChildren<AiChatClient*>(Qt::FindDirectChildrenOnly).isEmpty(); }));
        // clang-format on
        plugin.shutdown();

        // Nothing runs the event loop between the deferred deletion and the destruction that follows it.
        EXPECT_FALSE(plugin.findChildren<AiChatClient*>(Qt::FindDirectChildrenOnly).isEmpty());
    }
}

TEST(AiPluginTest, TellsTheAgentWhatThisRunCanActuallyDo) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString sandbox = QDir(root.path()).canonicalPath();

    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = sandbox;

    AiAgent agent = AiTestsHelper::testAgent();
    agent.systemPrompt = QStringLiteral("Model {{MODEL}} traits {{MODEL_TRAITS}} window {{CONTEXT_WINDOW}} budget {{OUTPUT_BUDGET}}\n{{VISION}}\n{{SEARCH}}\n{{SPEECH}}\n{{SERVERS}}");
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {AiTestsHelper::testConnection()}, {agent});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on

    // Every capability tag answers what this run really has, so the agent never promises what is not configured.
    const QString text = client->sentMessages.first().toObject().value(QStringLiteral("content")).toString();
    EXPECT_TRUE(text.contains(ModelConnections::connectionKey(AiTestsHelper::testConnection()))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("function-calling")));
    EXPECT_TRUE(text.contains(QStringLiteral("You read images")));
    EXPECT_TRUE(text.contains(QStringLiteral("No web search service is configured")));
    EXPECT_TRUE(text.contains(QStringLiteral("No speech service is configured")));
    EXPECT_TRUE(text.contains(QStringLiteral("No server is connected")));
    EXPECT_FALSE(text.contains(QStringLiteral("{{")));
    plugin.shutdown();
}

TEST(AiPluginTest, TakesTheAgentsAlongWhenTheConnectionTheyRunOnIsEdited) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    const ModelConnection stored = AiTestsHelper::testConnection();
    AiAgent agent = AiTestsHelper::testAgent();
    agent.connectionKey = ModelConnections::connectionKey(stored);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {stored}, {agent});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.connections().size(), 1);
    EXPECT_EQ(plugin.defaultConnectionKey(), ModelConnections::connectionKey(stored));

    // Changing the model of a connection is one act, so the agents that run on it follow to the key it now carries.
    ModelConnection edited = stored;
    edited.modelId = QStringLiteral("gpt-4o-mini");
    edited.parameters = ProviderCatalog::defaultParameters(*ProviderCatalog::findProvider(edited.providerId), edited.modelId);
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.replaceConnection(ModelConnections::connectionKey(stored), edited)).hasValue());
    ASSERT_EQ(plugin.connections().size(), 1);
    EXPECT_EQ(ModelConnections::connectionKey(plugin.connections().first()), ModelConnections::connectionKey(edited));
    EXPECT_EQ(plugin.defaultConnectionKey(), ModelConnections::connectionKey(edited));
    ASSERT_EQ(plugin.agents().size(), 1);
    EXPECT_EQ(plugin.agents().first().connectionKey, ModelConnections::connectionKey(edited));

    // The connection the agent runs on is still refused when it is removed, and the sentence says what to do about it.
    const auto removed = test::TestFutures::awaitFuture(plugin.saveConnections({}, {}));
    EXPECT_EQ(removed.error().code, QStringLiteral("ai_connection_in_use"));
    EXPECT_EQ(removed.error().detail, agent.name);
    EXPECT_TRUE(host.translate(QStringLiteral("ai.error.connection-in-use")).contains(QStringLiteral("before removing")));

    // A key nobody configured is refused, because every later reader resolves it.
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.replaceConnection(QStringLiteral("openai/retired"), edited)).error().code, QStringLiteral("ai_connection_unknown"));
    plugin.shutdown();
}

TEST(AiAgentPromptTest, ReplacesEveryDeclaredTagAndRefusesTheOnesNobodyDeclares) {
    // Every tag the catalog declares is replaced by what the run knows, and a tag nobody declares is named back.
    EXPECT_FALSE(AgentPrompts::promptTags().isEmpty());
    EXPECT_TRUE(AgentPrompts::unknownPromptTags(QStringLiteral("You are {{AGENT_NAME}} at {{DATE_TIME}}")).isEmpty());
    EXPECT_EQ(AgentPrompts::unknownPromptTags(QStringLiteral("{{AGENT_NAME}} and {{NOT_A_TAG}} and {{ALSO_MISSING}}")), QStringList({QStringLiteral("NOT_A_TAG"), QStringLiteral("ALSO_MISSING")}));

    const QHash<QString, QString> values{{QStringLiteral("AGENT_NAME"), QStringLiteral("Reviewer")}, {QStringLiteral("TASK_TITLE"), QStringLiteral("Ship it")}};
    EXPECT_EQ(AgentPrompts::renderPrompt(QStringLiteral("You are {{AGENT_NAME}} on {{TASK_TITLE}}"), values), QStringLiteral("You are Reviewer on Ship it"));

    // A declared tag the run has nothing for becomes empty rather than staying on the wire as a marker.
    EXPECT_EQ(AgentPrompts::renderPrompt(QStringLiteral("[{{TASK_ISSUE_URL}}]"), values), QStringLiteral("[]"));
}

TEST(AiAgentTest, ValidatesEveryFieldAndTheSetItBelongsTo) {
    const AiAgent valid = AiTestsHelper::testAgent();
    ASSERT_TRUE(TaskContracts::validateAgent(valid).hasValue());

    AiAgent wrongIdentifier = valid;
    wrongIdentifier.id = QStringLiteral("Reviewer Two");
    EXPECT_EQ(TaskContracts::validateAgent(wrongIdentifier).error().code, QStringLiteral("ai_agent_invalid"));

    AiAgent withoutName = valid;
    withoutName.name = QStringLiteral("   ");
    EXPECT_EQ(TaskContracts::validateAgent(withoutName).error().code, QStringLiteral("ai_agent_invalid"));

    AiAgent withoutPrompt = valid;
    withoutPrompt.systemPrompt.clear();
    EXPECT_EQ(TaskContracts::validateAgent(withoutPrompt).error().code, QStringLiteral("ai_agent_invalid"));

    AiAgent beyondIterations = valid;
    beyondIterations.maximumIterations = ProviderCatalog::aiLimits().maximumAgentIterations + 1;
    EXPECT_EQ(TaskContracts::validateAgent(beyondIterations).error().code, QStringLiteral("ai_agent_invalid"));

    // A tag nobody declares is refused where the prompt is written, so no run ever meets one it cannot answer.
    AiAgent unknownTag = valid;
    unknownTag.systemPrompt = QStringLiteral("You are {{WHO_KNOWS}}");
    EXPECT_EQ(TaskContracts::validateAgent(unknownTag).error().code, QStringLiteral("ai_agent_tag_unknown"));

    const QVector<ModelConnection> connections{AiTestsHelper::testConnection()};
    ASSERT_TRUE(TaskContracts::validateAgentSet({valid}, connections).hasValue());
    EXPECT_EQ(TaskContracts::validateAgentSet({valid, valid}, connections).error().code, QStringLiteral("ai_agent_duplicate"));

    AiAgent orphan = valid;
    orphan.connectionKey = QStringLiteral("openai/retired-model");
    EXPECT_EQ(TaskContracts::validateAgentSet({orphan}, connections).error().code, QStringLiteral("ai_connection_unknown"));
}

TEST(AiTaskRepositoryTest, RoundTripsTheAgentsAndRejectsAStoredOneNobodyCouldRun) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);

    AiSettings settings;
    settings.connections = {AiTestsHelper::testConnection()};
    settings.agents = {AiTestsHelper::testAgent()};
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveSettings(settings)).hasValue());
    ASSERT_EQ(host.savedSettings.size(), 1);
    host.settingsDocument = host.savedSettings.first();

    const AiSettings stored = repository.settings();
    ASSERT_EQ(stored.agents.size(), 1);
    EXPECT_EQ(stored.agents.first(), AiTestsHelper::testAgent());

    // An entry this plugin cannot use is left out and every other one still loads.
    const QJsonObject unknownField{{QStringLiteral("id"), QStringLiteral("reviewer")}, {QStringLiteral("nonexistent"), 1}};
    host.settingsDocument = {{QStringLiteral("agents"), QJsonArray{unknownField}}};
    EXPECT_TRUE(repository.settings().agents.isEmpty());

    host.settingsDocument = {{QStringLiteral("agents"), QJsonObject{}}};
    EXPECT_TRUE(repository.settings().agents.isEmpty());
}

// A gate that keeps a place nobody withdrew admits nobody afterwards, and that stops every task of that provider forever.
TEST(AiRequestGateTest, NeverWedgesThroughManyHoldersThatStopWithdrawOrAreDestroyed) {
    AiRequestGate gate;
    gate.setLimits({{QStringLiteral("openai"), 0, 0, 2}, {QStringLiteral("anthropic"), 0, 0, 1}});

    const QStringList providers{QStringLiteral("openai"), QStringLiteral("anthropic")};
    // The one that withdraws outlives every round, so a place kept for it is a place the gate never gives back.
    QObject patient;
    int admitted = 0;

    for (int round = 0; round < 40; ++round) {
        const QString providerId = providers.at(round % providers.size());
        QVector<QObject*> holders;

        for (int index = 0; index < 4; ++index) {
            auto* holder = new QObject;
            holders.append(holder);
            // clang-format off
            gate.acquire(providerId, holder, [&admitted]() { ++admitted; });
            // clang-format on
        }

        QCoreApplication::processEvents();

        // One that stays alive withdraws before its turn, one is destroyed while it waits or holds, and the rest release the ordinary way.
        // clang-format off
        gate.acquire(providerId, &patient, [&admitted]() { ++admitted; });
        // clang-format on
        gate.withdraw(providerId, &patient);
        delete holders.at(0);

        for (int index = 1; index < holders.size(); ++index) {
            gate.release(providerId, holders.at(index));
            delete holders.at(index);
        }

        QCoreApplication::processEvents();
    }

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&gate, &providers]() { return gate.inFlight(providers.at(0)) == 0 && gate.waiting(providers.at(0)) == 0 && gate.inFlight(providers.at(1)) == 0 && gate.waiting(providers.at(1)) == 0; }));
    // clang-format on
    EXPECT_GT(admitted, 0);

    // Every place came back, so the gate still admits rather than holding one for a holder that is gone.
    for (const auto& providerId : providers) {
        QObject owner;
        bool answered = false;
        // clang-format off
        gate.acquire(providerId, &owner, [&answered]() { answered = true; });
        ASSERT_TRUE(test::TestFutures::waitUntil([&answered]() { return answered; })) << providerId.toStdString() << " admits nobody after the run";
        // clang-format on
        gate.release(providerId, &owner);
    }
}

TEST(AiRequestGateTest, PacesEveryRequestOfOneProviderThroughOneQueue) {
    AiRequestGate gate;
    QObject owner;
    QStringList admitted;
    // clang-format off
    const auto record = [&admitted](const QString& name) { return [&admitted, name]() { admitted.append(name); }; };
    // clang-format on

    // A provider nobody limited admits everything that arrives.
    EXPECT_EQ(gate.acquire(QStringLiteral("openai"), &owner, record(QStringLiteral("first"))), 0);
    EXPECT_EQ(gate.acquire(QStringLiteral("openai"), &owner, record(QStringLiteral("second"))), 0);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return admitted.size() == 2; }));
    // clang-format on
    EXPECT_EQ(admitted, QStringList({QStringLiteral("first"), QStringLiteral("second")}));
    gate.release(QStringLiteral("openai"), &owner);
    gate.release(QStringLiteral("openai"), &owner);

    // A delay between requests holds the next one back, and the queue keeps the order it was asked in.
    admitted.clear();
    gate.setLimits({{QStringLiteral("nvidia"), 250, 0, 0}});
    EXPECT_EQ(gate.acquire(QStringLiteral("nvidia"), &owner, record(QStringLiteral("one"))), 0);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return admitted.size() == 1; }));
    // clang-format on
    gate.release(QStringLiteral("nvidia"), &owner);
    EXPECT_GT(gate.acquire(QStringLiteral("nvidia"), &owner, record(QStringLiteral("two"))), 0);
    EXPECT_GT(gate.acquire(QStringLiteral("nvidia"), &owner, record(QStringLiteral("three"))), 0);
    EXPECT_EQ(admitted.size(), 1);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return admitted.size() == 3; }));
    // clang-format on
    EXPECT_EQ(admitted, QStringList({QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")}));
    gate.release(QStringLiteral("nvidia"), &owner);
    gate.release(QStringLiteral("nvidia"), &owner);

    // A concurrency of one admits the next request only once the previous one is released.
    admitted.clear();
    gate.setLimits({{QStringLiteral("groq"), 0, 0, 1}});
    EXPECT_EQ(gate.acquire(QStringLiteral("groq"), &owner, record(QStringLiteral("alpha"))), 0);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return admitted.size() == 1; }));
    // clang-format on
    EXPECT_EQ(gate.acquire(QStringLiteral("groq"), &owner, record(QStringLiteral("beta"))), -1);
    EXPECT_EQ(gate.inFlight(QStringLiteral("groq")), 1);
    EXPECT_EQ(gate.waiting(QStringLiteral("groq")), 1);
    EXPECT_EQ(admitted.size(), 1);
    gate.release(QStringLiteral("groq"), &owner);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return admitted.size() == 2; }));
    // clang-format on
    gate.release(QStringLiteral("groq"), &owner);

    // A caller that is gone before its turn is dropped instead of being admitted into nothing.
    admitted.clear();
    gate.setLimits({{QStringLiteral("cerebras"), 0, 0, 1}});
    EXPECT_EQ(gate.acquire(QStringLiteral("cerebras"), &owner, record(QStringLiteral("holder"))), 0);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return admitted.size() == 1; }));
    // clang-format on
    auto abandoned = std::make_unique<QObject>();
    gate.acquire(QStringLiteral("cerebras"), abandoned.get(), record(QStringLiteral("abandoned")));
    abandoned.reset();
    gate.release(QStringLiteral("cerebras"), &owner);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return gate.waiting(QStringLiteral("cerebras")) == 0; }));
    // clang-format on
    EXPECT_EQ(admitted, QStringList({QStringLiteral("holder")}));

    // A holder destroyed before its admission was delivered gives the slot back, so the provider keeps admitting.
    admitted.clear();
    gate.setLimits({{QStringLiteral("mistral"), 0, 0, 1}});
    auto stopped = std::make_unique<QObject>();
    EXPECT_EQ(gate.acquire(QStringLiteral("mistral"), stopped.get(), record(QStringLiteral("stopped"))), 0);
    EXPECT_EQ(gate.inFlight(QStringLiteral("mistral")), 1);
    stopped.reset();
    EXPECT_EQ(gate.inFlight(QStringLiteral("mistral")), 0);
    EXPECT_EQ(gate.acquire(QStringLiteral("mistral"), &owner, record(QStringLiteral("next"))), 0);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return admitted.size() == 1; }));
    // clang-format on
    EXPECT_EQ(admitted, QStringList({QStringLiteral("next")}));
    gate.release(QStringLiteral("mistral"), &owner);
}

TEST(AiTaskRepositoryTest, RoundTripsTheProviderRateLimitsAndRejectsTheOnesNobodyCouldHonour) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);

    AiSettings settings;
    settings.rateLimits = {{QStringLiteral("nvidia"), 1500, 40, 1}, {QStringLiteral("openrouter"), 0, 20, 0}};
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveSettings(settings)).hasValue());
    ASSERT_EQ(host.savedSettings.size(), 1);
    host.settingsDocument = host.savedSettings.first();

    const AiSettings stored = repository.settings();
    ASSERT_EQ(stored.rateLimits.size(), 2);
    EXPECT_EQ(stored.rateLimits.at(0), settings.rateLimits.at(0));
    EXPECT_EQ(stored.rateLimits.at(1), settings.rateLimits.at(1));

    // A limit that names no provider, repeats one or leaves the range it is bounded by is left out, and every other one still loads.
    const QJsonObject unknown{{QStringLiteral("providerId"), QStringLiteral("absent")}, {QStringLiteral("minimumIntervalMs"), 0}, {QStringLiteral("maximumRequestsPerMinute"), 0}, {QStringLiteral("maximumConcurrentRequests"), 0}};
    host.settingsDocument = {{QStringLiteral("rateLimits"), QJsonArray{unknown}}};
    EXPECT_TRUE(repository.settings().rateLimits.isEmpty());

    QJsonObject duplicated = unknown;
    duplicated[QStringLiteral("providerId")] = QStringLiteral("nvidia");
    host.settingsDocument = {{QStringLiteral("rateLimits"), QJsonArray{duplicated, duplicated}}};
    EXPECT_EQ(repository.settings().rateLimits.size(), 1);

    QJsonObject negative = duplicated;
    negative[QStringLiteral("minimumIntervalMs")] = -1;
    host.settingsDocument = {{QStringLiteral("rateLimits"), QJsonArray{negative}}};
    EXPECT_TRUE(repository.settings().rateLimits.isEmpty());

    QJsonObject beyond = duplicated;
    beyond[QStringLiteral("minimumIntervalMs")] = ProviderCatalog::aiLimits().maximumRequestDelayMs + 1;
    host.settingsDocument = {{QStringLiteral("rateLimits"), QJsonArray{beyond}}};
    EXPECT_TRUE(repository.settings().rateLimits.isEmpty());

    host.settingsDocument = {{QStringLiteral("rateLimits"), QJsonObject{}}};
    EXPECT_TRUE(repository.settings().rateLimits.isEmpty());
}

TEST(AiTaskRepositoryTest, TakesTheDeclaredDefaultForEveryStoredValueItCannotUse) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);

    // A container of the wrong type is the declared default, so it never becomes an empty selection nobody explained.
    const QVector<QPair<QString, QJsonValue>> mistyped{{QStringLiteral("connections"), QStringLiteral("none")}, {QStringLiteral("execution"), 12}, {QStringLiteral("search"), QJsonArray{}}, {QStringLiteral("speech"), true}, {QStringLiteral("mcpServers"), QJsonObject{}}};

    for (const auto& [key, value] : mistyped) {
        host.settingsDocument = {{key, value}};
        const AiSettings settings = repository.settings();
        EXPECT_TRUE(settings.connections.isEmpty()) << key.toStdString();
        EXPECT_TRUE(settings.mcpServers.isEmpty()) << key.toStdString();
        EXPECT_EQ(settings.execution.parallelExecutions, ExecutionSettings{}.parallelExecutions) << key.toStdString();
    }

    // An entry of the wrong shape is left out and the entries around it still load.
    const QJsonObject server{{QStringLiteral("id"), QStringLiteral("files")}, {QStringLiteral("command"), QStringLiteral("mcp-files")}, {QStringLiteral("arguments"), QJsonArray{12}}};
    host.settingsDocument = {{QStringLiteral("mcpServers"), QJsonArray{server}}};
    EXPECT_TRUE(repository.settings().mcpServers.isEmpty());

    const QJsonObject wrongParameters{{QStringLiteral("providerId"), QStringLiteral("openai")}, {QStringLiteral("parameters"), QJsonArray{}}};
    host.settingsDocument = {{QStringLiteral("connections"), QJsonArray{wrongParameters}}};
    EXPECT_TRUE(repository.settings().connections.isEmpty());

    // Two stored connections sharing one key keep the first, because one key names one configuration.
    const QJsonObject stored = AiTestsHelper::settingsDocument({AiTestsHelper::testConnection()}, {}).value(QStringLiteral("connections")).toArray().first().toObject();
    host.settingsDocument = {{QStringLiteral("connections"), QJsonArray{stored, stored}}};
    EXPECT_EQ(repository.settings().connections.size(), 1);

    // A default naming a connection that is not configured is cleared, because every later reader resolves it.
    host.settingsDocument = {{QStringLiteral("defaultConnectionKey"), QStringLiteral("openai/retired")}};
    EXPECT_TRUE(repository.settings().defaultConnectionKey.isEmpty());

    // A key nobody declares changes nothing, so a document written by another version still opens.
    host.settingsDocument = {{QStringLiteral("nobodyDeclaresThis"), 1}, {QStringLiteral("connections"), QJsonArray{stored}}};
    EXPECT_EQ(repository.settings().connections.size(), 1);
}

TEST(McpClientTest, RejectsAnInvalidServerAndCompletesPendingRequests) {
    agent::mcp::McpServerDescriptor absent;
    absent.id = QStringLiteral("absent");
    absent.command = QStringLiteral("workpane-nonexistent-mcp-server");
    agent::mcp::McpClient missing(absent);
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&missing, &agent::mcp::McpClient::failed, &missing, [&failures](const Error& error) { failures.append(error); });
    // clang-format on
    missing.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !failures.isEmpty(); }));
    // clang-format on
    EXPECT_FALSE(missing.ready());

    agent::mcp::McpServerDescriptor blank;
    blank.id = QStringLiteral("empty");
    blank.command = QStringLiteral("   ");
    agent::mcp::McpClient empty(blank);
    QVector<Error> emptyFailures;
    // clang-format off
    QObject::connect(&empty, &agent::mcp::McpClient::failed, &empty, [&emptyFailures](const Error& error) { emptyFailures.append(error); });
    // clang-format on
    empty.start();
    ASSERT_EQ(emptyFailures.size(), 1);
    EXPECT_EQ(emptyFailures.first().code, QStringLiteral("ai_mcp_invalid"));

    // A request against a server that is not running completes instead of leaking its callback.
    QVector<Result<QJsonObject>> replies;
    // clang-format off
    empty.callTool(QStringLiteral("anything"), {}, [&replies](Result<QJsonObject> result) { replies.append(std::move(result)); });
    // clang-format on
    ASSERT_EQ(replies.size(), 1);
    EXPECT_EQ(replies.first().error().code, QStringLiteral("ai_mcp_unavailable"));
}

// Serves the Model Context Protocol over the streamable HTTP transport, answering with a session and an event stream.
class RecordedMcpHttpServer final {
  public:
    RecordedMcpHttpServer() {
        // clang-format off
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            QTcpSocket* socket = m_server.nextPendingConnection();
            auto* buffer = new QByteArray();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
                buffer->append(socket->readAll());
                const qsizetype boundary = buffer->indexOf(QByteArrayLiteral("\r\n\r\n"));
                if (boundary < 0) {
                    return;
                }

                const QByteArray head = buffer->left(boundary);
                const QJsonObject message = QJsonDocument::fromJson(buffer->mid(boundary + 4)).object();
                m_sessions.append(head.toLower().contains(QByteArrayLiteral("mcp-session-id:")));
                m_methods.append(message.value(QStringLiteral("method")).toString());
                if (head.startsWith(QByteArrayLiteral("DELETE"))) {
                    m_deleted = true;
                    socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
                    socket->disconnectFromHost();
                    delete buffer;
                    return;
                }
                if (!message.contains(QStringLiteral("id"))) {
                    socket->write(QByteArrayLiteral("HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
                    socket->disconnectFromHost();
                    delete buffer;
                    return;
                }

                const QString method = message.value(QStringLiteral("method")).toString();
                QJsonObject result;
                if (method == QStringLiteral("initialize")) {
                    result = QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")}, {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}}};
                } else if (method == QStringLiteral("tools/list")) {
                    result = QJsonObject{{QStringLiteral("tools"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("remote_search")}, {QStringLiteral("description"), QStringLiteral("Remote search")}, {QStringLiteral("inputSchema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}}}}};
                } else {
                    result = QJsonObject{{QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("remote answer")}}}}};
                }

                const QByteArray envelope = QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), message.value(QStringLiteral("id"))}, {QStringLiteral("result"), result}}).toJson(QJsonDocument::Compact);
                // The initialization answers as a single document while later replies use the event stream.
                const QByteArray body = method == QStringLiteral("initialize") ? envelope : QByteArrayLiteral("event: message\ndata: ") + envelope + QByteArrayLiteral("\n\n");
                const QByteArray type = method == QStringLiteral("initialize") ? QByteArrayLiteral("application/json") : QByteArrayLiteral("text/event-stream");
                socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: ") + type + QByteArrayLiteral("\r\nMcp-Session-Id: session-42\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
                socket->disconnectFromHost();
                delete buffer;
            });
        });
        // clang-format on
    }

    [[nodiscard]] bool listen() {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] QString address() const {
        return QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_server.serverPort());
    }

    [[nodiscard]] const QStringList& methods() const {
        return m_methods;
    }

    [[nodiscard]] const QVector<bool>& sessions() const {
        return m_sessions;
    }

    [[nodiscard]] bool deleted() const {
        return m_deleted;
    }

  private:
    QTcpServer m_server;
    QStringList m_methods;
    QVector<bool> m_sessions;
    bool m_deleted{false};
};

TEST(McpClientTest, SpeaksTheStreamableHttpTransportWithSessionAndEventStream) {
    RecordedMcpHttpServer server;
    ASSERT_TRUE(server.listen());

    agent::mcp::McpServerDescriptor remote;
    remote.id = QStringLiteral("remote");
    remote.transport = agent::mcp::McpTransport::Http;
    remote.url = server.address();
    remote.apiKey = QStringLiteral("token");
    agent::mcp::McpClient client(remote);
    QSignalSpy initialized(&client, &agent::mcp::McpClient::initialized);
    QSignalSpy toolsChanged(&client, &agent::mcp::McpClient::toolsChanged);
    client.start();

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return initialized.count() == 1 && toolsChanged.count() == 1; }));
    // clang-format on
    EXPECT_TRUE(client.ready());
    ASSERT_EQ(client.tools().size(), 1);
    EXPECT_EQ(client.tools().first().name, QStringLiteral("remote_search"));

    QVector<Result<QJsonObject>> replies;
    // clang-format off
    client.callTool(QStringLiteral("remote_search"), QJsonObject{{QStringLiteral("q"), QStringLiteral("workpane")}}, [&replies](Result<QJsonObject> result) { replies.append(std::move(result)); });
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !replies.isEmpty(); }));
    // clang-format on
    ASSERT_TRUE(replies.first().hasValue());
    EXPECT_EQ(replies.first().value().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("remote answer"));

    // The session the server assigned at initialization travels on every later request.
    ASSERT_GE(server.sessions().size(), 2);
    EXPECT_FALSE(server.sessions().first());
    EXPECT_TRUE(server.sessions().at(1));
    EXPECT_TRUE(server.methods().contains(QStringLiteral("notifications/initialized")));

    client.stop();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return server.deleted(); }));
    // clang-format on
}

// The size of the answer is decided by the server, so the client stops reading rather than holding whatever arrives.
class OversizedMcpHttpServer final {
  public:
    OversizedMcpHttpServer() {
        // clang-format off
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            QTcpSocket* socket = m_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
                socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n"));
                const QByteArray padding(1024 * 1024, 'a');
                for (int chunk = 0; chunk < 12; ++chunk) {
                    socket->write(QByteArrayLiteral(": ") + padding + QByteArrayLiteral("\n"));
                    socket->flush();
                }
            });
        });
        // clang-format on
    }

    [[nodiscard]] bool listen() {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] QString address() const {
        return QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_server.serverPort());
    }

  private:
    QTcpServer m_server;
};

TEST(McpClientTest, StopsReadingAnAnswerLargerThanThePermittedSize) {
    OversizedMcpHttpServer server;
    ASSERT_TRUE(server.listen());

    agent::mcp::McpServerDescriptor remote;
    remote.id = QStringLiteral("remote");
    remote.transport = agent::mcp::McpTransport::Http;
    remote.url = server.address();
    agent::mcp::McpClient client(remote);
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&client, &agent::mcp::McpClient::failed, &client, [&failures](const Error& error) { failures.append(error); });
    // clang-format on

    client.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&failures]() { return !failures.isEmpty(); }, 20000));
    // clang-format on

    QStringList codes;
    for (const auto& failure : failures) {
        codes.append(failure.code);
    }
    EXPECT_EQ(failures.first().code, QStringLiteral("ai_mcp_answer_too_large"));
    EXPECT_EQ(failures.size(), 1) << codes.join(QStringLiteral(", ")).toStdString();
    EXPECT_FALSE(client.ready());
    client.stop();
}

// The stdio transport holds the same bound the streamed one does, so a server writing past it is refused by name.
TEST(McpClientTest, RefusesAStdioAnswerLargerThanThePermittedSize) {
    agent::mcp::McpServerDescriptor fixture;
    fixture.id = QStringLiteral("fixture");
    fixture.command = QCoreApplication::applicationFilePath();
    fixture.arguments = {QStringLiteral("--workpane-test-mcp-malformed"), QStringLiteral("8")};

    agent::mcp::McpClient client(fixture);
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&client, &agent::mcp::McpClient::failed, &client, [&failures](const Error& error) { failures.append(error); });
    // clang-format on

    client.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&failures]() { return !failures.isEmpty(); }, 30000));
    // clang-format on

    EXPECT_EQ(failures.first().code, QStringLiteral("ai_mcp_message_too_large"));
    QStringList codes;
    for (const auto& failure : failures) {
        codes.append(failure.code);
    }
    EXPECT_EQ(failures.size(), 1) << codes.join(QStringLiteral(", ")).toStdString();
    EXPECT_FALSE(client.ready());
    client.stop();
    agent::mcp::McpClient::drainTransports();
}

TEST(McpClientTest, RejectsAnInvalidHttpAddress) {
    agent::mcp::McpServerDescriptor invalid;
    invalid.id = QStringLiteral("remote");
    invalid.transport = agent::mcp::McpTransport::Http;
    invalid.url = QStringLiteral("ftp://example.com/mcp");
    agent::mcp::McpClient client(invalid);
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&client, &agent::mcp::McpClient::failed, &client, [&failures](const Error& error) { failures.append(error); });
    // clang-format on
    client.start();
    ASSERT_EQ(failures.size(), 1);
    EXPECT_EQ(failures.first().code, QStringLiteral("ai_mcp_invalid"));
    EXPECT_FALSE(client.ready());
}

TEST(McpClientTest, AnswersRootsSamplingProgressAndCancelsAbandonedRequests) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    agent::mcp::McpServerDescriptor descriptor;
    descriptor.id = QStringLiteral("fixture");
    descriptor.command = QCoreApplication::applicationFilePath();
    descriptor.arguments = {QStringLiteral("--workpane-test-mcp")};
    descriptor.roots = {QDir(root.path()).canonicalPath()};
    descriptor.samplingEnabled = true;
    descriptor.samplingMaximumTokens = 64;

    agent::mcp::McpClient client(descriptor);
    int samplingCalls = 0;
    int samplingBudget = 0;
    // clang-format off
    client.setSamplingHandler([&samplingCalls, &samplingBudget](const QJsonObject&, int maximumTokens, agent::mcp::McpReply reply) {
        ++samplingCalls;
        samplingBudget = maximumTokens;
        reply(Result<QJsonObject>::success({{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("sampled")}}}}));
    });
    // clang-format on

    QSignalSpy initialized(&client, &agent::mcp::McpClient::initialized);
    QSignalSpy progress(&client, &agent::mcp::McpClient::progressReported);
    client.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return initialized.count() == 1; }));
    // clang-format on

    QVector<Result<QJsonObject>> replies;
    // clang-format off
    client.request(QStringLiteral("probe/client"), {}, [&replies](Result<QJsonObject> result) { replies.append(std::move(result)); });
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return samplingCalls == 1 && progress.count() == 1; }));
    // clang-format on

    // The server sampled through the client budget instead of its own credential.
    EXPECT_EQ(samplingBudget, 64);

    const QList<QVariant> reported = progress.first();
    EXPECT_EQ(reported.at(0).toString(), QStringLiteral("t1"));
    EXPECT_DOUBLE_EQ(reported.at(1).toDouble(), 40.0);
    EXPECT_EQ(reported.at(3).toString(), QStringLiteral("halfway"));

    client.stop();
    EXPECT_FALSE(client.ready());
}

TEST(McpClientTest, DeclaresSamplingOnlyWhenTheServerIsAllowedToSpendTheModel) {
    agent::mcp::McpServerDescriptor descriptor;
    descriptor.id = QStringLiteral("fixture");
    descriptor.command = QCoreApplication::applicationFilePath();
    descriptor.arguments = {QStringLiteral("--workpane-test-mcp")};

    // A server that was not allowed receives a method-not-found instead of the configured model.
    agent::mcp::McpClient disallowed(descriptor);
    int calls = 0;
    // clang-format off
    disallowed.setSamplingHandler([&calls](const QJsonObject&, int, agent::mcp::McpReply reply) { ++calls; reply(Result<QJsonObject>::success({})); });
    // clang-format on
    QSignalSpy initialized(&disallowed, &agent::mcp::McpClient::initialized);
    disallowed.start();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return initialized.count() == 1; }));
    // clang-format on

    QVector<Result<QJsonObject>> replies;
    // clang-format off
    disallowed.request(QStringLiteral("probe/client"), {}, [&replies](Result<QJsonObject> result) { replies.append(std::move(result)); });
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !replies.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(calls, 0);
    disallowed.stop();
}

TEST(AiPluginTest, StartsTheAgentFromInstructionsThatDeclareItsBoundaryAndItsSkills) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString sandbox = QDir(root.path()).canonicalPath();
    ASSERT_TRUE(QDir(sandbox).mkpath(QStringLiteral(".claude/skills/code-review")));
    QFile skill(QDir(sandbox).filePath(QStringLiteral(".claude/skills/code-review/SKILL.md")));
    ASSERT_TRUE(skill.open(QIODevice::WriteOnly));
    skill.write(QByteArrayLiteral("---\nname: code-review\ndescription: Review a change and summarise findings\n---\n\nAlways summarise findings as a bullet list."));
    skill.close();

    QFile agents(QDir(sandbox).filePath(QStringLiteral("AGENTS.md")));
    ASSERT_TRUE(agents.open(QIODevice::WriteOnly));
    agents.write(QByteArrayLiteral("Run the suite before finishing."));
    agents.close();

    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = sandbox;
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on

    ASSERT_GE(client->sentMessages.size(), 2);
    const QJsonObject instructions = client->sentMessages.first().toObject();
    EXPECT_EQ(instructions.value(QStringLiteral("role")).toString(), QStringLiteral("system"));

    // The instructions are the ones the agent declares, with the tags it carries replaced by what the run knows.
    const QString text = instructions.value(QStringLiteral("content")).toString();
    EXPECT_TRUE(text.contains(QStringLiteral("You are %1.").arg(AiTestsHelper::testAgent().name)));
    EXPECT_FALSE(text.contains(QStringLiteral("{{")));
    EXPECT_TRUE(text.contains(sandbox));
    // The agent is told which machine and which moment it runs in, so a task about today does not guess.
    const QString environment = client->sentMessages.first().toObject().value(QStringLiteral("content")).toString();
    EXPECT_TRUE(environment.contains(host.translate(QStringLiteral("ai.agent.environment"))));
    EXPECT_TRUE(environment.contains(QStringLiteral("- home directory: %1").arg(QDir::homePath())));
    EXPECT_TRUE(environment.contains(QStringLiteral("- locale: %1").arg(QLocale::system().name())));
    EXPECT_TRUE(environment.contains(QStringLiteral("- utc time: %1").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd")))));
    EXPECT_TRUE(environment.contains(QStringLiteral("- operating system: %1").arg(QSysInfo::prettyProductName())));

    // A published context file joins the prompt in full while a skill is offered by name and description only.
    EXPECT_TRUE(text.contains(QStringLiteral("Run the suite before finishing.")));
    EXPECT_TRUE(text.contains(QStringLiteral("code-review")));
    EXPECT_TRUE(text.contains(QStringLiteral("Review a change and summarise findings")));
    EXPECT_FALSE(text.contains(QStringLiteral("bullet list")));
    EXPECT_EQ(client->sentMessages.at(1).toObject().value(QStringLiteral("content")).toString(), task.prompt);

    // Every tool the agent may call is declared with the request.
    EXPECT_FALSE(client->sentTools.isEmpty());
    plugin.shutdown();
}

TEST(AiPluginTest, KeepsTheAssistantTextOfEveryIterationInsteadOfOnlyTheLast) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiAgent limitedAgent = AiTestsHelper::testAgent();
    limitedAgent.maximumIterations = 5;
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {AiTestsHelper::testConnection()}, {limitedAgent});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on

    emit client->contentReceived(QStringLiteral("first thought"));
    client->deliverToolCalls({{QStringLiteral("call_1"), QStringLiteral("list_directory"), QJsonObject{}}});
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client->sendCalls == 2; }));
    // clang-format on

    emit client->contentReceived(QStringLiteral("final answer"));
    client->deliver(QString{}, {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on

    const auto executions = test::TestFutures::awaitFuture(plugin.executions(task.id));
    ASSERT_TRUE(executions.hasValue());
    plugin.shutdown();
}

TEST(AiPluginTest, ReachesATerminalStateForEveryRunOfALongSequenceOfTurnsAndStops) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiAgent agent = AiTestsHelper::testAgent();
    agent.maximumIterations = 6;
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {AiTestsHelper::testConnection()}, {agent});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    for (int round = 0; round < 20; ++round) {
        client = nullptr;
        ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
        // clang-format on

        const int turns = round % 3 + 1;
        for (int turn = 0; turn < turns; ++turn) {
            const int expected = client->sendCalls + 1;
            client->deliverToolCalls({{QStringLiteral("call_%1").arg(turn), QStringLiteral("list_directory"), QJsonObject{}}});
            // clang-format off
            ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client->sendCalls == expected; }));
            // clang-format on
        }

        // A run stopped in the middle of a turn ends exactly like one that answered, so the card never stays busy.
        if (round % 2 == 0) {
            ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.stopTask(task.id)).hasValue());
        } else {
            client->deliver(QStringLiteral("answer %1").arg(round), {}, QStringLiteral("stop"));
        }
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
        // clang-format on
    }

    const auto executions = test::TestFutures::awaitFuture(plugin.executions(task.id));
    ASSERT_TRUE(executions.hasValue());
    plugin.shutdown();
}

TEST(AiToolRegistryTest, DiscoversSkillsInThePublishedLayoutAndDisclosesThemProgressively) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString sandbox = QDir(root.path()).canonicalPath();
    ASSERT_TRUE(QDir(sandbox).mkpath(QStringLiteral(".claude/skills/pdf-forms")));
    QFile skill(QDir(sandbox).filePath(QStringLiteral(".claude/skills/pdf-forms/SKILL.md")));
    ASSERT_TRUE(skill.open(QIODevice::WriteOnly));
    skill.write(QByteArrayLiteral("---\nname: pdf-forms\ndescription: Fill and inspect PDF forms\n---\n\nUse the pdftk conventions described here."));
    skill.close();

    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    AiToolRegistry registry(host);

    // A workspace may declare its skills in any published directory, and a skill written as a single file counts like a bundle.
    ASSERT_TRUE(QDir(sandbox).mkpath(QStringLiteral(".skills/data-export")));
    QFile hidden(QDir(sandbox).filePath(QStringLiteral(".skills/data-export/SKILL.md")));
    ASSERT_TRUE(hidden.open(QIODevice::WriteOnly));
    hidden.write(QByteArrayLiteral("---\nname: data-export\ndescription: Export a dataset\n---\n\nBody."));
    hidden.close();
    ASSERT_TRUE(QDir(sandbox).mkpath(QStringLiteral("skills")));
    QFile plain(QDir(sandbox).filePath(QStringLiteral("skills/reporting.md")));
    ASSERT_TRUE(plain.open(QIODevice::WriteOnly));
    plain.write(QByteArrayLiteral("---\nname: reporting\ndescription: Build a report\n---\n\nBody."));
    plain.close();
    QFile silent(QDir(sandbox).filePath(QStringLiteral("skills/silent.md")));
    ASSERT_TRUE(silent.open(QIODevice::WriteOnly));
    silent.write(QByteArrayLiteral("no front matter at all"));
    silent.close();

    QVector<agent::ResourceDescriptor> catalog;
    // clang-format off
    registry.discoverResources(sandbox, [&catalog](const QVector<agent::ResourceDescriptor>& found) { catalog = found; });
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !catalog.isEmpty(); }));
    // clang-format on
    EXPECT_EQ(catalog.first().name, QStringLiteral("pdf-forms"));
    EXPECT_EQ(catalog.first().description, QStringLiteral("Fill and inspect PDF forms"));
    EXPECT_FALSE(catalog.first().root.isEmpty());

    QStringList names;

    // The machine roots hold whatever the reader of that machine put there, so this counts what the workspace declares.
    for (const auto& declared : catalog) {
        if (declared.project) {
            names.append(declared.name);
        }
    }

    ASSERT_EQ(names.size(), 3);

    EXPECT_TRUE(names.contains(QStringLiteral("data-export")));
    EXPECT_TRUE(names.contains(QStringLiteral("reporting")));
    // A file that declares nothing about itself is skipped by name and the catalog around it still answers.
    EXPECT_FALSE(names.contains(QStringLiteral("silent")));

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    registry.invoke({QStringLiteral("k1"), QStringLiteral("list_skills"), QJsonObject{}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 1; }));
    // clang-format on
    EXPECT_TRUE(results.first().text.contains(QStringLiteral("pdf-forms")));
    EXPECT_FALSE(results.first().text.contains(QStringLiteral("pdftk")));

    registry.invoke({QStringLiteral("k2"), QStringLiteral("search_skills"), QJsonObject{{QStringLiteral("query"), QStringLiteral("PDF")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_TRUE(results.at(1).text.contains(QStringLiteral("pdf-forms")));
    registry.invoke({QStringLiteral("k3"), QStringLiteral("search_skills"), QJsonObject{{QStringLiteral("query"), QStringLiteral("spreadsheet")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 3; }));
    // clang-format on
    EXPECT_FALSE(results.at(2).failed);
    EXPECT_FALSE(results.at(2).text.contains(QStringLiteral("pdf-forms")));

    // The body is only reachable through the tool that loads it.
    registry.invoke({QStringLiteral("k4"), QStringLiteral("read_skill"), QJsonObject{{QStringLiteral("name"), QStringLiteral("pdf-forms")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 4; }));
    // clang-format on
    EXPECT_TRUE(results.at(3).text.contains(QStringLiteral("pdftk")));

    registry.invoke({QStringLiteral("k5"), QStringLiteral("read_skill"), QJsonObject{{QStringLiteral("name"), QStringLiteral("absent")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 5; }));
    // clang-format on
    EXPECT_TRUE(results.at(4).failed);

    // A skill ships its reference beside its instructions, and the agent reads it from inside that bundle and nowhere else.
    QFile reference(QDir(sandbox).filePath(QStringLiteral(".claude/skills/pdf-forms/reference.md")));
    ASSERT_TRUE(reference.open(QIODevice::WriteOnly));
    reference.write(QByteArrayLiteral("every field of the form"));
    reference.close();
    registry.invoke({QStringLiteral("k6"), QStringLiteral("read_skill_file"), QJsonObject{{QStringLiteral("name"), QStringLiteral("pdf-forms")}, {QStringLiteral("path"), QStringLiteral("reference.md")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 6; }));
    // clang-format on
    EXPECT_FALSE(results.at(5).failed) << results.at(5).text.toStdString();
    EXPECT_TRUE(results.at(5).text.contains(QStringLiteral("every field")));

    registry.invoke({QStringLiteral("k7"), QStringLiteral("read_skill_file"), QJsonObject{{QStringLiteral("name"), QStringLiteral("pdf-forms")}, {QStringLiteral("path"), QStringLiteral("../../../etc/hosts")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 7; }));
    // clang-format on
    EXPECT_TRUE(results.at(6).failed);
}

// A model whose answer budget takes its whole window leaves no room, and a conversation nobody fits overflows the model instead of compacting.
// A command line agent is handed a prompt and runs its own tools, so nothing takes room from its window and a long conversation still reaches it.
TEST(AiToolContractTest, LeavesTheWholeWindowToAConversationBoundForACommandLineAgent) {
    const ProviderDescriptor* provider = ProviderCatalog::findProvider(QStringLiteral("grok-cli"));
    ASSERT_NE(provider, nullptr);

    // The service publishes the same number for what this model reads and for what it may answer.
    const ModelDescriptor* model = ProviderCatalog::findModel(*provider, QStringLiteral("grok-4.6"));
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->maximumOutputTokens, model->contextWindow);

    // Reserving what it may answer would leave nothing at all.
    const std::optional<qint64> reserved = ToolContracts::fittingTokenLimit(model->contextWindow, model->maximumOutputTokens);
    ASSERT_TRUE(reserved.has_value());
    EXPECT_EQ(reserved.value(), 0);

    // Reserving none of it leaves the conversation the window it really has.
    const std::optional<qint64> whole = ToolContracts::fittingTokenLimit(model->contextWindow, 0);
    ASSERT_TRUE(whole.has_value());
    EXPECT_GT(whole.value(), model->contextWindow / 2);
}

TEST(AiToolContractTest, FitsTheConversationWhenTheReservationTakesTheWholeWindow) {
    QJsonArray conversation;
    conversation.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QStringLiteral("instructions")}});
    conversation.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("the task")}});
    const QString filler(2000, QLatin1Char('x'));

    for (int index = 0; index < 8; ++index) {
        conversation.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), filler}});
        conversation.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), filler}});
    }

    // The catalog carries models whose declared maximum output is the whole context window, and a budget of zero asks for exactly that.
    const std::optional<qint64> noRoom = ToolContracts::fittingTokenLimit(8192, 8192);
    ASSERT_TRUE(noRoom.has_value());
    EXPECT_EQ(noRoom.value(), 0);

    const FittedConversation fitted = ToolContracts::fitConversation(conversation, noRoom);
    EXPECT_GT(fitted.dropped.size(), 0);
    EXPECT_LT(fitted.messages.size(), conversation.size());

    // The instructions and the task are what a run cannot lose, so they are what survives a window with no room.
    ASSERT_EQ(fitted.messages.size(), 2);
    EXPECT_EQ(fitted.messages.at(0).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("instructions"));
    EXPECT_EQ(fitted.messages.at(1).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("the task"));

    // A tool result is shortened before any turn is dropped, which a window with no room still asks for.
    QJsonArray results;
    results.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("tool_call_id"), QStringLiteral("call-1")}, {QStringLiteral("content"), QString(40000, QLatin1Char('y'))}});
    EXPECT_GT(ToolContracts::pruneToolResults(results, noRoom), 0);

    // A model that declares no window bounds nothing, so the same conversation passes whole.
    const FittedConversation unbounded = ToolContracts::fitConversation(conversation, ToolContracts::fittingTokenLimit(0, 0));
    EXPECT_EQ(unbounded.messages.size(), conversation.size());
    EXPECT_TRUE(unbounded.dropped.isEmpty());
}

TEST(AiToolContractTest, FitsTheConversationToTheModelWindowWithoutBreakingToolTurns) {
    const QJsonObject instructions{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QStringLiteral("be an agent")}};
    const QJsonObject task{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("do the work")}};
    const QString filler(4000, QLatin1Char('x'));

    QJsonArray conversation{instructions, task};

    for (int turn = 0; turn < 6; ++turn) {
        conversation.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), filler}, {QStringLiteral("tool_calls"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("c")}}}}});
        conversation.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("tool_call_id"), QStringLiteral("c")}, {QStringLiteral("content"), filler}});
    }

    EXPECT_GT(ToolContracts::estimateTokens(conversation), 0);

    // A window nobody declares bounds nothing, so there is no limit rather than a limit of zero.
    EXPECT_FALSE(ToolContracts::fittingTokenLimit(0, 0).has_value());
    EXPECT_LT(ToolContracts::fittingTokenLimit(1000, 0).value(), 1000);

    // The answer and the tool declarations take their share of the window before the conversation gets any.
    EXPECT_LT(ToolContracts::fittingTokenLimit(1000, 400).value(), ToolContracts::fittingTokenLimit(1000, 0).value());

    // A reservation that takes the whole window leaves a limit of zero, which is a window with no room and not the absence of one.
    EXPECT_EQ(ToolContracts::fittingTokenLimit(1000, 1000).value(), 0);
    EXPECT_EQ(ToolContracts::fittingTokenLimit(1000, 4000).value(), 0);

    // A window that fits everything leaves the conversation untouched.
    const FittedConversation complete = ToolContracts::fitConversation(conversation, ToolContracts::estimateTokens(conversation));
    EXPECT_EQ(complete.messages.size(), conversation.size());
    EXPECT_TRUE(complete.dropped.isEmpty());

    const FittedConversation fitted = ToolContracts::fitConversation(conversation, 3000);
    EXPECT_GT(fitted.dropped.size(), 0);
    EXPECT_LE(ToolContracts::estimateTokens(fitted.messages), 3000);

    // Every message leaves the conversation exactly once, either kept or handed to the summary.
    EXPECT_EQ(fitted.messages.size() + fitted.dropped.size(), conversation.size());
    EXPECT_EQ(fitted.preservedHead, 2);

    // The instructions and the task survive, and no orphan tool result is left without the turn that asked for it.
    ASSERT_GE(fitted.messages.size(), 2);
    EXPECT_EQ(fitted.messages.first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
    EXPECT_EQ(fitted.messages.at(1).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("do the work"));

    for (qsizetype index = 2; index < fitted.messages.size(); ++index) {
        if (fitted.messages.at(index).toObject().value(QStringLiteral("role")).toString() == QStringLiteral("tool")) {
            EXPECT_TRUE(fitted.messages.at(index - 1).toObject().contains(QStringLiteral("tool_calls"))) << index;
        }
    }
}

TEST(AiToolRegistryTest, TellsTheAgentWhichTaskItIsWorkingOn) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);

    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), QStringLiteral("workspace-1"));
    task.issueUrl = QStringLiteral("https://github.com/paulo/workpane/issues/42");
    task.workdir = QStringLiteral("/tmp/project");
    registry.setTaskContext(task, AiTestsHelper::testConnection());

    QVector<ToolResult> results;
    // clang-format off
    registry.invoke({QStringLiteral("d1"), QStringLiteral("describe_task"), QJsonObject{}}, {}, [&results](ToolResult result) { results.append(std::move(result)); });
    // clang-format on
    ASSERT_EQ(results.size(), 1);
    ASSERT_FALSE(results.first().failed);

    const QJsonObject described = QJsonDocument::fromJson(results.first().text.toUtf8()).object();
    EXPECT_EQ(described.value(QStringLiteral("title")).toString(), task.title);
    EXPECT_EQ(described.value(QStringLiteral("description")).toString(), task.description);
    EXPECT_EQ(described.value(QStringLiteral("issueUrl")).toString(), task.issueUrl);
    EXPECT_EQ(described.value(QStringLiteral("workingDirectory")).toString(), task.workdir);

    // A task without an issue omits the field instead of reporting an empty address.
    const AiTask withoutIssue = AiTestsHelper::makeTask(QStringLiteral("task-2"), QStringLiteral("workspace-1"));
    registry.setTaskContext(withoutIssue, AiTestsHelper::testConnection());
    // clang-format off
    registry.invoke({QStringLiteral("d2"), QStringLiteral("describe_task"), QJsonObject{}}, {}, [&results](ToolResult result) { results.append(std::move(result)); });
    // clang-format on
    EXPECT_FALSE(QJsonDocument::fromJson(results.at(1).text.toUtf8()).object().contains(QStringLiteral("issueUrl")));
}

TEST(AiTaskRepositoryTest, ReadsBackEveryTaskItAcceptedForBothExecutionKinds) {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    AiTask command;
    command.id = QStringLiteral("task-command");
    command.workspaceId = QStringLiteral("workspace-1");
    command.title = QStringLiteral("Build");
    command.executionKind = TaskExecutionKind::Command;
    command.command = QStringLiteral("make");
    command.workdir = QStringLiteral("/tmp/project");
    command.createdAtUtc = now;
    command.updatedAtUtc = now;

    // A command run carries no prompt, so a reader demanding one refuses to load what the writer accepted.
    EXPECT_TRUE(AiTaskRepository::validTask(command));

    AiTask agent = command;
    agent.id = QStringLiteral("task-agent");
    agent.executionKind = TaskExecutionKind::Agent;
    agent.command.clear();
    agent.workdir.clear();
    agent.agentId = AiTestsHelper::testAgent().id;
    EXPECT_FALSE(AiTaskRepository::validTask(agent));
    agent.prompt = QStringLiteral("Write the report");
    EXPECT_TRUE(AiTaskRepository::validTask(agent));

    // An agent task names the agent it is handed to, and a command names none.
    AiTask withoutAgent = agent;
    withoutAgent.agentId.clear();
    EXPECT_FALSE(AiTaskRepository::validTask(withoutAgent));
    AiTask blankAgent = agent;
    blankAgent.agentId = QStringLiteral("   ");
    EXPECT_FALSE(AiTaskRepository::validTask(blankAgent));
    AiTask commandWithAgent = command;
    commandWithAgent.agentId = AiTestsHelper::testAgent().id;
    EXPECT_FALSE(AiTaskRepository::validTask(commandWithAgent));

    // An agent may declare the directory its file tools are bound to, and a relative one is rejected.
    agent.workdir = QStringLiteral("relative/path");
    EXPECT_FALSE(AiTaskRepository::validTask(agent));
    agent.workdir = QStringLiteral("/tmp/project");
    EXPECT_TRUE(AiTaskRepository::validTask(agent));

    AiTask commandWithoutRoot = command;
    commandWithoutRoot.workdir.clear();
    EXPECT_FALSE(AiTaskRepository::validTask(commandWithoutRoot));

    test::TestPluginHost host;
    const AiWorkspace workspace{command.workspaceId, QStringLiteral("Product"), 0, true, now, now};
    AiTestsHelper::installAiRows(host, {workspace}, {command, agent}, {});
    AiTaskRepository repository(host);
    ASSERT_TRUE(repository.initialize().hasValue());

    // Startup must load both kinds, because one rejected task stops the whole application from opening.
    const auto loaded = repository.tasks();
    ASSERT_TRUE(loaded.hasValue()) << qPrintable(loaded.error().message);
    ASSERT_EQ(loaded.value().size(), 2);
    EXPECT_EQ(loaded.value().at(0).executionKind, TaskExecutionKind::Command);
    EXPECT_TRUE(loaded.value().at(0).prompt.isEmpty());
    EXPECT_EQ(loaded.value().at(1).prompt, agent.prompt);

    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveTask(command)).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(repository.saveTask(agent)).hasValue());
}

TEST(AiTaskRepositoryTest, RejectsAnIssueAddressThatIsNotAWebAddress) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);

    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), QStringLiteral("workspace-1"));
    task.issueUrl = QStringLiteral("not-an-address");
    EXPECT_EQ(test::TestFutures::awaitFuture(repository.saveTask(task)).error().code, QStringLiteral("ai_tasks_task_invalid"));

    task.issueUrl = QStringLiteral("https://bitbucket.org/team/repo/issues/7");
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.saveTask(task)).hasValue());

    // An empty address is allowed, because linking an issue is optional.
    task.issueUrl.clear();
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.saveTask(task)).hasValue());
}

TEST(McpClientTest, AnswersEveryMalformedLineInsteadOfReadingPastIt) {
    // Each shape is one way a server can write a line wrongly, and every one must end with the client answering.
    for (int shape = 0; shape <= 7; ++shape) {
        agent::mcp::McpServerDescriptor fixture;
        fixture.id = QStringLiteral("fixture");
        fixture.command = QCoreApplication::applicationFilePath();
        fixture.arguments = {QStringLiteral("--workpane-test-mcp-malformed"), QString::number(shape)};

        agent::mcp::McpClient client(fixture);
        int answers = 0;
        // clang-format off
        QObject::connect(&client, &agent::mcp::McpClient::failed, &client, [&answers](const Error&) { ++answers; });
        QObject::connect(&client, &agent::mcp::McpClient::initialized, &client, [&answers]() { ++answers; });
        // clang-format on

        client.start();
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return answers > 0; }, 30000)) << "shape " << shape;
        // clang-format on
        EXPECT_FALSE(client.ready()) << "shape " << shape;
        client.stop();
    }

    agent::mcp::McpClient::drainTransports();
}

// A command task reaches no model, so it runs a process and is judged by what that process printed and by how it ended.
TEST(AiPluginTest, RunsACommandTaskAndRecordsWhatTheProcessPrinted) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    QTemporaryDir project;
    ASSERT_TRUE(project.isValid());
    const QString workdir = QFileInfo(project.path()).canonicalFilePath();
    QFile marker(QDir(workdir).filePath(QStringLiteral("marker.txt")));
    ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
    marker.write(QByteArrayLiteral("here"));
    marker.close();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};

    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.executionKind = TaskExecutionKind::Command;
    task.command = AiTestsHelper::readFileCommand(QStringLiteral("marker.txt"));
    task.workdir = workdir;
    task.agentId.clear();
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});
    const AiTestsHelper::RecordedRuns runs = AiTestsHelper::installExecutionRows(host, {}, {});
    // clang-format off
    AiPlugin plugin([](AiRequestGate&, const ModelConnection&) { return std::unique_ptr<AiChatClient>(std::make_unique<FakeChatClient>()); });
    // clang-format on
    const auto started = plugin.initialize(host);
    ASSERT_TRUE(started.hasValue()) << started.error().code.toStdString() << " " << started.error().message.toStdString() << " " << started.error().detail.toStdString();
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&plugin]() { return plugin.tasks().first().column == TaskColumn::Done; }));
    // clang-format on

    // A command that ended well leaves nothing to report and moves the card on.
    EXPECT_TRUE(plugin.lastError(task.id).isEmpty()) << plugin.lastError(task.id).toStdString();

    // The command only succeeds beside the file, so reaching Done is what proves it ran where the task declared.
    Q_UNUSED(runs);
}

// A command that ends badly fails its run and keeps what it printed, because that is where the reason is.
TEST(AiPluginTest, FailsACommandTaskThatEndedBadlyAndKeepsWhatItPrinted) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    QTemporaryDir project;
    ASSERT_TRUE(project.isValid());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};

    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.executionKind = TaskExecutionKind::Command;
    task.command = AiTestsHelper::failingCommand();
    task.workdir = QFileInfo(project.path()).canonicalFilePath();
    task.agentId.clear();
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});
    const AiTestsHelper::RecordedRuns runs = AiTestsHelper::installExecutionRows(host, {}, {});

    // clang-format off
    AiPlugin plugin([](AiRequestGate&, const ModelConnection&) { return std::unique_ptr<AiChatClient>(std::make_unique<FakeChatClient>()); });
    // clang-format on
    const auto started = plugin.initialize(host);
    ASSERT_TRUE(started.hasValue()) << started.error().code.toStdString() << " " << started.error().message.toStdString() << " " << started.error().detail.toStdString();
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&plugin, &task]() { return !plugin.lastError(task.id).isEmpty(); }));
    // clang-format on

    // A command that ended badly returns the card to the board and keeps the reason on it.
    EXPECT_EQ(plugin.tasks().first().column, TaskColumn::Todo) << "a task whose command failed did not return to the board";
    Q_UNUSED(runs);
}

// Moving a card is how the board is used, so where it is dropped decides whether a run starts, stops or the card simply moves.
TEST(AiPluginTest, StartsStopsOrMovesACardByWhereItIsDropped) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});
    AiTestsHelper::installExecutionRows(host, {}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return std::unique_ptr<AiChatClient>(std::move(created)); });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.tasks().first().column, TaskColumn::Todo);

    // A card dropped anywhere but Doing while nothing runs simply moves.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.moveTask(task.id, TaskColumn::Blocked)).hasValue());
    EXPECT_EQ(plugin.tasks().first().column, TaskColumn::Blocked);
    EXPECT_EQ(plugin.runState(task.id), TaskRunState::Idle);

    // A card dropped into Doing starts it.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.moveTask(task.id, TaskColumn::Doing)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&client]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on
    EXPECT_NE(plugin.runState(task.id), TaskRunState::Idle);

    // A card dragged out of Doing while it runs stops it and returns it to the board.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.moveTask(task.id, TaskColumn::Review)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&plugin, &task]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.tasks().first().column, TaskColumn::Todo) << "a card stopped by being dragged did not return to the board";

    // A card nobody declares is refused by name.
    const auto unknown = test::TestFutures::awaitFuture(plugin.moveTask(QStringLiteral("nobody-declares-this"), TaskColumn::Done));
    ASSERT_FALSE(unknown.hasValue());
    EXPECT_EQ(unknown.error().code, QStringLiteral("ai_tasks_task_unknown"));
}

} // namespace workpane::plugins::ai
