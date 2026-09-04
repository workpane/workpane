#include "AiAgentSettingsView.h"
#include "AiTestSupport.h"

#include <QImage>

#include <memory>
#include <optional>
#include <utility>

namespace workpane::plugins::ai {

TEST(AiChatClientTest, ReadsTheRejectionReasonFromEveryShapeAServiceReportsIt) {
    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*openai, model), {}};

    // clang-format off
    const auto reasonOf = [&connection](const QByteArray& body) {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            return QString{};
        }
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, body]() {
            QTcpSocket* socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, body]() {
                if (!socket->readAll().contains(QByteArrayLiteral("\r\n\r\n"))) {
                    return;
                }
                socket->write(QByteArrayLiteral("HTTP/1.1 429 Too Many Requests\r\nRetry-After: 0\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
                socket->disconnectFromHost();
            });
        });

        AiRequestGate clientGate;
        AiHttpChatClient client(clientGate);
        QVector<Error> failures;
        QObject::connect(&client, &AiChatClient::failed, &client, [&failures](const Error& error) { failures.append(error); });
        client.send({connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
        return test::TestFutures::waitUntil([&]() { return !failures.isEmpty(); }, 20000) && !failures.isEmpty() ? failures.first().message : QString{};
    };
    // clang-format on

    EXPECT_EQ(reasonOf(QByteArrayLiteral(R"({"error":{"type":"rate_limit","message":"slow down"}})")), QStringLiteral("rate_limit: slow down"));
    EXPECT_EQ(reasonOf(QByteArrayLiteral(R"({"error":"too many requests"})")), QStringLiteral("too many requests"));
    EXPECT_EQ(reasonOf(QByteArrayLiteral(R"({"message":"quota exhausted"})")), QStringLiteral("quota exhausted"));
    EXPECT_EQ(reasonOf(QByteArrayLiteral(R"({"detail":"rate limit reached for this key"})")), QStringLiteral("rate limit reached for this key"));

    // A service reporting no reason at all is quoted, because that body is still what explains the rejection.
    EXPECT_EQ(reasonOf(QByteArrayLiteral("Too Many Requests")), QStringLiteral("Too Many Requests"));
}

TEST(AiChatClientTest, SendsOnlyTheSamplingControlEachProviderAccepts) {
    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    const QString model = QStringLiteral("claude-3-haiku-20240307");
    const ModelConnection connection{anthropic->id, model, {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*anthropic, model), {}};

    // The Anthropic API rejects a request carrying both sampling controls.
    // clang-format off
    const QJsonObject body = ChatRequests::buildRequestBody(*anthropic, {connection, {}, QJsonArray{}, {}}, [](const QString& key) { return key; });
    // clang-format on
    EXPECT_TRUE(body.contains(QStringLiteral("temperature")));
    EXPECT_FALSE(body.contains(QStringLiteral("top_p")));

    for (const auto& parameter : ProviderCatalog::applicableParameters(*anthropic, model)) {
        EXPECT_NE(parameter.id, QStringLiteral("topP"));
    }
}

TEST(AiTaskRepositoryTest, NeverBindsANullValueToATextColumnDeclaredNotNull) {
    test::TestPluginHost host;
    AiTaskRepository repository(host);
    ASSERT_TRUE(repository.initialize().hasValue());

    // A default execution carries null strings, which Qt would bind as SQL NULL against a NOT NULL column.
    TaskExecution execution;
    execution.id = QStringLiteral("execution-1");
    execution.taskId = QStringLiteral("task-1");
    execution.status = ExecutionStatus::Succeeded;
    execution.startedAtUtc = QDateTime::currentDateTimeUtc();
    execution.finishedAtUtc = execution.startedAtUtc.addSecs(1);
    ASSERT_TRUE(execution.finishReason.isNull());
    ASSERT_TRUE(execution.errorMessage.isNull());
    ASSERT_TRUE(execution.content.isNull());
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.finishExecution(execution)).hasValue());

    AiTask task;
    task.id = QStringLiteral("task-1");
    task.workspaceId = QStringLiteral("workspace-1");
    task.title = QStringLiteral("Review");
    task.prompt = QStringLiteral("Prompt");
    task.agentId = AiTestsHelper::testAgent().id;
    task.createdAtUtc = QDateTime::currentDateTimeUtc();
    task.updatedAtUtc = task.createdAtUtc;
    ASSERT_TRUE(task.description.isNull());
    EXPECT_TRUE(test::TestFutures::awaitFuture(repository.saveTask(task)).hasValue());

    qsizetype inspected = 0;

    for (const auto& recorded : host.databaseExecutions) {
        for (const auto& binding : recorded.value(QStringLiteral("bindings")).toList()) {
            EXPECT_FALSE(binding.typeId() == QMetaType::QString && binding.toString().isNull()) << recorded.value(QStringLiteral("statement")).toString().toStdString();
            ++inspected;
        }
    }

    for (const auto& transaction : host.databaseTransactions) {
        for (const auto& statement : transaction) {
            for (const auto& binding : statement.bindings) {
                EXPECT_FALSE(binding.typeId() == QMetaType::QString && binding.toString().isNull()) << statement.statement.toStdString();
                ++inspected;
            }
        }
    }

    EXPECT_GT(inspected, 0);
}

TEST(AiTaskDialogTest, LetsAnAgentChooseItsWorkingDirectoryAndValidatesEveryDeclaredField) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    const ModelConnection anthropicConnection = ModelConnections::declaredConnection(*ProviderCatalog::findProvider(QStringLiteral("anthropic")), QStringLiteral("claude-opus-5"));
    const QVector<ModelConnection> connections{AiTestsHelper::testConnection(), anthropicConnection};
    AiTaskDialog dialog(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    dialog.show();

    auto* kind = dialog.findChild<QComboBox*>(QStringLiteral("aiTaskExecutionKind"));
    auto* title = dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"));
    auto* prompt = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiTaskPromptField"));
    auto* workdir = dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"));
    auto* command = dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskCommand"));
    auto* validation = dialog.findChild<QLabel*>(QStringLiteral("aiTaskValidation"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(kind, nullptr);
    ASSERT_NE(workdir, nullptr);
    ASSERT_NE(command, nullptr);
    ASSERT_NE(buttons, nullptr);

    // An agent needs the working directory its file tools are bound to, while the command fields belong to a command.
    EXPECT_TRUE(workdir->isVisible());
    EXPECT_FALSE(command->isVisible());
    EXPECT_TRUE(dialog.findChild<QComboBox*>(QStringLiteral("aiTaskAgent"))->isVisible());

    title->setText(QStringLiteral("Write the report"));
    prompt->setPlainText(QStringLiteral("Write it"));
    workdir->setText(QStringLiteral("relative/path"));
    QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
    EXPECT_EQ(dialog.result(), 0);
    EXPECT_TRUE(validation->isVisible());
    EXPECT_EQ(validation->text(), host.translate(QStringLiteral("ai.validation.workdir")));

    workdir->setText(root.path());
    dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskIssueUrl"))->setText(QStringLiteral("not-an-address"));
    QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
    EXPECT_EQ(dialog.result(), 0);
    EXPECT_EQ(validation->text(), host.translate(QStringLiteral("ai.validation.issue-url")));

    dialog.findChild<QLineEdit*>(QStringLiteral("aiTaskIssueUrl"))->setText(QStringLiteral("https://github.com/paulo/workpane/issues/7"));
    QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(dialog.task().executionKind, TaskExecutionKind::Agent);
    EXPECT_EQ(dialog.task().workdir, QDir::cleanPath(root.path()));

    // A new task starts in the home directory of the running system, and clearing the field means no file access at all.
    AiTaskDialog fresh(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    fresh.show();
    EXPECT_EQ(fresh.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"))->text(), QDir::homePath());
    fresh.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"))->setText(QStringLiteral("At home"));
    fresh.findChild<QPlainTextEdit*>(QStringLiteral("aiTaskPromptField"))->setPlainText(QStringLiteral("Answer"));
    QTest::mouseClick(fresh.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(fresh.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(fresh.task().workdir, QDir::cleanPath(QDir::homePath()));

    AiTaskDialog withoutRoot(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    withoutRoot.show();
    withoutRoot.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"))->setText(QString{});
    withoutRoot.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"))->setText(QStringLiteral("No files"));
    withoutRoot.findChild<QPlainTextEdit*>(QStringLiteral("aiTaskPromptField"))->setPlainText(QStringLiteral("Answer"));
    QTest::mouseClick(withoutRoot.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(withoutRoot.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_TRUE(withoutRoot.task().workdir.isEmpty());

    // The schedule kinds read from no schedule to the most expressive one instead of alphabetically.
    auto* scheduleKind = dialog.findChild<QComboBox*>(QStringLiteral("aiTaskScheduleKind"));
    ASSERT_NE(scheduleKind, nullptr);
    ASSERT_EQ(scheduleKind->count(), 4);
    EXPECT_TRUE(scheduleKind->itemData(0).toString().isEmpty());
    EXPECT_EQ(scheduleKind->itemData(1).toString(), AiTaskRepository::scheduleKindName(ScheduleKind::Once));
    EXPECT_EQ(scheduleKind->itemData(2).toString(), AiTaskRepository::scheduleKindName(ScheduleKind::Interval));
    EXPECT_EQ(scheduleKind->itemData(3).toString(), AiTaskRepository::scheduleKindName(ScheduleKind::Cron));

    // The task chooses one configured connection, and it opens on the default one.
    auto* agentBox = dialog.findChild<QComboBox*>(QStringLiteral("aiTaskAgent"));
    ASSERT_NE(agentBox, nullptr);
    EXPECT_EQ(agentBox->count(), 1);
    EXPECT_EQ(dialog.task().agentId, AiTestsHelper::testAgent().id);

    AiTaskDialog another(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    another.show();
    another.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"))->setText(QStringLiteral("Ported"));
    another.findChild<QPlainTextEdit*>(QStringLiteral("aiTaskPromptField"))->setPlainText(QStringLiteral("Answer"));
    QTest::mouseClick(another.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(another.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(another.task().agentId, AiTestsHelper::testAgent().id);

    // Choosing the command kind exposes the command fields and keeps the same working directory row.
    AiTaskDialog commandDialog(host, QStringLiteral("workspace-1"), std::nullopt, {12, 60}, {AiTestsHelper::testAgent()}, nullptr);
    commandDialog.show();
    auto* commandKind = commandDialog.findChild<QComboBox*>(QStringLiteral("aiTaskExecutionKind"));
    commandKind->setCurrentIndex(commandKind->findData(AiTaskRepository::taskExecutionKindName(TaskExecutionKind::Command)));
    EXPECT_TRUE(commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskCommand"))->isVisible());
    EXPECT_TRUE(commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"))->isVisible());
    EXPECT_FALSE(commandDialog.findChild<QComboBox*>(QStringLiteral("aiTaskAgent"))->isVisible());

    commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskTitleField"))->setText(QStringLiteral("Build"));
    QTest::mouseClick(commandDialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    EXPECT_EQ(commandDialog.findChild<QLabel*>(QStringLiteral("aiTaskValidation"))->text(), host.translate(QStringLiteral("ai.validation.command")));
    commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskCommand"))->setText(QStringLiteral("make"));
    commandDialog.findChild<QLineEdit*>(QStringLiteral("aiTaskWorkdir"))->setText(root.path());
    QTest::mouseClick(commandDialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(commandDialog.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(commandDialog.task().command, QStringLiteral("make"));
    EXPECT_EQ(commandDialog.task().commandTimeoutSeconds, 60);
    EXPECT_TRUE(commandDialog.task().agentId.isEmpty());
}

TEST(AiTaskInfoDialogTest, PresentsExecutionsLogsAndExplainsAnEmptyOutput) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);

    TaskExecution failed;
    failed.id = QStringLiteral("execution-1");
    failed.taskId = task.id;
    failed.status = ExecutionStatus::Failed;
    failed.startedAtUtc = now;
    failed.finishedAtUtc = now.addSecs(1);
    failed.errorMessage = QStringLiteral("invalid_request_error: model is unknown");
    failed.inputTokens = 1000;
    failed.outputTokens = 500;
    failed.providerId = QStringLiteral("openai");
    failed.modelId = QStringLiteral("gpt-4o");
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});
    AiTestsHelper::installExecutionRows(host, {failed}, {{QStringLiteral("log-1"), failed.id, 1, now, ExecutionLogLevel::Error, ExecutionLogKind::Failed, failed.errorMessage}});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    AiTaskInfoDialog dialog(plugin, host, task, nullptr);
    dialog.resize(900, 600);
    dialog.show();

    auto* executionGrid = dialog.findChild<QTableWidget*>(QStringLiteral("aiExecutionGrid"));
    auto* logGrid = dialog.findChild<QTableWidget*>(QStringLiteral("aiLogGrid"));
    auto* outputPages = dialog.findChild<QStackedWidget*>(QStringLiteral("aiOutputPages"));
    auto* outputEmpty = dialog.findChild<QLabel*>(QStringLiteral("aiOutputEmpty"));
    ASSERT_NE(executionGrid, nullptr);
    ASSERT_NE(logGrid, nullptr);
    ASSERT_NE(outputPages, nullptr);
    ASSERT_NE(outputEmpty, nullptr);

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return executionGrid->rowCount() == 1 && logGrid->rowCount() == 1; }));
    // clang-format on
    ASSERT_EQ(executionGrid->rowCount(), 1);
    EXPECT_EQ(executionGrid->item(0, 1)->text(), QStringLiteral("Failed"));
    EXPECT_EQ(executionGrid->item(0, 5)->text(), failed.errorMessage);

    // A run reports what it cost, from the price the catalog carries for the model that run really spoke to.
    const auto spent = ProviderCatalog::runCost(failed.providerId, failed.modelId, failed.inputTokens, failed.outputTokens);
    ASSERT_TRUE(spent.has_value());
    EXPECT_EQ(executionGrid->item(0, 3)->text(), QStringLiteral("USD %1").arg(QLocale::system().toString(spent.value(), 'f', 4)));
    EXPECT_EQ(logGrid->item(0, 2)->text(), host.translations.value(QStringLiteral("ai.log-kind.failed")));
    EXPECT_EQ(logGrid->item(0, 3)->text(), failed.errorMessage);

    // A long message wraps instead of being truncated.
    EXPECT_TRUE(logGrid->wordWrap());
    EXPECT_TRUE(executionGrid->wordWrap());

    // An execution without returned text explains why instead of showing a blank surface.
    EXPECT_EQ(outputPages->currentWidget(), outputEmpty);
    EXPECT_EQ(outputEmpty->text(), failed.errorMessage);

    // The returned content is rendered as Markdown instead of being shown as its source.
    TaskExecution answered = failed;
    answered.id = QStringLiteral("execution-2");
    answered.status = ExecutionStatus::Succeeded;
    answered.errorMessage.clear();
    answered.content = QStringLiteral("# Title\n\nA paragraph with `code` and a [link](https://example.com).\n\n- first\n- second\n");
    AiTestsHelper::installExecutionRows(host, {answered}, {});
    AiTaskInfoDialog rendered(plugin, host, task, nullptr);
    rendered.resize(900, 600);
    rendered.show();
    auto* renderedContent = rendered.findChild<QTextBrowser*>(QStringLiteral("aiExecutionContent"));
    ASSERT_NE(renderedContent, nullptr);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !renderedContent->toPlainText().isEmpty(); }));
    // clang-format on
    EXPECT_EQ(renderedContent->toMarkdown().trimmed().isEmpty(), false);
    EXPECT_FALSE(renderedContent->toPlainText().contains(QStringLiteral("# Title")));
    EXPECT_TRUE(renderedContent->toPlainText().contains(QStringLiteral("Title")));
    EXPECT_TRUE(renderedContent->toHtml().contains(QStringLiteral("https://example.com")));
    EXPECT_EQ(renderedContent->frameShape(), QFrame::NoFrame);

    // A payload entry is opened on demand while every other kind stays readable in the grid.
    AiTestsHelper::installExecutionRows(host, {failed}, {{QStringLiteral("log-1"), failed.id, 1, now, ExecutionLogLevel::Error, ExecutionLogKind::Failed, failed.errorMessage}, {QStringLiteral("log-2"), failed.id, 2, now, ExecutionLogLevel::Debug, ExecutionLogKind::RequestSent, QStringLiteral("{\"model\":\"gpt-4o\"}")}});
    AiTaskInfoDialog payloads(plugin, host, task, nullptr);
    payloads.resize(900, 600);
    payloads.show();
    auto* payloadGrid = payloads.findChild<QTableWidget*>(QStringLiteral("aiLogGrid"));
    ASSERT_NE(payloadGrid, nullptr);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return payloadGrid->rowCount() == 2; }));
    // clang-format on
    // The newest entry is the first row, so the payload it carries is the one on top.
    EXPECT_EQ(payloadGrid->cellWidget(1, 3), nullptr);
    EXPECT_EQ(payloadGrid->item(1, 3)->text(), failed.errorMessage);
    ASSERT_NE(payloadGrid->cellWidget(0, 3), nullptr);
    auto* openPayload = payloadGrid->cellWidget(0, 3)->findChild<QToolButton*>();
    ASSERT_NE(openPayload, nullptr);
    EXPECT_EQ(openPayload->text(), host.translate(QStringLiteral("ai.log.open-payload")));
    EXPECT_EQ(openPayload->objectName(), QStringLiteral("chipButton"));
    EXPECT_EQ(openPayload->height(), host.theme().metric(ui::ThemeMetric::BadgeRadius) * 2);
    EXPECT_TRUE(AiTaskRepository::carriesExchangedPayload(ExecutionLogKind::ResponseReceived));
    EXPECT_FALSE(AiTaskRepository::carriesExchangedPayload(ExecutionLogKind::UsageReported));

    // The payload answers nothing, so it opens without a nested loop and the chip that opened it survives the reload that follows.
    const QPointer<QWidget> chipCell = payloadGrid->cellWidget(0, 3);
    openPayload->click();
    auto* viewer = payloads.findChild<QDialog*>(QStringLiteral("aiPayloadDialog"));
    ASSERT_NE(viewer, nullptr);
    EXPECT_TRUE(viewer->isVisible());
    EXPECT_FALSE(chipCell.isNull());

    viewer->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(payloads.findChild<QDialog*>(QStringLiteral("aiPayloadDialog")), nullptr);

    // The dialog opens with the shared page header, so it closes with the same divider as every other surface.
    ASSERT_NE(payloads.findChild<QWidget*>(QStringLiteral("pageHeader")), nullptr);

    // A command run exchanges nothing with a model and stores no artifact, so those surfaces do not exist for it.
    AiTask commandTask = task;
    commandTask.executionKind = TaskExecutionKind::Command;
    commandTask.command = QStringLiteral("make");
    AiTaskInfoDialog commandDialog(plugin, host, commandTask, nullptr);
    commandDialog.resize(900, 600);
    commandDialog.show();
    EXPECT_EQ(commandDialog.findChild<QTableWidget*>(QStringLiteral("aiLogGrid")), nullptr);
    EXPECT_EQ(commandDialog.findChild<QTableWidget*>(QStringLiteral("aiArtifactGrid")), nullptr);
    ASSERT_NE(commandDialog.findChild<QTableWidget*>(QStringLiteral("aiExecutionGrid")), nullptr);
    ASSERT_NE(commandDialog.findChild<QStackedWidget*>(QStringLiteral("aiOutputPages")), nullptr);

    // Selecting an execution of a command run must stay safe without those surfaces.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return commandDialog.findChild<QTableWidget*>(QStringLiteral("aiExecutionGrid"))->rowCount() == 1; }));
    // clang-format on
    EXPECT_EQ(commandDialog.findChild<QTableWidget*>(QStringLiteral("aiExecutionGrid"))->currentRow(), 0);
}

TEST(AiServiceSettingsViewTest, AsksOnlyForWhatTheSelectedServiceDoesNotPublish) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiTestsHelper::installEmptyProviderRows(host);
    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    std::unique_ptr<QWidget> search(plugin.createSettingsSection(QStringLiteral("tools"), QStringLiteral("search"), nullptr));
    ASSERT_NE(search, nullptr);
    search->show();
    auto* service = search->findChild<QComboBox*>(QStringLiteral("aiSearchProvider"));
    auto* instance = search->findChild<QLineEdit*>(QStringLiteral("aiSearchInstance"));
    auto* searchKey = search->findChild<ui::SecretField*>(QStringLiteral("aiSearchKey"));
    ASSERT_NE(service, nullptr);
    ASSERT_NE(instance, nullptr);
    ASSERT_NE(searchKey, nullptr);

    // A hosted service publishes its endpoint, so only its credential is asked for and it arrives already referenced.
    EXPECT_FALSE(instance->isVisible());
    EXPECT_TRUE(searchKey->isVisible());
    EXPECT_EQ(searchKey->value(), QStringLiteral("{env.BRAVE_API_KEY}"));

    service->setCurrentIndex(service->findData(TaskContracts::searchProviderIdentifier(SearchProvider::Tavily)));
    EXPECT_EQ(TaskContracts::searchAddress({SearchProvider::Tavily, {}, {}}), QStringLiteral("https://api.tavily.com"));
    EXPECT_FALSE(instance->isVisible());

    // A self-hosted instance is the only one that carries an address, and it needs no key.
    service->setCurrentIndex(service->findData(TaskContracts::searchProviderIdentifier(SearchProvider::SearxNg)));
    EXPECT_TRUE(instance->isVisible());
    EXPECT_FALSE(searchKey->isVisible());

    std::unique_ptr<QWidget> speech(plugin.createSettingsSection(QStringLiteral("tools"), QStringLiteral("speech"), nullptr));
    ASSERT_NE(speech, nullptr);
    speech->show();
    auto* speechService = speech->findChild<QComboBox*>(QStringLiteral("aiSpeechProvider"));
    auto* typedVoice = speech->findChild<QLineEdit*>(QStringLiteral("aiSpeechVoice"));
    auto* declaredVoice = speech->findChild<QComboBox*>(QStringLiteral("aiSpeechDeclaredVoice"));
    auto* speechKey = speech->findChild<ui::SecretField*>(QStringLiteral("aiSpeechKey"));
    ASSERT_NE(speechService, nullptr);
    ASSERT_NE(typedVoice, nullptr);
    ASSERT_NE(declaredVoice, nullptr);
    ASSERT_NE(speechKey, nullptr);

    // The address is never asked for, and an account catalog is typed while a closed set is chosen.
    EXPECT_EQ(speech->findChild<QLineEdit*>(QStringLiteral("aiSpeechAddress")), nullptr);
    EXPECT_EQ(speechKey->value(), QStringLiteral("{env.ELEVENLABS_API_KEY}"));
    EXPECT_TRUE(typedVoice->isVisible());
    EXPECT_FALSE(declaredVoice->isVisible());

    speechService->setCurrentIndex(speechService->findData(QStringLiteral("openai")));
    EXPECT_FALSE(typedVoice->isVisible());
    EXPECT_TRUE(declaredVoice->isVisible());
    EXPECT_GT(declaredVoice->count(), 0);
    EXPECT_EQ(declaredVoice->currentData().toString(), TaskContracts::declaredSpeechSettings(QStringLiteral("openai")).voiceId);
    plugin.shutdown();
}

TEST(AiPluginTest, BuildsEveryDeclaredSettingsSectionAndRefusesAnUndeclaredOne) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiTestsHelper::installEmptyProviderRows(host);
    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    const auto groups = plugin.settingsGroups();
    QStringList groupIds;
    QStringList sectionIds;

    for (const auto& group : groups) {
        groupIds.append(group.id);
        for (const auto& section : group.sections) {
            sectionIds.append(QStringLiteral("%1/%2").arg(group.id, section.id));
            std::unique_ptr<QWidget> page(plugin.createSettingsSection(group.id, section.id, nullptr));
            ASSERT_NE(page, nullptr) << qPrintable(section.id);
            page->show();
        }
    }

    EXPECT_EQ(groupIds, QStringList({QStringLiteral("connections"), QStringLiteral("providers"), QStringLiteral("agents"), QStringLiteral("tools"), QStringLiteral("general")}));
    EXPECT_EQ(sectionIds, QStringList({QStringLiteral("connections/general"), QStringLiteral("providers/selection"), QStringLiteral("providers/rate-limits"), QStringLiteral("agents/general"), QStringLiteral("tools/mcp"), QStringLiteral("tools/search"), QStringLiteral("tools/speech"), QStringLiteral("general/general")}));

    std::unique_ptr<QWidget> unknown(plugin.createSettingsSection(QStringLiteral("providers"), QStringLiteral("absent"), nullptr));
    EXPECT_EQ(unknown, nullptr);
    std::unique_ptr<QWidget> foreign(plugin.createSettingsSection(QStringLiteral("absent"), QStringLiteral("general"), nullptr));
    EXPECT_EQ(foreign, nullptr);

    // The server section starts from the empty state, because no server is registered yet.
    std::unique_ptr<QWidget> servers(plugin.createSettingsSection(QStringLiteral("tools"), QStringLiteral("mcp"), nullptr));
    ASSERT_NE(servers, nullptr);
    servers->resize(700, 400);
    servers->show();
    auto* grid = servers->findChild<QTableWidget*>(QStringLiteral("aiMcpGrid"));
    auto* empty = servers->findChild<QLabel*>(QStringLiteral("aiMcpEmpty"));
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(grid->isVisible());
    EXPECT_TRUE(empty->isVisible());
    plugin.shutdown();
}

TEST(AiMcpSettingsViewTest, OpensTheEditorOfTheServerThatWasDoubleClicked) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiTestsHelper::installEmptyProviderRows(host);
    const QJsonObject server{{QStringLiteral("id"), QStringLiteral("files")}, {QStringLiteral("transport"), QStringLiteral("stdio")}, {QStringLiteral("command"), QStringLiteral("mcp-files")}};
    host.settingsDocument = {{QStringLiteral("mcpServers"), QJsonArray{server}}};

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.mcpServers().size(), 1);

    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("tools"), QStringLiteral("mcp"), nullptr));
    ASSERT_NE(section, nullptr);
    section->resize(760, 520);
    section->show();

    auto* grid = section->findChild<QTableWidget*>(QStringLiteral("aiMcpGrid"));
    ASSERT_NE(grid, nullptr);
    ASSERT_EQ(grid->rowCount(), 1);

    bool opened = false;
    // clang-format off
    QTimer::singleShot(0, section.get(), [&section, &opened]() { if (auto* dialog = section->findChild<QDialog*>(QStringLiteral("aiMcpServerDialog")); dialog != nullptr) { opened = true; dialog->reject(); } });
    // clang-format on
    grid->selectRow(0);
    emit grid->doubleClicked(grid->model()->index(0, 0));

    EXPECT_TRUE(opened);
    EXPECT_EQ(plugin.mcpServers().size(), 1);
    section.reset();
    plugin.shutdown();
}

TEST(AiProviderSettingsViewTest, GovernsEveryPerProviderSectionFromOneSelector) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiTestsHelper::installEmptyProviderRows(host);

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> selection(plugin.createSettingsSection(QStringLiteral("providers"), QStringLiteral("selection"), nullptr));
    std::unique_ptr<QWidget> limits(plugin.createSettingsSection(QStringLiteral("providers"), QStringLiteral("rate-limits"), nullptr));
    ASSERT_NE(selection, nullptr);
    ASSERT_NE(limits, nullptr);
    selection->show();
    limits->show();

    auto* provider = selection->findChild<QComboBox*>(QStringLiteral("aiScopeProvider"));
    auto* interval = limits->findChild<QSpinBox*>(QStringLiteral("aiRateLimitInterval"));
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(interval, nullptr);
    ASSERT_GT(provider->count(), 1);

    // The limit is written against the provider the selector carries, so a value never lands on the one nobody chose.
    const QString first = provider->currentData().toString();
    interval->setValue(750);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&plugin, &first]() { const auto stored = plugin.rateLimits(); return stored.size() == 1 && stored.first().providerId == first && stored.first().minimumIntervalMs == 750; }));
    // clang-format on

    // Selecting another provider reloads every section of the group, so the fields present what that provider was given.
    provider->setCurrentIndex(provider->currentIndex() == 0 ? 1 : 0);
    const QString second = provider->currentData().toString();
    EXPECT_NE(second, first);
    EXPECT_EQ(interval->value(), 0);

    interval->setValue(120);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&plugin]() { return plugin.rateLimits().size() == 2; }));
    // clang-format on

    provider->setCurrentIndex(provider->findData(first));
    EXPECT_EQ(interval->value(), 750);
}

TEST(AiConnectionSettingsViewTest, OpensTheEditorOfTheRowThatWasDoubleClicked) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiTestsHelper::installEmptyProviderRows(host);
    host.settingsDocument = AiTestsHelper::settingsDocument({AiTestsHelper::testConnection()}, ModelConnections::connectionKey(AiTestsHelper::testConnection()));

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("connections"), QStringLiteral("general"), nullptr));
    ASSERT_NE(section, nullptr);
    section->resize(760, 520);
    section->show();

    auto* grid = section->findChild<QTableWidget*>(QStringLiteral("aiConnectionGrid"));
    ASSERT_NE(grid, nullptr);
    ASSERT_EQ(grid->rowCount(), 1);

    // The editor is modal, so it is dismissed as soon as it appears and what matters is that the row opened it.
    bool opened = false;
    // clang-format off
    QTimer::singleShot(0, section.get(), [&section, &opened]() { if (auto* dialog = section->findChild<AiConnectionDialog*>(); dialog != nullptr) { opened = true; dialog->reject(); } });
    // clang-format on
    grid->selectRow(0);
    emit grid->doubleClicked(grid->model()->index(0, 0));

    EXPECT_TRUE(opened);
    EXPECT_EQ(plugin.connections().size(), 1);
    section.reset();
    plugin.shutdown();
}

TEST(AiConnectionSettingsViewTest, RemovesAConnectionThroughTheConfirmationAndKeepsTheDefaultConfigured) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    ModelConnection second = AiTestsHelper::testConnection();
    second.modelId = QStringLiteral("gpt-4o-mini");
    second.displayName = QStringLiteral("Cheap reviewer");
    second.parameters = ProviderCatalog::defaultParameters(*ProviderCatalog::findProvider(QStringLiteral("openai")), second.modelId);
    AiTestsHelper::installEmptyProviderRows(host);
    host.settingsDocument = AiTestsHelper::settingsDocument({AiTestsHelper::testConnection(), second}, ModelConnections::connectionKey(AiTestsHelper::testConnection()), {});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.connections().size(), 2);

    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("connections"), QStringLiteral("general"), nullptr));
    ASSERT_NE(section, nullptr);
    section->resize(760, 520);
    section->show();

    auto* grid = section->findChild<QTableWidget*>(QStringLiteral("aiConnectionGrid"));
    auto* empty = section->findChild<QLabel*>(QStringLiteral("aiConnectionEmpty"));
    auto* defaultConnection = section->findChild<QComboBox*>(QStringLiteral("aiDefaultConnection"));
    auto* remove = section->findChild<QToolButton*>(QStringLiteral("aiConnectionRemove"));
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(defaultConnection, nullptr);
    ASSERT_NE(remove, nullptr);
    EXPECT_TRUE(grid->isVisible());
    EXPECT_FALSE(empty->isVisible());
    EXPECT_EQ(grid->rowCount(), 2);
    EXPECT_EQ(grid->item(0, 0)->text(), QStringLiteral("openai/gpt-4o"));
    EXPECT_EQ(grid->item(1, 0)->text(), QStringLiteral("Cheap reviewer"));
    EXPECT_EQ(grid->columnCount(), 3);
    EXPECT_EQ(defaultConnection->count(), 2);
    EXPECT_EQ(defaultConnection->currentData().toString(), QStringLiteral("openai/gpt-4o"));

    // A cancelled confirmation removes nothing.
    host.confirmation = false;
    grid->selectRow(0);
    QTest::mouseClick(remove, Qt::LeftButton);
    EXPECT_EQ(plugin.connections().size(), 2);

    // Removing the default connection moves the default to one that is still configured.
    host.confirmation = true;
    grid->selectRow(0);
    QTest::mouseClick(remove, Qt::LeftButton);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.connections().size() == 1; }));
    // clang-format on
    EXPECT_EQ(plugin.defaultConnectionKey(), ModelConnections::connectionKey(second));
    EXPECT_EQ(grid->rowCount(), 1);

    grid->selectRow(0);
    QTest::mouseClick(remove, Qt::LeftButton);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.connections().isEmpty(); }));
    // clang-format on
    EXPECT_TRUE(plugin.defaultConnectionKey().isEmpty());
    EXPECT_FALSE(grid->isVisible());
    EXPECT_TRUE(empty->isVisible());
    EXPECT_FALSE(defaultConnection->isEnabled());

    section.reset();
    plugin.shutdown();
}

TEST(AiPluginTest, DispatchesTheRestoredQueueAsSoonAsItLoads) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask queued = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    queued.column = TaskColumn::Doing;
    AiTestsHelper::installAiRows(host, {workspace}, {queued}, {queued.id});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // What was waiting when the application closed reaches the model again without the user touching anything.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on
    EXPECT_EQ(plugin.runState(queued.id), TaskRunState::Running);
    clients.first()->deliver(QStringLiteral("done"), {1, 2}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(queued.id) == TaskRunState::Idle; }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiPluginTest, WritesTheFileTheAgentAsksForAndFinishesOnTheAnswerThatFollows) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = workdir.path();
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    // The whole point of the agent is that a file it asks for exists afterwards, written where it asked for it.
    const QString page = QDir(QDir(workdir.path()).canonicalPath()).filePath(QStringLiteral("site/index.html"));
    FakeChatClient* agent = clients.first();
    agent->deliverToolCalls({{QStringLiteral("c1"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), page}, {QStringLiteral("content"), QStringLiteral("<html>landing</html>")}}}});
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return agent->sendCalls == 2; }));
    // clang-format on
    ASSERT_TRUE(QFileInfo(page).isFile());
    QFile written(page);
    ASSERT_TRUE(written.open(QIODevice::ReadOnly));
    EXPECT_EQ(written.readAll(), QByteArrayLiteral("<html>landing</html>"));

    // The turn carries the result back, so the model sees the path it wrote instead of a failure.
    const QJsonArray sent = agent->sentMessages;
    ASSERT_GE(sent.size(), 4);
    EXPECT_TRUE(QJsonDocument(sent).toJson(QJsonDocument::Compact).contains(page.toUtf8()));

    agent->deliver(QStringLiteral("The landing page is written"), {10, 20}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);
    EXPECT_TRUE(plugin.lastError(task.id).isEmpty());
    plugin.shutdown();
}

TEST(AiPluginTest, SaysWhichToolTheRunIsCallingWhileItIsCallingIt) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = workdir.path();
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_TRUE(plugin.executionDetail(task.id).isEmpty());

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    // A long command keeps the call open, so the card can be read while the tool is still running.
    int detailChanges = 0;
    // clang-format off
    QObject::connect(&plugin, &AiPlugin::taskRunStateChanged, &plugin, [&detailChanges](const QString&) { ++detailChanges; });
    clients.first()->deliverToolCalls({{QStringLiteral("c1"), QStringLiteral("run_command"), QJsonObject{{QStringLiteral("command"), test::TestProcesses::sleepingCommand(2)}}}});
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.executionDetail(task.id) == QStringLiteral("run_command"); }));
    // clang-format on
    EXPECT_EQ(plugin.executionPhase(task.id), ExecutionPhase::CallingTool);
    EXPECT_GT(detailChanges, 0);

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.stopTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_TRUE(plugin.executionDetail(task.id).isEmpty());
    plugin.shutdown();
}

TEST(AiPluginTest, KeepsWhatArrivedWhenTheProviderCutTheAnswerAndNamesTheBudgetAsTheReason) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    // The model was cut before it finished, so what arrived is kept and the reason says the budget ended the run.
    const qint64 budget = ModelConnections::outputBudget(AiTestsHelper::testConnection());
    clients.first()->deliver(QStringLiteral("I will now write the landing page"), {1972, budget}, QStringLiteral("length"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);
    EXPECT_EQ(plugin.lastStopReason(task.id), AgentStopReason::OutputBudget);
    EXPECT_TRUE(plugin.lastError(task.id).isEmpty());
    ASSERT_FALSE(plugin.conversation(task.id).isEmpty());
    EXPECT_EQ(plugin.conversation(task.id).last().content, QStringLiteral("I will now write the landing page"));
    EXPECT_TRUE(ChatRequests::truncatedByOutputBudget(QStringLiteral("max_tokens")));
    EXPECT_FALSE(ChatRequests::truncatedByOutputBudget(QStringLiteral("stop")));
    EXPECT_FALSE(ChatRequests::truncatedByOutputBudget(QStringLiteral("end_turn")));
    plugin.shutdown();
}

TEST(AiAgentSettingsViewTest, StartsFromTheEmptyStateAndOffersTheTemplateAndTheTags) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiTestsHelper::installEmptyProviderRows(host);
    host.settingsDocument = AiTestsHelper::settingsDocument({AiTestsHelper::testConnection()}, ModelConnections::connectionKey(AiTestsHelper::testConnection()), {});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    std::unique_ptr<QWidget> section(plugin.createSettingsSection(QStringLiteral("agents"), QStringLiteral("general"), nullptr));
    ASSERT_NE(section, nullptr);
    section->resize(760, 520);
    section->show();

    auto* grid = section->findChild<QTableWidget*>(QStringLiteral("aiAgentGrid"));
    auto* empty = section->findChild<QLabel*>(QStringLiteral("aiAgentEmpty"));
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(grid->isVisible());
    EXPECT_TRUE(empty->isVisible());

    AiAgentDialog dialog(host, {}, {}, plugin.connections(), nullptr);
    dialog.show();
    auto* prompt = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiAgentSystemPrompt"));
    auto* insertTemplate = dialog.findChild<QPushButton*>(QStringLiteral("aiAgentInsertTemplate"));
    auto* showTags = dialog.findChild<QPushButton*>(QStringLiteral("aiAgentShowTags"));
    auto* templateChooser = dialog.findChild<QComboBox*>(QStringLiteral("aiAgentTemplate"));
    ASSERT_NE(prompt, nullptr);
    ASSERT_NE(insertTemplate, nullptr);
    ASSERT_NE(showTags, nullptr);
    ASSERT_NE(templateChooser, nullptr);
    EXPECT_TRUE(prompt->toPlainText().isEmpty());

    // A dialog opens on the field it asks for first, because Qt builds the tab order from the order the widgets were created.
    QWidget* firstInChain = nullptr;

    for (QWidget* candidate = dialog.nextInFocusChain(); candidate != nullptr && candidate != &dialog && firstInChain == nullptr; candidate = candidate->nextInFocusChain()) {
        if ((candidate->focusPolicy() & Qt::TabFocus) != 0) {
            firstInChain = candidate;
        }
    }

    EXPECT_EQ(firstInChain, dialog.findChild<QLineEdit*>(QStringLiteral("aiAgentName")));

    // The height a control really takes is the one the shared style gives it, so the row is measured with that style applied.
    dialog.setStyleSheet(ui::ApplicationStyleSheet::applicationStyleSheet(host.theme()));

    // A row that mixes a selectable field with buttons carries one height, because two heights read as two kinds of row.
    EXPECT_EQ(insertTemplate->sizeHint().height(), templateChooser->sizeHint().height());
    EXPECT_EQ(showTags->sizeHint().height(), templateChooser->sizeHint().height());

    // Every template the catalog declares is offered, named by its own translation and sorted like every other list.
    ASSERT_EQ(templateChooser->count(), ProviderCatalog::promptTemplates().size());
    QStringList offered;
    for (int index = 0; index < templateChooser->count(); ++index) {
        offered.append(templateChooser->itemText(index));
        EXPECT_FALSE(templateChooser->itemText(index).startsWith(QStringLiteral("ai.template.")));
        EXPECT_FALSE(templateChooser->itemData(index, Qt::ToolTipRole).toString().isEmpty());
    }
    QStringList sorted = offered;
    sorted.sort(Qt::CaseInsensitive);
    EXPECT_EQ(offered, sorted);

    // Each one inserts its own body, carries the tags in place and names no tag the run cannot answer.
    QStringList bodies;
    for (int index = 0; index < templateChooser->count(); ++index) {
        templateChooser->setCurrentIndex(index);
        QTest::mouseClick(insertTemplate, Qt::LeftButton);
        const QString body = prompt->toPlainText();
        EXPECT_FALSE(body.isEmpty());
        EXPECT_TRUE(body.contains(QStringLiteral("{{SYSTEM_PROMPT_DATA}}")));
        EXPECT_TRUE(body.contains(QStringLiteral("{{TASK_WORKDIR}}")));
        EXPECT_TRUE(AgentPrompts::unknownPromptTags(body).isEmpty());
        EXPECT_FALSE(bodies.contains(body));
        bodies.append(body);
    }

    // The list of tags is shown where the prompt is written, without holding the loop for an answer nobody reads.
    QTest::mouseClick(showTags, Qt::LeftButton);
    auto* tags = dialog.findChild<QDialog*>(QStringLiteral("aiAgentTagsDialog"));
    ASSERT_NE(tags, nullptr);
    auto* tagsContent = tags->findChild<QTextBrowser*>(QStringLiteral("aiAgentTagsContent"));
    ASSERT_NE(tagsContent, nullptr);
    EXPECT_TRUE(tagsContent->toPlainText().contains(QStringLiteral("{{SYSTEM_PROMPT_DATA}}")));
    QPointer<QDialog> closing = tags;
    tags->reject();
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&closing]() { return closing.isNull(); }));
    // clang-format on

    // The identifier is spelled from the name while nobody has typed one, and stops following once somebody does.
    auto* identifier = dialog.findChild<QLineEdit*>(QStringLiteral("aiAgentIdentifier"));
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("aiAgentName"));
    ASSERT_NE(identifier, nullptr);
    ASSERT_NE(name, nullptr);
    EXPECT_LT(name->mapTo(&dialog, QPoint(0, 0)).y(), identifier->mapTo(&dialog, QPoint(0, 0)).y());
    // Nothing is written into the field while the name is typed, and what it is saved as is spelled from that name.
    QTest::keyClicks(name, QStringLiteral("Claudinho Review Bot"));
    EXPECT_TRUE(identifier->text().isEmpty());
    EXPECT_EQ(dialog.agent().id, QStringLiteral("claudinho-review-bot"));

    // A name that opens with something the identifier may not carry loses it rather than opening with it.
    name->clear();
    QTest::keyClicks(name, QStringLiteral("9 Reviewer!"));
    EXPECT_EQ(dialog.agent().id, QStringLiteral("reviewer"));

    // A prompt carrying a tag nobody declares refuses to be saved and says which one.
    auto* validation = dialog.findChild<QLabel*>(QStringLiteral("aiTaskValidation"));
    ASSERT_NE(identifier, nullptr);
    ASSERT_NE(name, nullptr);
    ASSERT_NE(validation, nullptr);
    identifier->setText(QStringLiteral("reviewer"));
    name->setText(QStringLiteral("Reviewer"));
    prompt->setPlainText(QStringLiteral("You are {{NOT_A_TAG}}"));
    QTest::mouseClick(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    EXPECT_NE(dialog.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_TRUE(validation->text().contains(QStringLiteral("NOT_A_TAG")));

    prompt->setPlainText(QStringLiteral("You are {{AGENT_NAME}}"));
    QTest::mouseClick(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
    ASSERT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(dialog.agent().id, QStringLiteral("reviewer"));
    EXPECT_EQ(dialog.agent().connectionKey, ModelConnections::connectionKey(AiTestsHelper::testConnection()));
    plugin.shutdown();
}

TEST(AiPluginTest, RefusesAConnectionAnAgentRunsOnAndStopsTheTasksOfAnAgentThatIsRemoved) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    AiPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.agents().size(), 1);

    // A connection an agent runs on is refused while that agent exists, and the alert names the agent that holds it.
    const auto removedConnection = test::TestFutures::awaitFuture(plugin.saveConnections({}, {}));
    EXPECT_EQ(removedConnection.error().code, QStringLiteral("ai_connection_in_use"));
    EXPECT_EQ(removedConnection.error().detail, AiTestsHelper::testAgent().name);
    EXPECT_EQ(plugin.connections().size(), 1);
    EXPECT_TRUE(host.translate(QStringLiteral("ai.error.connection-in-use")).arg(removedConnection.error().detail).contains(AiTestsHelper::testAgent().name));

    // An agent naming a connection nobody configured is refused where it is saved.
    AiAgent orphan = AiTestsHelper::testAgent();
    orphan.connectionKey = QStringLiteral("openai/retired-model");
    EXPECT_EQ(test::TestFutures::awaitFuture(plugin.saveAgents({orphan})).error().code, QStringLiteral("ai_connection_unknown"));

    // Removing the agent is allowed, and the task it was handed stops with the reason instead of waiting for nobody.
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.saveAgents({})).hasValue());
    EXPECT_TRUE(plugin.agents().isEmpty());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.lastError(task.id).contains(AiTestsHelper::testAgent().id); }));
    // clang-format on
    plugin.shutdown();
}

TEST(AiPluginTest, RefusesToRunATaskWhoseAgentIsNoLongerConfigured) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask orphan = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    orphan.agentId = QStringLiteral("retired-agent");
    AiTestsHelper::installAiRows(host, {workspace}, {orphan}, {});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // The task loaded, because an agent removed later must not stop the application from opening.
    ASSERT_EQ(plugin.tasks().size(), 1);
    const auto resolved = plugin.agentForTask(plugin.tasks().first());
    EXPECT_EQ(resolved.error().code, QStringLiteral("ai_agent_unknown"));

    const auto started = test::TestFutures::awaitFuture(plugin.startTask(orphan.id));
    EXPECT_EQ(started.error().code, QStringLiteral("ai_agent_unknown"));
    EXPECT_TRUE(clients.isEmpty());
    EXPECT_EQ(plugin.runState(orphan.id), TaskRunState::Idle);
    plugin.shutdown();
}

TEST(AiPluginTest, FailsAQueuedTaskWhoseConnectionIsGoneInsteadOfLeavingItWaitingForever) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask orphan = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    orphan.agentId = QStringLiteral("retired-agent");
    orphan.column = TaskColumn::Doing;
    AiTestsHelper::installAiRows(host, {workspace}, {orphan}, {orphan.id});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    // The restored queue dispatches it, the run fails with the reason and the card returns to To Do.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.hasLastExecution(orphan.id); }));
    // clang-format on
    EXPECT_EQ(plugin.lastExecutionStatus(orphan.id), ExecutionStatus::Failed);
    // The card names the agent that is gone rather than showing the sentence written for the log.
    EXPECT_EQ(plugin.lastError(orphan.id), host.translate(QStringLiteral("ai.error.agent-removed")).arg(orphan.agentId));
    EXPECT_TRUE(clients.isEmpty());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(orphan.id) == TaskRunState::Idle; }));
    // clang-format on
    plugin.shutdown();
}

// A command line agent has no catalog to list and no request body, so the dialog closes what would answer about neither.
// A provider that signs in on its own has no credential to configure, and a field that fills itself says so under itself in the ink of the theme.
// The identifier is spelled from the name when it is saved empty, and the field is left alone while the reader is still typing that name.
TEST(AiAgentDialogTest, SpellsTheIdentifierWhenItIsSavedEmptyAndWritesNothingWhileTheNameIsTyped) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiAgentDialog dialog(host, {}, {}, {}, nullptr);
    dialog.show();
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("aiAgentName"));
    auto* identifier = dialog.findChild<QLineEdit*>(QStringLiteral("aiAgentIdentifier"));
    ASSERT_NE(name, nullptr);
    ASSERT_NE(identifier, nullptr);

    QTest::keyClicks(name, QStringLiteral("Claudinho CLI"));
    EXPECT_TRUE(identifier->text().isEmpty()) << identifier->text().toStdString();
    EXPECT_EQ(dialog.agent().id, QStringLiteral("claudinho-cli"));

    // An identifier the reader typed is the one that is kept.
    QTest::keyClicks(identifier, QStringLiteral("chosen-name"));
    EXPECT_EQ(dialog.agent().id, QStringLiteral("chosen-name"));
}

TEST(AiConnectionDialogTest, HidesTheCredentialAProviderDoesNotUseAndSaysWhichFieldFillsItself) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();

    AiConnectionDialog dialog(host, AiTestsHelper::testConnection(), {}, nullptr);
    dialog.show();
    auto* provider = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionProvider"));
    auto* apiKey = dialog.findChild<QWidget*>(QStringLiteral("aiConnectionApiKey"));
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(apiKey, nullptr);
    EXPECT_TRUE(apiKey->isVisible());

    provider->setCurrentIndex(provider->findData(QStringLiteral("claude-cli")));
    EXPECT_FALSE(apiKey->isVisible());
    provider->setCurrentIndex(provider->findData(QStringLiteral("openai")));
    EXPECT_TRUE(apiKey->isVisible());

    // The hint reads in the muted ink and the caption size of the active theme rather than in a colour of its own.
    auto* hint = dialog.findChild<QLabel*>(QStringLiteral("settingsHint"));
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->text().isEmpty());
    const workpane::ui::Theme& theme = workpane::ui::ThemeManager::instance().theme();
    EXPECT_EQ(hint->palette().color(QPalette::WindowText), theme.color(workpane::ui::ThemeColor::TextMuted));
    EXPECT_EQ(hint->font().pointSize(), theme.font(workpane::ui::ThemeFont::Caption).pointSize());

    // The hint sits under the field it explains and starts where that field starts, rather than on a row of its own.
    auto* named = dialog.findChild<QLineEdit*>(QStringLiteral("aiConnectionDisplayName"));
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(hint->parentWidget(), named->parentWidget());
    EXPECT_EQ(hint->x(), named->x());
    EXPECT_GT(hint->y(), named->y());
    EXPECT_LE(hint->y() - (named->y() + named->height()), theme.metric(workpane::ui::ThemeMetric::ControlVerticalPadding));
}

TEST(AiConnectionDialogTest, OffersNeitherModelDiscoveryNorExtraParametersToACommandLineAgent) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();

    AiConnectionDialog dialog(host, AiTestsHelper::testConnection(), {}, nullptr);
    dialog.show();
    auto* provider = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionProvider"));
    auto* refresh = dialog.findChild<QToolButton*>(QStringLiteral("aiConnectionRefreshModels"));
    auto* extras = dialog.findChild<QWidget*>(QStringLiteral("aiConnectionExtraSection"));
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(refresh, nullptr);
    ASSERT_NE(extras, nullptr);

    // A provider reached over a wire owns both, because it answers a catalog and carries a request body.
    EXPECT_TRUE(refresh->isVisible());
    EXPECT_TRUE(extras->isVisible());

    provider->setCurrentIndex(provider->findData(QStringLiteral("claude-cli")));

    EXPECT_FALSE(refresh->isVisible());
    EXPECT_FALSE(extras->isVisible());

    // Coming back restores them, because the shape follows the selection rather than the first provider shown.
    provider->setCurrentIndex(provider->findData(QStringLiteral("openai")));

    EXPECT_TRUE(refresh->isVisible());
    EXPECT_TRUE(extras->isVisible());
}

TEST(AiConnectionDialogTest, KeepsTheReplacedParameterEditorsAliveUntilTheClickThatCausedItIsOver) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();

    AiConnectionDialog dialog(host, AiTestsHelper::testConnection(), {}, nullptr);
    dialog.show();
    auto* model = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionModel"));
    ASSERT_NE(model, nullptr);

    // The rebuild is reached from the editing that ends when the user clicks the very editor it replaces.
    const QPointer<QWidget> replaced = dialog.findChild<QWidget*>(QStringLiteral("aiParameter.temperature"));
    ASSERT_FALSE(replaced.isNull());
    model->setCurrentText(QStringLiteral("o3-mini"));
    emit model->lineEdit()->editingFinished();

    EXPECT_FALSE(replaced.isNull());
    EXPECT_FALSE(replaced->isVisible());
    EXPECT_NE(dialog.findChild<QWidget*>(QStringLiteral("aiParameter.reasoningEffort")), nullptr);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(replaced.isNull());
}

TEST(AiConnectionDialogTest, ExplainsWhyAConnectionCannotBeSaved) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();

    ModelConnection stored = AiTestsHelper::testConnection();
    stored.displayName = QStringLiteral("Reviewer");
    stored.extraParameters = QJsonObject{{QStringLiteral("seed"), 7}};
    AiConnectionDialog dialog(host, stored, {QStringLiteral("anthropic/claude-opus-5")}, nullptr);
    dialog.show();

    // Opening an existing connection presents what it stored rather than what its provider declares.
    EXPECT_EQ(dialog.connection().apiKey, QStringLiteral("sk-test"));
    EXPECT_EQ(dialog.connection().displayName, QStringLiteral("Reviewer"));
    EXPECT_EQ(dialog.connection().modelId, QStringLiteral("gpt-4o"));
    EXPECT_EQ(dialog.connection().extraParameters.value(QStringLiteral("seed")).toInt(), 7);

    auto* validation = dialog.findChild<QLabel*>(QStringLiteral("aiTaskValidation"));
    auto* extraValidation = dialog.findChild<QLabel*>(QStringLiteral("aiConnectionExtraValidation"));
    auto* extra = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiConnectionExtraParameters"));
    auto* model = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionModel"));
    auto* apiKey = dialog.findChild<ui::SecretField*>(QStringLiteral("aiConnectionApiKey"));
    auto* save = dialog.findChild<QPushButton*>(QStringLiteral("primaryButton"));
    ASSERT_NE(validation, nullptr);
    ASSERT_NE(extraValidation, nullptr);
    ASSERT_NE(extra, nullptr);
    ASSERT_NE(model, nullptr);
    ASSERT_NE(apiKey, nullptr);
    ASSERT_NE(save, nullptr);

    // A provider without its credential says so instead of leaving the dialog silently unsaved.
    dialog.resize(dialog.sizeHint());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([extra]() { return extra->height() > 0; }));
    // clang-format on
    const int roomBefore = extra->height();
    const int dialogBefore = dialog.height();
    apiKey->setValue(QString{});
    dialog.accept();
    EXPECT_TRUE(dialog.isVisible());
    EXPECT_TRUE(validation->isVisible());

    // The dialog grows to carry the message, so the field the message appeared under keeps its room.
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&dialog, dialogBefore]() { return dialog.height() > dialogBefore; }));
    // clang-format on
    EXPECT_GE(extra->height(), roomBefore);
    EXPECT_EQ(validation->text(), QStringLiteral("Enter the API key this provider requires"));

    apiKey->setValue(QStringLiteral("sk-test"));
    model->setCurrentText(QString{});
    dialog.accept();
    EXPECT_TRUE(dialog.isVisible());
    EXPECT_EQ(validation->text(), QStringLiteral("Choose the model this provider should run"));

    // A document that cannot be sent is reported while it is typed and blocks the confirm action.
    model->setCurrentText(QStringLiteral("gpt-4o"));
    emit model->lineEdit()->editingFinished();
    extra->setPlainText(QStringLiteral("{\"seed\": }"));
    EXPECT_TRUE(extraValidation->isVisible());
    EXPECT_FALSE(save->isEnabled());

    extra->setPlainText(QStringLiteral("[1, 2]"));
    EXPECT_TRUE(extraValidation->isVisible());
    EXPECT_EQ(extraValidation->text(), QStringLiteral("The extra parameters must be a JSON object"));
    EXPECT_FALSE(save->isEnabled());

    extra->setPlainText(QStringLiteral("{\"seed\": 42}"));
    EXPECT_FALSE(extraValidation->isVisible());
    EXPECT_TRUE(save->isEnabled());
    EXPECT_EQ(dialog.connection().extraParameters.value(QStringLiteral("seed")).toInt(), 42);

    // A pair another connection already configures is refused, because one key names one configuration.
    auto* provider = dialog.findChild<QComboBox*>(QStringLiteral("aiConnectionProvider"));
    ASSERT_NE(provider, nullptr);
    provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
    model->setCurrentText(QStringLiteral("claude-opus-5"));
    emit model->lineEdit()->editingFinished();
    dialog.accept();
    EXPECT_TRUE(dialog.isVisible());
    EXPECT_EQ(validation->text(), QStringLiteral("This provider and model pair is already configured"));
}

TEST(AiPluginTest, RecordsEverySentAndReceivedExchangeNewestFirst) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    const AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {});

    FakeChatClient* client = nullptr;
    // clang-format off
    AiPlugin plugin([&client](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); client = created.get(); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_EQ(plugin.executionPhase(task.id), ExecutionPhase::Idle);

    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client != nullptr && client->sendCalls == 1; }));
    // clang-format on
    EXPECT_EQ(plugin.executionPhase(task.id), ExecutionPhase::Sending);

    emit client->requestSent(QStringLiteral("https://api.openai.com/v1/chat/completions"), QStringLiteral("{\"model\":\"gpt-4o\"}"));
    client->deliver(QStringLiteral("answer"), {15, 390}, QStringLiteral("end_turn"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.executionPhase(task.id), ExecutionPhase::Idle);

    QVector<ExecutionLogEntry> written;

    for (const auto& recorded : host.databaseExecutions) {
        if (!recorded.value(QStringLiteral("statement")).toString().contains(QStringLiteral("INSERT INTO ai_tasks_logs"))) {
            continue;
        }
        const QVariantList bindings = recorded.value(QStringLiteral("bindings")).toList();
        ExecutionLogEntry entry;
        entry.sequence = bindings.at(2).toLongLong();
        entry.kind = AiTaskRepository::parseExecutionLogKind(bindings.at(5).toString()).value();
        entry.detail = bindings.at(6).toString();
        written.append(entry);
    }

    QVector<ExecutionLogKind> kinds;

    for (const auto& entry : written) {
        kinds.append(entry.kind);
    }

    EXPECT_TRUE(kinds.contains(ExecutionLogKind::Started));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::RequestSent));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::FirstTokenReceived));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::ResponseReceived));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::UsageReported));
    EXPECT_TRUE(kinds.contains(ExecutionLogKind::Succeeded));

    // Everything sent and everything received is recorded verbatim next to its translatable event.
    for (const auto& entry : written) {
        if (entry.kind == ExecutionLogKind::RequestSent) {
            EXPECT_TRUE(entry.detail.contains(QStringLiteral("chat/completions")));
            EXPECT_TRUE(entry.detail.contains(QStringLiteral("gpt-4o")));
        }
        if (entry.kind == ExecutionLogKind::ResponseReceived) {
            EXPECT_EQ(entry.detail, QStringLiteral("answer"));
        }
        if (entry.kind == ExecutionLogKind::UsageReported) {
            EXPECT_TRUE(entry.detail.contains(QStringLiteral("15")));
            EXPECT_TRUE(entry.detail.contains(QStringLiteral("end_turn")));
        }
    }

    // The sequence is monotonic so the reader can present the newest entry first.
    for (qsizetype index = 1; index < written.size(); ++index) {
        EXPECT_GT(written.at(index).sequence, written.at(index - 1).sequence);
    }

    plugin.shutdown();
}

TEST(AiChatClientTest, ParsesTheRecordedAnthropicMessageStream) {
    // Recorded from the published Anthropic streaming example.
    const QByteArray stream = QByteArrayLiteral("event: message_start\n"
                                                "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1nZ\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"claude-opus-5\",\"stop_reason\":null,\"stop_sequence\":null,\"usage\":{\"input_tokens\":25,\"output_tokens\":1}}}\n\n"
                                                "event: content_block_start\n"
                                                "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
                                                "event: ping\n"
                                                "data: {\"type\": \"ping\"}\n\n"
                                                "event: content_block_delta\n"
                                                "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n\n"
                                                "event: content_block_delta\n"
                                                "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"!\"}}\n\n"
                                                "event: content_block_stop\n"
                                                "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
                                                "event: message_delta\n"
                                                "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":15}}\n\n"
                                                "event: message_stop\n"
                                                "data: {\"type\":\"message_stop\"}\n\n");

    RecordedStreamServer server(stream);
    ASSERT_TRUE(server.listen());

    const ProviderDescriptor* anthropic = ProviderCatalog::findProvider(QStringLiteral("anthropic"));
    ASSERT_NE(anthropic, nullptr);
    const QString model = QStringLiteral("claude-3-haiku-20240307");
    const ModelConnection connection{anthropic->id, model, {}, QStringLiteral("sk-test"), {}, ProviderCatalog::defaultParameters(*anthropic, model), {}};

    AiRequestGate clientGate;
    AiHttpChatClient client(clientGate);
    QStringList deltas;
    QString content;
    ChatUsage usage;
    QString finishReason;
    bool completed = false;
    // clang-format off
    QObject::connect(&client, &AiChatClient::contentReceived, &client, [&deltas](const QString& delta) { deltas.append(delta); });
    QObject::connect(&client, &AiChatClient::finished, &client, [&](const QString& text, const QVector<ToolCall>&, ChatUsage reported, const QString& reason) { content = text; usage = reported; finishReason = reason; completed = true; });
    // clang-format on
    // clang-format off
    client.send({connection, server.address(), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
    // clang-format on

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return completed; }));
    // clang-format on
    EXPECT_EQ(content, QStringLiteral("Hello!"));
    EXPECT_EQ(deltas, QStringList({QStringLiteral("Hello"), QStringLiteral("!")}));
    EXPECT_EQ(usage.inputTokens, 25);
    EXPECT_EQ(usage.outputTokens, 15);
    EXPECT_EQ(finishReason, QStringLiteral("end_turn"));

    const QJsonObject sent = QJsonDocument::fromJson(server.requestBody()).object();
    EXPECT_EQ(sent.value(QStringLiteral("model")).toString(), model);
    EXPECT_TRUE(sent.value(QStringLiteral("stream")).toBool());
}

TEST(AiChatClientTest, ParsesTheRecordedOpenAiChatCompletionStream) {
    // Recorded from the published OpenAI chat completion chunk format.
    const QByteArray stream = QByteArrayLiteral("data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":\"\"},\"finish_reason\":null}]}\n\n"
                                                "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hello\"},\"finish_reason\":null}]}\n\n"
                                                "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\" world\"},\"finish_reason\":null}]}\n\n"
                                                "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
                                                "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"created\":1694268190,\"model\":\"gpt-4o\",\"choices\":[],\"usage\":{\"prompt_tokens\":9,\"completion_tokens\":12,\"total_tokens\":21}}\n\n"
                                                "data: [DONE]\n\n");

    RecordedStreamServer server(stream);
    ASSERT_TRUE(server.listen());

    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk-test"), {}, ProviderCatalog::defaultParameters(*openai, model), {}};

    AiRequestGate clientGate;
    AiHttpChatClient client(clientGate);
    QString content;
    ChatUsage usage;
    QString finishReason;
    bool completed = false;
    // clang-format off
    QObject::connect(&client, &AiChatClient::finished, &client, [&](const QString& text, const QVector<ToolCall>&, ChatUsage reported, const QString& reason) { content = text; usage = reported; finishReason = reason; completed = true; });
    // clang-format on
    // clang-format off
    client.send({connection, server.address(), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
    // clang-format on

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return completed; }));
    // clang-format on
    EXPECT_EQ(content, QStringLiteral("Hello world"));
    EXPECT_EQ(usage.inputTokens, 9);
    EXPECT_EQ(usage.outputTokens, 12);
    EXPECT_EQ(finishReason, QStringLiteral("stop"));
}

TEST(AiToolRegistryTest, NamesEveryCallTheWayAReaderSpellsItAndSaysWhatItIsDoing) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);

    // A declared tool is named by the catalog and says the one thing that explains the call.
    const ToolPresentation search = registry.presentation(QStringLiteral("web_search"), QJsonObject{{QStringLiteral("query"), QStringLiteral("bitcoin price USD current")}, {QStringLiteral("count"), 5}});
    EXPECT_EQ(search.title, QStringLiteral("Web Search"));
    EXPECT_EQ(search.activity, QStringLiteral("Searching for: bitcoin price USD current"));
    EXPECT_FALSE(search.activity.contains(QStringLiteral("count")));

    // A tool that carries nothing worth naming shows its name alone.
    const ToolPresentation skills = registry.presentation(QStringLiteral("list_skills"), {});
    EXPECT_EQ(skills.title, QStringLiteral("List Skills"));
    EXPECT_TRUE(skills.activity.isEmpty());

    // A long argument keeps its beginning and its end, because the end of a path is what names the file.
    const QString deep = QStringLiteral("/Users/paulo/Developer/workspaces/node/tibia-lp2/assets/hero-background.svg");
    const ToolPresentation written = registry.presentation(QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), deep}, {QStringLiteral("content"), QStringLiteral("<svg/>")}});
    EXPECT_EQ(written.title, QStringLiteral("Write File"));
    EXPECT_TRUE(written.activity.endsWith(QStringLiteral("hero-background.svg")));
    EXPECT_TRUE(written.activity.contains(QStringLiteral("…")));
    EXPECT_FALSE(written.activity.contains(QStringLiteral("<svg/>")));

    // A tool nobody declared is named the way it was spelled, with the marks that separate its words read as spaces.
    const ToolPresentation published = registry.presentation(QStringLiteral("weather.get_forecast"), {});
    EXPECT_EQ(published.title, QStringLiteral("Get Forecast"));
}

TEST(AiToolContractTest, ValidatesTheDeclaredSchemaOfEveryTool) {
    const ToolSchema valid{QStringLiteral("generate_image"), QStringLiteral("ai.tool.generate-image"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{{QStringLiteral("prompt"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}}};
    EXPECT_TRUE(ToolContracts::validateToolSchema(valid).hasValue());

    ToolSchema upperCase = valid;
    upperCase.name = QStringLiteral("GenerateImage");
    EXPECT_EQ(ToolContracts::validateToolSchema(upperCase).error().code, QStringLiteral("ai_tool_invalid"));

    ToolSchema withoutDescription = valid;
    withoutDescription.descriptionKey.clear();
    EXPECT_EQ(ToolContracts::validateToolSchema(withoutDescription).error().code, QStringLiteral("ai_tool_invalid"));

    ToolSchema scalarParameters = valid;
    scalarParameters.parameters = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
    EXPECT_EQ(ToolContracts::validateToolSchema(scalarParameters).error().code, QStringLiteral("ai_tool_invalid"));
}

TEST(AiToolContractTest, SerializesToolsTurnsAndResultsForEachWireProtocol) {
    const ToolSchema tool{QStringLiteral("get_weather"), QStringLiteral("ai.tool.get-weather"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{{QStringLiteral("location"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}}};
    // clang-format off
    const auto translate = [](const QString& key) { return QStringLiteral("described ") + key; };
    // clang-format on

    const QJsonArray openAiTools = ToolContracts::serializeTools(WireProtocol::OpenAiCompatible, {tool}, translate);
    ASSERT_EQ(openAiTools.size(), 1);
    EXPECT_EQ(openAiTools.first().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("function"));
    EXPECT_EQ(openAiTools.first().toObject().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString(), tool.name);
    EXPECT_TRUE(openAiTools.first().toObject().value(QStringLiteral("function")).toObject().contains(QStringLiteral("parameters")));

    const QJsonArray anthropicTools = ToolContracts::serializeTools(WireProtocol::Anthropic, {tool}, translate);
    ASSERT_EQ(anthropicTools.size(), 1);
    EXPECT_EQ(anthropicTools.first().toObject().value(QStringLiteral("name")).toString(), tool.name);
    EXPECT_TRUE(anthropicTools.first().toObject().contains(QStringLiteral("input_schema")));

    const ToolCall call{QStringLiteral("toolu_01"), tool.name, QJsonObject{{QStringLiteral("location"), QStringLiteral("San Francisco, CA")}}};
    const QJsonObject openAiTurn = ToolContracts::serializeAssistantTurn(WireProtocol::OpenAiCompatible, QStringLiteral("checking"), {call});
    ASSERT_EQ(openAiTurn.value(QStringLiteral("tool_calls")).toArray().size(), 1);
    EXPECT_EQ(openAiTurn.value(QStringLiteral("tool_calls")).toArray().first().toObject().value(QStringLiteral("id")).toString(), call.id);

    const QJsonObject anthropicTurn = ToolContracts::serializeAssistantTurn(WireProtocol::Anthropic, QStringLiteral("checking"), {call});
    const QJsonArray blocks = anthropicTurn.value(QStringLiteral("content")).toArray();
    ASSERT_EQ(blocks.size(), 2);
    EXPECT_EQ(blocks.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    EXPECT_EQ(blocks.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("tool_use"));

    const ToolResult result{call.id, QStringLiteral("15 degrees"), false};
    const QVector<QJsonObject> openAiResults = ToolContracts::serializeToolResults(WireProtocol::OpenAiCompatible, {result});
    ASSERT_EQ(openAiResults.size(), 1);
    EXPECT_EQ(openAiResults.first().value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
    EXPECT_EQ(openAiResults.first().value(QStringLiteral("tool_call_id")).toString(), call.id);

    const QVector<QJsonObject> anthropicResults = ToolContracts::serializeToolResults(WireProtocol::Anthropic, {result});
    ASSERT_EQ(anthropicResults.size(), 1);
    EXPECT_EQ(anthropicResults.first().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    EXPECT_EQ(anthropicResults.first().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("tool_use_id")).toString(), call.id);

    const ToolResult failure{call.id, QStringLiteral("boom"), true};
    EXPECT_TRUE(ToolContracts::serializeToolResults(WireProtocol::Anthropic, {failure}).first().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("is_error")).toBool());
}

TEST(AiPluginTest, CarriesThePictureAToolReadIntoTheRequestThatFollowsIt) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());
    QImage picture(4, 4, QImage::Format_RGB32);
    picture.fill(Qt::red);
    ASSERT_TRUE(picture.save(workdir.filePath(QStringLiteral("shot.png"))));

    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    task.workdir = workdir.path();

    ModelConnection seeing = AiTestsHelper::testConnection();
    seeing.modelId = QStringLiteral("chatgpt-4o-latest");
    seeing.parameters = ProviderCatalog::defaultParameters(*ProviderCatalog::findProvider(seeing.providerId), seeing.modelId);
    AiAgent reader = AiTestsHelper::testAgent();
    reader.connectionKey = ModelConnections::connectionKey(seeing);
    AiTestsHelper::installAiRows(host, {workspace}, {task}, {}, {seeing}, {reader});

    QVector<FakeChatClient*> clients;
    // clang-format off
    AiPlugin plugin([&clients](AiRequestGate&, const ModelConnection&) { auto created = std::make_unique<FakeChatClient>(); clients.append(created.get()); return created; });
    // clang-format on
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(test::TestFutures::awaitFuture(plugin.startTask(task.id)).hasValue());
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return clients.size() == 1; }));
    // clang-format on

    FakeChatClient* seeingAgent = clients.first();
    seeingAgent->deliverToolCalls({{QStringLiteral("call-1"), QStringLiteral("read_image"), QJsonObject{{QStringLiteral("path"), QStringLiteral("shot.png")}}}});
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([seeingAgent]() { return seeingAgent->sendCalls == 2; }));
    // clang-format on

    // The picture the tool read reaches the model, because a tool result that carries only its text shows nothing.
    bool carried = false;

    for (const auto& message : seeingAgent->sentMessages) {
        for (const auto& block : message.toObject().value(QStringLiteral("content")).toArray()) {
            const QJsonObject entry = block.toObject();
            carried = carried || entry.value(QStringLiteral("type")).toString() == QStringLiteral("image_url") || entry.value(QStringLiteral("type")).toString() == QStringLiteral("image");
        }
    }

    EXPECT_TRUE(carried);

    plugin.shutdown();
}

TEST(AiToolContractTest, ShortensTheTextOfAResultThatCarriesAnImageWithoutLosingThePicture) {
    const QString huge(40000, QLatin1Char('x'));
    const QByteArray pixels = QByteArrayLiteral("-pretend-this-is-a-picture-");
    const QVector<ToolResult> results{{QStringLiteral("call-1"), QStringLiteral("head-marker") + huge + QStringLiteral("tail-marker"), false, pixels, QByteArrayLiteral("image/png")}, {QStringLiteral("call-2"), QStringLiteral("head-marker") + huge + QStringLiteral("tail-marker"), false, {}, {}}};
    QJsonArray messages;

    for (const auto& message : ToolContracts::serializeToolResults(WireProtocol::Anthropic, results)) {
        messages.append(message);
    }

    ASSERT_GT(ToolContracts::pruneToolResults(messages, ToolContracts::estimateTokens(messages) / 2), 0);

    // The text of both results is shortened, and the picture the first one carries is still there.
    const QJsonArray blocks = messages.first().toObject().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(blocks.size(), 2);
    const QJsonArray carried = blocks.first().toObject().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(carried.size(), 2);
    const QString shortened = carried.first().toObject().value(QStringLiteral("text")).toString();
    EXPECT_TRUE(shortened.startsWith(QStringLiteral("head-marker")));
    EXPECT_TRUE(shortened.endsWith(QStringLiteral("tail-marker")));
    EXPECT_LT(shortened.size(), huge.size());
    EXPECT_EQ(carried.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image"));
    EXPECT_EQ(QByteArray::fromBase64(carried.at(1).toObject().value(QStringLiteral("source")).toObject().value(QStringLiteral("data")).toString().toUtf8()), pixels);

    const QString plain = blocks.at(1).toObject().value(QStringLiteral("content")).toString();
    EXPECT_TRUE(plain.startsWith(QStringLiteral("head-marker")));
    EXPECT_TRUE(plain.endsWith(QStringLiteral("tail-marker")));
}

TEST(AiToolContractTest, CarriesAnImageInTheShapeEachProtocolAccepts) {
    const QByteArray pixels = QByteArrayLiteral("\x89PNG\r\n\x1a\n-pretend-this-is-a-picture");
    const QVector<ToolResult> results{{QStringLiteral("call-1"), QStringLiteral("shot.png"), false, pixels, QByteArrayLiteral("image/png")}, {QStringLiteral("call-2"), QStringLiteral("done"), false, {}, {}}};

    // The Anthropic API accepts the image inside the result of the tool that read it.
    const QVector<QJsonObject> anthropic = ToolContracts::serializeToolResults(WireProtocol::Anthropic, results);
    ASSERT_EQ(anthropic.size(), 1);
    const QJsonArray blocks = anthropic.first().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(blocks.size(), 2);
    const QJsonArray carried = blocks.first().toObject().value(QStringLiteral("content")).toArray();
    ASSERT_EQ(carried.size(), 2);
    EXPECT_EQ(carried.first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("shot.png"));
    EXPECT_EQ(carried.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image"));
    const QJsonObject source = carried.at(1).toObject().value(QStringLiteral("source")).toObject();
    EXPECT_EQ(source.value(QStringLiteral("media_type")).toString(), QStringLiteral("image/png"));
    EXPECT_EQ(QByteArray::fromBase64(source.value(QStringLiteral("data")).toString().toUtf8()), pixels);
    EXPECT_TRUE(blocks.at(1).toObject().value(QStringLiteral("content")).isString());

    // The OpenAI tool message carries text alone, so the image follows it as the user turn that shows it.
    const QVector<QJsonObject> openAi = ToolContracts::serializeToolResults(WireProtocol::OpenAiCompatible, results);
    ASSERT_EQ(openAi.size(), 3);
    EXPECT_EQ(openAi.at(0).value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
    EXPECT_EQ(openAi.at(0).value(QStringLiteral("content")).toString(), QStringLiteral("shot.png"));
    EXPECT_EQ(openAi.at(1).value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
    EXPECT_EQ(openAi.at(2).value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    const QJsonArray images = openAi.at(2).value(QStringLiteral("content")).toArray();
    ASSERT_EQ(images.size(), 1);
    EXPECT_EQ(images.first().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image_url"));
    EXPECT_TRUE(images.first().toObject().value(QStringLiteral("image_url")).toObject().value(QStringLiteral("url")).toString().startsWith(QStringLiteral("data:image/png;base64,")));

    // A turn that read no image adds no user turn at all.
    const QVector<ToolResult> textOnly{{QStringLiteral("call-3"), QStringLiteral("done"), false, {}, {}}};
    EXPECT_EQ(ToolContracts::serializeToolResults(WireProtocol::OpenAiCompatible, textOnly).size(), 1);
    EXPECT_EQ(ToolContracts::serializeToolResults(WireProtocol::Anthropic, textOnly).size(), 1);
}

TEST(AiToolContractTest, ShortensOldToolResultsBeforeAnyTurnIsDropped) {
    const QString huge(40000, QLatin1Char('x'));
    QJsonArray messages{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QStringLiteral("instructions")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("do the task")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QStringLiteral("looking")}}, QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("tool_call_id"), QStringLiteral("call-1")}, {QStringLiteral("content"), QStringLiteral("head-marker") + huge + QStringLiteral("tail-marker")}}};
    const qsizetype turnsBefore = messages.size();
    const qint64 limit = ToolContracts::estimateTokens(messages) / 2;

    const qsizetype pruned = ToolContracts::pruneToolResults(messages, limit);
    EXPECT_EQ(pruned, 1);
    EXPECT_EQ(messages.size(), turnsBefore);
    const QString shortened = messages.at(3).toObject().value(QStringLiteral("content")).toString();
    EXPECT_TRUE(shortened.startsWith(QStringLiteral("head-marker")));
    EXPECT_TRUE(shortened.endsWith(QStringLiteral("tail-marker")));
    EXPECT_LT(shortened.size(), huge.size());

    // Nothing but a tool result is touched, and a conversation that already fits is left alone.
    EXPECT_EQ(messages.at(2).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("looking"));
    QJsonArray fitting{QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("content"), QStringLiteral("short")}}};
    EXPECT_EQ(ToolContracts::pruneToolResults(fitting, 1000000), 0);

    // An Anthropic tool result carries its text inside its blocks and is shortened the same way.
    QJsonArray anthropic{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_result")}, {QStringLiteral("tool_use_id"), QStringLiteral("call-1")}, {QStringLiteral("content"), QStringLiteral("head-marker") + huge + QStringLiteral("tail-marker")}}}}}};
    EXPECT_EQ(ToolContracts::pruneToolResults(anthropic, ToolContracts::estimateTokens(anthropic) / 2), 1);
    const QString block = anthropic.at(0).toObject().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("content")).toString();
    EXPECT_TRUE(block.startsWith(QStringLiteral("head-marker")));
    EXPECT_TRUE(block.endsWith(QStringLiteral("tail-marker")));
}

TEST(AiToolContractTest, RebuildsToolCallsFromTheRecordedAnthropicStream) {
    // Recorded from the published Anthropic tool use streaming example.
    const QStringList events{QStringLiteral(R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_01T1x1fJ34qAmk2tNTrN7Up6","name":"get_weather","input":{}}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":""}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"location\":"}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":" \"San"}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":" Francisc"}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"o,"}})"), QStringLiteral(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":" CA\"}"}})"), QStringLiteral(R"({"type":"content_block_stop","index":1})")};

    ToolCallAccumulator accumulator(WireProtocol::Anthropic);
    EXPECT_TRUE(accumulator.empty());

    for (const auto& event : events) {
        accumulator.consume(QJsonDocument::fromJson(event.toUtf8()).object());
    }

    const auto calls = accumulator.calls();
    ASSERT_TRUE(calls.hasValue()) << calls.error().code.toStdString();
    ASSERT_EQ(calls.value().size(), 1);
    EXPECT_EQ(calls.value().first().id, QStringLiteral("toolu_01T1x1fJ34qAmk2tNTrN7Up6"));
    EXPECT_EQ(calls.value().first().name, QStringLiteral("get_weather"));
    EXPECT_EQ(calls.value().first().arguments.value(QStringLiteral("location")).toString(), QStringLiteral("San Francisco, CA"));
}

TEST(AiToolContractTest, RebuildsToolCallsFromTheRecordedOpenAiStream) {
    // Recorded from the published OpenAI streamed tool call format.
    const QStringList events{QStringLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"call_abc123","type":"function","function":{"name":"get_weather","arguments":""}}]}}]})"), QStringLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"loc"}}]}}]})"), QStringLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"function":{"arguments":"ation\":\"Paris\"}"}}]}}]})"), QStringLiteral(R"({"choices":[{"index":0,"delta":{},"finish_reason":"tool_calls"}]})")};

    ToolCallAccumulator accumulator(WireProtocol::OpenAiCompatible);

    for (const auto& event : events) {
        accumulator.consume(QJsonDocument::fromJson(event.toUtf8()).object());
    }

    const auto calls = accumulator.calls();
    ASSERT_TRUE(calls.hasValue()) << calls.error().code.toStdString();
    ASSERT_EQ(calls.value().size(), 1);
    EXPECT_EQ(calls.value().first().id, QStringLiteral("call_abc123"));
    EXPECT_EQ(calls.value().first().arguments.value(QStringLiteral("location")).toString(), QStringLiteral("Paris"));

    accumulator.clear();
    EXPECT_TRUE(accumulator.empty());
}

TEST(AiToolContractTest, AnswersAMalformedToolCallAndRefusesOneWithoutItsIdentity) {
    // A call whose arguments could not be read still reaches the model, carrying what arrived with it.
    ToolCallAccumulator truncated(WireProtocol::OpenAiCompatible);
    truncated.consume(QJsonDocument::fromJson(QByteArrayLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"call_1","function":{"name":"get_weather","arguments":"{\"location\":"}}]}}]})")).object());
    const auto unreadable = truncated.calls();
    ASSERT_TRUE(unreadable.hasValue());
    ASSERT_EQ(unreadable.value().size(), 1);
    EXPECT_EQ(unreadable.value().first().name, QStringLiteral("get_weather"));
    EXPECT_EQ(unreadable.value().first().unreadableArguments, QStringLiteral("{\"location\":"));
    EXPECT_TRUE(unreadable.value().first().arguments.isEmpty());

    // A call nobody can answer has no identity to answer it with, so that one still ends the run.
    ToolCallAccumulator withoutName(WireProtocol::OpenAiCompatible);
    withoutName.consume(QJsonDocument::fromJson(QByteArrayLiteral(R"({"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"call_1","function":{"arguments":"{}"}}]}}]})")).object());
    EXPECT_EQ(withoutName.calls().error().code, QStringLiteral("ai_tool_call_invalid"));

    // A tool call carrying no argument fragment is still a valid call with an empty object.
    ToolCallAccumulator withoutArguments(WireProtocol::Anthropic);
    withoutArguments.consume(QJsonDocument::fromJson(QByteArrayLiteral(R"({"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_1","name":"list_files","input":{}}})")).object());
    const auto calls = withoutArguments.calls();
    ASSERT_TRUE(calls.hasValue());
    EXPECT_TRUE(calls.value().first().arguments.isEmpty());
}

// A pipe decides where a chunk ends, so a character split across two reads is finished by the next one rather than lost.
TEST(AiCommandRunnerTest, KeepsACharacterSplitAcrossTwoReadsOfTheOutput) {
    qputenv("WORKPANE_TEST_SPLIT_OUTPUT", QByteArrayLiteral("1"));
    AiCommandRunner runner;
    QString output;
    int code = -1;
    bool finished = false;
    // clang-format off
    QObject::connect(&runner, &AiCommandRunner::finished, &runner, [&output, &code, &finished](int exitCode, const QString& text) { code = exitCode; output = text; finished = true; });
    QObject::connect(&runner, &AiCommandRunner::failed, &runner, [&finished](const Error&) { finished = true; });
    // clang-format on

    runner.startProgram(QCoreApplication::applicationFilePath(), {}, QDir::currentPath(), 0);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&finished]() { return finished; }));
    // clang-format on
    qunsetenv("WORKPANE_TEST_SPLIT_OUTPUT");

    EXPECT_EQ(code, 0);
    EXPECT_EQ(output, QStringLiteral("março de 2026 \U0001F600 fim\n"));
}

TEST(AiCommandRunnerTest, RunsACommandAndReportsItsOutputAndExitCode) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    AiCommandRunner runner;
    QString output;
    int exitCode = -1;
    bool completed = false;
    // clang-format off
    QObject::connect(&runner, &AiCommandRunner::finished, &runner, [&](int code, const QString& text) { exitCode = code; output = text; completed = true; });
    // clang-format on
    runner.start(QStringLiteral("echo workpane"), workdir.path(), 30);

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return completed; }));
    // clang-format on
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(output.contains(QStringLiteral("workpane")));
    EXPECT_FALSE(runner.running());
}

TEST(AiCommandRunnerTest, ReportsANonZeroExitCodeAndRejectsInvalidInput) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    AiCommandRunner failing;
    int exitCode = -1;
    bool completed = false;
    // clang-format off
    QObject::connect(&failing, &AiCommandRunner::finished, &failing, [&](int code, const QString&) { exitCode = code; completed = true; });
    // clang-format on
    failing.start(QStringLiteral("exit 3"), workdir.path(), 30);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return completed; }));
    // clang-format on
    EXPECT_EQ(exitCode, 3);

    AiCommandRunner invalid;
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&invalid, &AiCommandRunner::failed, &invalid, [&failures](const Error& error) { failures.append(error); });
    // clang-format on
    invalid.start(QStringLiteral("   "), workdir.path(), 30);
    ASSERT_EQ(failures.size(), 1);
    EXPECT_EQ(failures.first().code, QStringLiteral("ai_command_invalid"));

    AiCommandRunner missingDirectory;
    QVector<Error> directoryFailures;
    // clang-format off
    QObject::connect(&missingDirectory, &AiCommandRunner::failed, &missingDirectory, [&directoryFailures](const Error& error) { directoryFailures.append(error); });
    // clang-format on
    missingDirectory.start(QStringLiteral("echo hi"), QDir(workdir.path()).filePath(QStringLiteral("absent")), 30);
    ASSERT_EQ(directoryFailures.size(), 1);
    EXPECT_EQ(directoryFailures.first().code, QStringLiteral("ai_command_workdir_invalid"));
}

TEST(AiCommandRunnerTest, StopsACommandThatExceedsItsTimeLimit) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    AiCommandRunner runner;
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&runner, &AiCommandRunner::failed, &runner, [&failures](const Error& error) { failures.append(error); });
    // clang-format on
    runner.start(test::TestProcesses::sleepingCommand(5), workdir.path(), 1);

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !failures.isEmpty(); }, 8000));
    // clang-format on
    EXPECT_EQ(failures.first().code, QStringLiteral("ai_command_timeout"));
    EXPECT_FALSE(runner.running());
    EXPECT_EQ(failures.size(), 1);
}

// A card never shows the diagnostic of a failure, so every condition the reader reaches carries a sentence of the catalog.
TEST(AiCommandRunnerTest, CarriesATranslatedSentenceForEveryConditionTheReaderReaches) {
    // clang-format off
    const auto translate = [](const QString& key) { return QStringLiteral("translated:") + key; };
    // clang-format on

    const QVector<QPair<QString, QString>> reachable{
        {QStringLiteral("ai_command_timeout"), QStringLiteral("translated:ai.error.command-timeout")}, {QStringLiteral("ai_command_output_too_large"), QStringLiteral("translated:ai.error.command-output-too-large")}, {QStringLiteral("ai_command_workdir_invalid"), QStringLiteral("translated:ai.error.command-workdir-invalid")}, {QStringLiteral("ai_command_failed"), QStringLiteral("translated:ai.error.command-start-failed")}, {QStringLiteral("ai_command_crashed"), QStringLiteral("translated:ai.error.command-crashed")},
    };

    for (const auto& condition : reachable) {
        EXPECT_EQ(CommandOutput::commandFailureMessage({condition.first.toUtf8().constData(), QStringLiteral("a diagnostic nobody translates"), {}}, translate), condition.second) << condition.first.toStdString();
    }

    // A guard the interface cannot reach keeps its diagnostic, which is where it belongs.
    EXPECT_EQ(CommandOutput::commandFailureMessage({"ai_command_busy", QStringLiteral("The runner is already running a command"), {}}, translate), QStringLiteral("The runner is already running a command"));
}

// A command whose working directory is gone reports that condition rather than a message written for the log.
TEST(AiCommandRunnerTest, ReportsAnUnavailableWorkingDirectoryAsTheConditionItIs) {
    AiCommandRunner runner;
    QVector<Error> failures;
    // clang-format off
    QObject::connect(&runner, &AiCommandRunner::failed, &runner, [&failures](const Error& error) { failures.append(error); });
    const auto translate = [](const QString& key) { return QStringLiteral("translated:") + key; };
    // clang-format on
    runner.start(QStringLiteral("echo hello"), QDir::tempPath() + QStringLiteral("/a-directory-that-is-not-there-42"), 5);

    ASSERT_EQ(failures.size(), 1);
    EXPECT_EQ(failures.first().code, QStringLiteral("ai_command_workdir_invalid"));
    EXPECT_EQ(CommandOutput::commandFailureMessage(failures.first(), translate), QStringLiteral("translated:ai.error.command-workdir-invalid"));
    EXPECT_FALSE(runner.running());
}

TEST(AiCommandRunnerTest, ReleasesARunningCommandWithoutBlockingTheInterface) {
    QTemporaryDir workdir;
    ASSERT_TRUE(workdir.isValid());

    QElapsedTimer elapsed;
    {
        AiCommandRunner runner;
        runner.start(test::TestProcesses::sleepingCommand(5), workdir.path(), 0);
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return runner.running(); }));
        // clang-format on
        runner.cancel();
        EXPECT_FALSE(runner.running());
        elapsed.start();
    }

    // A command that ignores the graceful request must never hold the interactive thread while its runner is released.
    EXPECT_LT(elapsed.elapsed(), 2000);

    QElapsedTimer abandoned;
    {
        AiCommandRunner runner;
        runner.start(test::TestProcesses::sleepingCommand(5), workdir.path(), 0);
        // clang-format off
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return runner.running(); }));
        // clang-format on
        abandoned.start();
    }
    EXPECT_LT(abandoned.elapsed(), 2000);
}

TEST(AiPluginTest, RunsTheAgentUntilItStopsAskingForToolsAndStopsAtTheIterationLimit) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiAgent limitedAgent = AiTestsHelper::testAgent();
    limitedAgent.maximumIterations = 3;
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

    // A tool call feeds the result back and starts another iteration instead of finishing the run.
    client->deliverToolCalls({{QStringLiteral("call_1"), QStringLiteral("list_files"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/tmp")}}}});
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return client->sendCalls == 2; }));
    // clang-format on
    EXPECT_EQ(plugin.runState(task.id), TaskRunState::Running);
    EXPECT_GE(client->sentMessages.size(), 3);

    client->deliver(QStringLiteral("done"), {5, 6}, QStringLiteral("stop"));
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);
    plugin.shutdown();
}

TEST(AiPluginTest, EndsTheAgentAtItsIterationLimitWithTheReasonRatherThanWithAFailure) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const AiWorkspace workspace{QStringLiteral("workspace-1"), QStringLiteral("Product"), 0, true, now, now};
    AiTask task = AiTestsHelper::makeTask(QStringLiteral("task-1"), workspace.id);
    AiAgent limitedAgent = AiTestsHelper::testAgent();
    limitedAgent.maximumIterations = 1;
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

    // The turn that has no turn after it is told so, so the model answers instead of calling another tool.
    const QString firstRequest = QString::fromUtf8(QJsonDocument(client->sentMessages).toJson(QJsonDocument::Compact));
    EXPECT_TRUE(firstRequest.contains(QStringLiteral("last turn"))) << firstRequest.toStdString();

    client->deliverToolCalls({{QStringLiteral("call_1"), QStringLiteral("list_files"), QJsonObject{}}});
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return plugin.runState(task.id) == TaskRunState::Idle; }));
    // clang-format on
    EXPECT_EQ(client->sendCalls, 1);
    EXPECT_EQ(plugin.lastExecutionStatus(task.id), ExecutionStatus::Succeeded);
    EXPECT_EQ(plugin.lastStopReason(task.id), AgentStopReason::IterationLimit);
    EXPECT_TRUE(plugin.lastError(task.id).isEmpty());
    plugin.shutdown();
}

TEST(AiChatClientTest, WithdrawsFromTheQueueWhenTheRunIsStoppedBeforeItsTurnCame) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*openai, model), {}};
    const ChatRequest request{connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}};
    // clang-format off
    const auto translate = [](const QString& key) { return key; };
    // clang-format on

    // One request at a time, so the second one waits for its turn.
    AiRequestGate gate;
    gate.setLimits({{openai->id, 0, 0, 1}});
    AiHttpChatClient holding(gate);
    AiHttpChatClient waiting(gate);
    holding.send(request, translate);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return gate.inFlight(openai->id) == 1; }));
    // clang-format on
    waiting.send(request, translate);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return gate.waiting(openai->id) == 1; }));
    // clang-format on

    // The run is stopped before its turn came, so the place it was holding in the queue is given back.
    waiting.cancel();
    EXPECT_EQ(gate.waiting(openai->id), 0);

    // The place given back is never admitted, so the stopped run is not dispatched when the queue moves.
    holding.cancel();

    for (int turn = 0; turn < 30; ++turn) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    }

    EXPECT_EQ(gate.waiting(openai->id), 0);
    EXPECT_EQ(gate.inFlight(openai->id), 0);
    EXPECT_FALSE(waiting.running());
}

// The size of a page is decided by whoever serves it, and the model chooses which one to fetch.
TEST(AiToolRegistryTest, StopsFetchingAPageLargerThanTheBoundInsteadOfHoldingItWhole) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    AiToolRegistry registry(host);

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString sandbox = QDir(root.path()).canonicalPath();

    QTcpServer generous;
    ASSERT_TRUE(generous.listen(QHostAddress::LocalHost, 0));
    const QByteArray padding(256 * 1024, 'a');
    constexpr int offeredChunks = 16;
    int sentChunks = 0;
    // clang-format off
    QObject::connect(&generous, &QTcpServer::newConnection, &generous, [&generous, &sentChunks, &padding]() {
        QTcpSocket* socket = generous.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &sentChunks, &padding]() {
            socket->readAll();
            socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"));
            socket->write(padding);
            ++sentChunks;
        });
        QObject::connect(socket, &QTcpSocket::bytesWritten, socket, [socket, &sentChunks, &padding]() {
            if (sentChunks >= offeredChunks || socket->state() != QAbstractSocket::ConnectedState) {
                return;
            }
            socket->write(padding);
            ++sentChunks;
        });
    });
    // clang-format on

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on
    const QString address = QStringLiteral("http://127.0.0.1:%1/big").arg(generous.serverPort());

    registry.invoke({QStringLiteral("f1"), QStringLiteral("fetch_url"), QJsonObject{{QStringLiteral("url"), address}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&results]() { return results.size() == 1; }, 20000));
    // clang-format on

    // The page was answered from what the bound allowed.
    EXPECT_FALSE(results.first().failed) << results.first().text.toStdString();
    EXPECT_FALSE(results.first().text.isEmpty());

    // The transfer stopped where the bound is rather than running to the end of what the server was willing to send.
    EXPECT_LT(sentChunks, offeredChunks) << "the server sent everything it offered";
}

// Fitting runs on the thread that draws before every turn, so measuring a message once is what keeps a long conversation from freezing it.
TEST(AiToolContractTest, FitsALongConversationWithoutMeasuringItAgainForEveryTurnItDrops) {
    QJsonArray messages;
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QStringLiteral("instructions")}});
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("the task")}});

    const QString body(2000, QLatin1Char('x'));

    for (int index = 0; index < 400; ++index) {
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), body}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), body}});
    }

    QElapsedTimer clock;
    clock.start();
    const FittedConversation fitted = ToolContracts::fitConversation(messages, 2000);
    const qint64 elapsed = clock.elapsed();

    // The instructions and the task are kept whatever had to go, and the newest turns fill what is left.
    ASSERT_GE(fitted.messages.size(), 2);
    EXPECT_EQ(fitted.preservedHead, 2);
    EXPECT_EQ(fitted.messages.at(0).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("instructions"));
    EXPECT_EQ(fitted.messages.at(1).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("the task"));
    EXPECT_EQ(fitted.messages.size() + fitted.dropped.size(), messages.size());

    // Measuring the whole conversation again for every turn it drops costs nearly a second here, which is a second of frozen interface.
    EXPECT_LT(elapsed, 250) << "fitting " << messages.size() << " messages took " << elapsed << "ms";
}

// A tool that reads bytes it cannot decode would answer the model with what the decoding lost, and an edit would write that loss back to the file.
TEST(AiToolRegistryTest, RefusesAFileThatIsNotTextInsteadOfRewritingWhatItHolds) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    AiToolRegistry registry(host);

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString sandbox = QDir(root.path()).canonicalPath();
    const QString name = QStringLiteral("legacy.php");
    const QByteArray content = QByteArrayLiteral("<?php $mes = 'mar\xE7o'; $ano = 2026;\n");
    QFile source(QDir(sandbox).filePath(name));
    ASSERT_TRUE(source.open(QIODevice::WriteOnly));
    ASSERT_EQ(source.write(content), content.size());
    source.close();

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    registry.invoke({QStringLiteral("r1"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), name}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&results]() { return results.size() == 1; }));
    // clang-format on
    EXPECT_TRUE(results.first().failed);
    EXPECT_TRUE(results.first().text.contains(name)) << results.first().text.toStdString();

    registry.invoke({QStringLiteral("e1"), QStringLiteral("edit_file"), QJsonObject{{QStringLiteral("path"), name}, {QStringLiteral("old_text"), QStringLiteral("2026")}, {QStringLiteral("new_text"), QStringLiteral("2027")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&results]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_TRUE(results.at(1).failed);
    EXPECT_TRUE(results.at(1).text.contains(name)) << results.at(1).text.toStdString();

    // The refused edit left every byte of the file where it was.
    QFile written(QDir(sandbox).filePath(name));
    ASSERT_TRUE(written.open(QIODevice::ReadOnly));
    EXPECT_EQ(written.readAll(), content);
}

TEST(AiToolRegistryTest, DeclaresValidSchemasAndKeepsEveryPathInsideTheWorkingDirectory) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    AiToolRegistry registry(host);

    ASSERT_FALSE(registry.schemas().isEmpty());

    for (const auto& schema : registry.schemas()) {
        EXPECT_TRUE(ToolContracts::validateToolSchema(schema).hasValue()) << schema.name.toStdString();
    }

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString sandbox = QDir(root.path()).canonicalPath();

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    // A traversal, an absolute path and a task without a working directory are all refused.
    registry.invoke({QStringLiteral("c1"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("../escape.txt")}}}, sandbox, collect);
    registry.invoke({QStringLiteral("c2"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("/etc/hosts")}}}, sandbox, collect);
    registry.invoke({QStringLiteral("c3"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("file.txt")}}}, QString{}, collect);
    registry.invoke({QStringLiteral("c4"), QStringLiteral("nonexistent_tool"), QJsonObject{}}, sandbox, collect);
    ASSERT_EQ(results.size(), 4);

    for (const auto& result : results) {
        EXPECT_TRUE(result.failed) << result.text.toStdString();
    }
}

TEST(AiToolRegistryTest, WritesReadsAndListsInsideTheWorkingDirectory) {
    test::TestPluginHost host;
    host.translations = translations::AiCatalog::english();
    filesystem::FileSystemService files;
    host.useFileSystem(files);
    AiToolRegistry registry(host);

    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString sandbox = QDir(root.path()).canonicalPath();

    QVector<ToolResult> results;
    // clang-format off
    const auto collect = [&results](ToolResult result) { results.append(std::move(result)); };
    // clang-format on

    registry.invoke({QStringLiteral("w1"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}, {QStringLiteral("content"), QStringLiteral("workpane")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 1; }));
    // clang-format on
    ASSERT_FALSE(results.first().failed) << results.first().text.toStdString();

    registry.invoke({QStringLiteral("r1"), QStringLiteral("read_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("notes.txt")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 2; }));
    // clang-format on
    EXPECT_FALSE(results.at(1).failed);
    EXPECT_EQ(results.at(1).text, QStringLiteral("workpane"));

    registry.invoke({QStringLiteral("l1"), QStringLiteral("list_directory"), QJsonObject{{QStringLiteral("path"), QStringLiteral("")}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 3; }));
    // clang-format on
    EXPECT_FALSE(results.at(2).failed);
    EXPECT_TRUE(results.at(2).text.contains(QStringLiteral("notes.txt")));

    registry.invoke({QStringLiteral("w2"), QStringLiteral("write_file"), QJsonObject{{QStringLiteral("path"), QStringLiteral("bad.txt")}, {QStringLiteral("content"), 42}}}, sandbox, collect);
    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return results.size() == 4; }));
    // clang-format on
    EXPECT_TRUE(results.at(3).failed);
}

TEST(AiChatClientTest, RetriesATransientFailureAndGivesUpOnARejection) {
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
        const QByteArray body = QByteArrayLiteral("{\"error\":{\"message\":\"overloaded\"}}");
        socket->write(QByteArrayLiteral("HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
        socket->disconnectFromHost();
        });
    });
    // clang-format on

    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*openai, model), {}};

    AiRequestGate clientGate;
    AiHttpChatClient client(clientGate);
    QVector<Error> failures;
    QList<qint64> waits;
    // clang-format off
    QObject::connect(&client, &AiChatClient::failed, &client, [&failures](const Error& error) { failures.append(error); });
    QObject::connect(&client, &AiChatClient::throttled, &client, [&waits](ThrottleReason reason, qint64 milliseconds) { if (reason == ThrottleReason::Retry) { waits.append(milliseconds); } });
    // clang-format on
    // clang-format off
    client.send({connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
    // clang-format on

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return !failures.isEmpty(); }, 10000));
    // clang-format on

    // A server side condition is retried up to the provider limit and then reported once.
    EXPECT_EQ(requests, openai->requestMaxRetries + 1);
    EXPECT_EQ(failures.size(), 1);
    EXPECT_TRUE(failures.first().message.contains(QStringLiteral("overloaded")));

    // Every attempt after the first one waited, because a rejection repeated immediately reproduces what caused it.
    ASSERT_EQ(waits.size(), openai->requestMaxRetries);
    EXPECT_GE(waits.first(), ProviderCatalog::aiLimits().retryBackoffMs);
    EXPECT_GT(waits.last(), waits.first());
}

TEST(AiChatClientTest, PacesTwoRequestsOfOneProviderThroughTheSharedGate) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    QList<qint64> arrivals;
    QElapsedTimer clock;
    clock.start();
    // clang-format off
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &arrivals, &clock]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &arrivals, &clock]() {
        if (!socket->readAll().contains(QByteArrayLiteral("\r\n\r\n"))) {
            return;
        }
        arrivals.append(clock.elapsed());
        const QByteArray body = QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
        socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: ") + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
        socket->disconnectFromHost();
        });
    });
    // clang-format on

    const ProviderDescriptor* openai = ProviderCatalog::findProvider(QStringLiteral("openai"));
    ASSERT_NE(openai, nullptr);
    const QString model = QStringLiteral("gpt-4o");
    const ModelConnection connection{openai->id, model, {}, QStringLiteral("sk"), {}, ProviderCatalog::defaultParameters(*openai, model), {}};
    const ChatRequest request{connection, QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}};
    // clang-format off
    const auto translate = [](const QString& key) { return key; };
    // clang-format on

    // The tasks of every workspace reach one service through the same queue, so the second request waits for the declared delay.
    AiRequestGate gate;
    gate.setLimits({{openai->id, 400, 0, 0}});
    AiHttpChatClient first(gate);
    AiHttpChatClient second(gate);
    int finished = 0;
    QList<qint64> waits;
    // clang-format off
    QObject::connect(&first, &AiChatClient::finished, &first, [&finished](const QString&, const QVector<ToolCall>&, ChatUsage, const QString&) { ++finished; });
    QObject::connect(&second, &AiChatClient::finished, &second, [&finished](const QString&, const QVector<ToolCall>&, ChatUsage, const QString&) { ++finished; });
    QObject::connect(&second, &AiChatClient::throttled, &second, [&waits](ThrottleReason reason, qint64 milliseconds) { if (reason == ThrottleReason::RateLimit) { waits.append(milliseconds); } });
    // clang-format on
    first.send(request, translate);
    second.send(request, translate);

    // clang-format off
    ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return finished == 2; }, 10000));
    // clang-format on
    ASSERT_EQ(arrivals.size(), 2);
    EXPECT_GT(arrivals.at(1), arrivals.at(0));

    // The pace is measured where it is applied, because an arrival at the socket also carries the setup of its own connection and the clock that timed it is not the one the gate waits on.
    EXPECT_EQ(waits.size(), 1);
    EXPECT_GE(waits.first(), 250);
    EXPECT_LE(waits.first(), 400);
}

TEST(AiHttpChatClientTest, EndsEveryHostileStreamWithExactlyOneTerminalEvent) {
    const QList<QByteArray> hostile{
        QByteArray{}, QByteArrayLiteral("data:"), QByteArrayLiteral("data: "), QByteArrayLiteral("data: {"), QByteArrayLiteral("data: {}\n\n"), QByteArrayLiteral("data: [DONE]\n\n"), QByteArrayLiteral("data: not json at all\n\n"), QByteArrayLiteral("event: message\ndata: {\"type\":\"unknown\"}\n\n"), QByteArrayLiteral("data: {\"type\":\"content_block_delta\"}\n\n"), QByteArrayLiteral("data: {\"choices\":[]}\n\n"), QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{}]}}]}\n\n"), QByteArray::fromHex("c328c328") + QByteArrayLiteral("\n\n"), QByteArray("data: {\0}\n\n", 11), QByteArrayLiteral("data: ") + QByteArray(200000, 'a') + QByteArrayLiteral("\n\n"), QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"") + QByteArray(100000, 'x') + QByteArrayLiteral("\"}}]}\n\n"),
    };

    for (const auto& stream : hostile) {
        RecordedStreamServer server(stream);
        ASSERT_TRUE(server.listen());

        const ProviderDescriptor* provider = ProviderCatalog::findProvider(QStringLiteral("openai"));
        ASSERT_NE(provider, nullptr);
        const QString model = QStringLiteral("gpt-4o-mini");
        const ModelConnection connection{provider->id, model, {}, QStringLiteral("sk-test"), {}, ProviderCatalog::defaultParameters(*provider, model), {}};

        AiRequestGate gate;
        AiHttpChatClient client(gate);
        int terminalEvents = 0;
        // clang-format off
        QObject::connect(&client, &AiChatClient::finished, &client, [&terminalEvents](const QString&, const QVector<ToolCall>&, ChatUsage, const QString&) { ++terminalEvents; });
        QObject::connect(&client, &AiChatClient::failed, &client, [&terminalEvents](const Error&) { ++terminalEvents; });
        client.send({connection, server.address(), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hello")}}}, {}}, [](const QString& key) { return key; });
        ASSERT_TRUE(test::TestFutures::waitUntil([&]() { return terminalEvents > 0; })) << stream.left(40).toStdString();
        // clang-format on

        // Exactly one terminal event is emitted per request, whatever the service sent.
        QCoreApplication::processEvents();
        EXPECT_EQ(terminalEvents, 1) << stream.left(40).toStdString();
    }
}

} // namespace workpane::plugins::ai
