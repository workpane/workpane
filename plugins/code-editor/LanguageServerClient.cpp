#include "LanguageServerClient.h"

#include "CodeEditorState.h"
#include "LanguageRegistry.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>
#include <utility>

namespace workpane::plugins::codeeditor {

constexpr int transportDrainTimeoutMs = 5000;
constexpr int maximumSymbolDepth = 64;

class LanguageServerClientHelper final {
  public:
    static QMutex& transportGuard();
    static QSet<QThread*>& liveTransports();
    static QJsonObject textDocument(const QString& uri);
    static bool declaredProvider(const QJsonValue& value);
    static QStringList triggerCharacters(const QJsonValue& provider);
    static QString hoverText(const QJsonValue& contents);
    static QString symbolQueryMethod(SymbolQueryKind kind);
    static SourceLocation locationOf(const QJsonObject& entry);
    static SemanticTokenSet decodeSemanticTokens(const QJsonArray& data, const QStringList& types, const QStringList& modifiers);
    static QVector<CompletionProposal> decodeCompletions(const QJsonArray& items, int bound);
    static QVector<SourceLocation> locationsOf(const QJsonValue& result);
    static QVector<DocumentSymbolNode> symbolNodes(const QJsonArray& items, int depth);
    static QVector<LanguageDiagnostic> diagnosticsOf(const QString& path, const QJsonArray& items);
};

QJsonObject LanguageServerClientHelper::textDocument(const QString& uri) {
    return {{QStringLiteral("uri"), uri}};
}

bool LanguageServerClientHelper::declaredProvider(const QJsonValue& value) {
    return value.isObject() || value.toBool(false);
}

QStringList LanguageServerClientHelper::triggerCharacters(const QJsonValue& provider) {
    QStringList characters;

    for (const auto& character : provider.toObject().value(QStringLiteral("triggerCharacters")).toArray()) {
        if (!character.toString().isEmpty()) {
            characters.append(character.toString());
        }
    }

    return characters;
}

QString LanguageServerClientHelper::hoverText(const QJsonValue& contents) {
    if (contents.isString()) {
        return contents.toString();
    }
    if (contents.isObject()) {
        return contents.toObject().value(QStringLiteral("value")).toString();
    }

    QStringList parts;

    for (const auto& entry : contents.toArray()) {
        const QString part = entry.isString() ? entry.toString() : entry.toObject().value(QStringLiteral("value")).toString();
        if (!part.isEmpty()) {
            parts.append(part);
        }
    }

    return parts.join(QStringLiteral("\n"));
}

QString LanguageServerClientHelper::symbolQueryMethod(SymbolQueryKind kind) {
    switch (kind) {
    case SymbolQueryKind::Definition:
        return QStringLiteral("textDocument/definition");
    case SymbolQueryKind::Declaration:
        return QStringLiteral("textDocument/declaration");
    case SymbolQueryKind::TypeDefinition:
        return QStringLiteral("textDocument/typeDefinition");
    case SymbolQueryKind::Implementation:
        return QStringLiteral("textDocument/implementation");
    case SymbolQueryKind::References:
        return QStringLiteral("textDocument/references");
    }

    return {};
}

SourceLocation LanguageServerClientHelper::locationOf(const QJsonObject& entry) {
    const QString targetUri = entry.contains(QStringLiteral("targetUri")) ? entry.value(QStringLiteral("targetUri")).toString() : entry.value(QStringLiteral("uri")).toString();
    const QJsonObject location = entry.value(QStringLiteral("location")).toObject();
    const QString uri = targetUri.isEmpty() ? location.value(QStringLiteral("uri")).toString() : targetUri;
    QJsonObject range = entry.value(QStringLiteral("range")).toObject();

    if (entry.contains(QStringLiteral("targetSelectionRange"))) {
        range = entry.value(QStringLiteral("targetSelectionRange")).toObject();
    } else if (!location.isEmpty()) {
        range = location.value(QStringLiteral("range")).toObject();
    }

    const QJsonObject start = range.value(QStringLiteral("start")).toObject();
    const QJsonObject end = range.value(QStringLiteral("end")).toObject();
    return {QUrl(uri).toLocalFile(), start.value(QStringLiteral("line")).toInt(), start.value(QStringLiteral("character")).toInt(), end.value(QStringLiteral("line")).toInt(), end.value(QStringLiteral("character")).toInt()};
}

QVector<SourceLocation> LanguageServerClientHelper::locationsOf(const QJsonValue& result) {
    QVector<SourceLocation> locations;

    if (result.isObject()) {
        locations.append(locationOf(result.toObject()));
        return locations;
    }

    for (const auto& entry : result.toArray()) {
        const SourceLocation location = locationOf(entry.toObject());
        if (!location.path.isEmpty()) {
            locations.append(location);
        }
    }

    return locations;
}

// A server answers either the nested document symbols or the flat symbol information, and both describe the same outline.
QVector<DocumentSymbolNode> LanguageServerClientHelper::symbolNodes(const QJsonArray& items, int depth) {
    QVector<DocumentSymbolNode> nodes;

    // An outline nests as deep as the server says, so the depth it may reach is ours to declare rather than its.
    if (depth >= maximumSymbolDepth) {
        return nodes;
    }

    for (const auto& entry : items) {
        const QJsonObject item = entry.toObject();
        const QString name = item.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }
        const SourceLocation location = locationOf(item.contains(QStringLiteral("selectionRange")) ? QJsonObject{{QStringLiteral("range"), item.value(QStringLiteral("selectionRange"))}} : item);
        nodes.append({name, item.value(QStringLiteral("detail")).toString(), item.value(QStringLiteral("kind")).toInt(), location.line, location.character, symbolNodes(item.value(QStringLiteral("children")).toArray(), depth + 1)});
    }

    return nodes;
}

QVector<LanguageDiagnostic> LanguageServerClientHelper::diagnosticsOf(const QString& path, const QJsonArray& items) {
    QVector<LanguageDiagnostic> diagnostics;

    for (const auto& value : items) {
        const QJsonObject object = value.toObject();
        const QJsonObject range = object.value(QStringLiteral("range")).toObject();
        const QJsonObject start = range.value(QStringLiteral("start")).toObject();
        const QJsonObject end = range.value(QStringLiteral("end")).toObject();
        const QJsonValue code = object.value(QStringLiteral("code"));
        LanguageDiagnostic diagnostic{path, start.value(QStringLiteral("line")).toInt(), start.value(QStringLiteral("character")).toInt(), end.value(QStringLiteral("line")).toInt(), end.value(QStringLiteral("character")).toInt(), object.value(QStringLiteral("severity")).toInt(1), object.value(QStringLiteral("message")).toString(), code.isDouble() ? QString::number(code.toInt()) : code.toString(), object.value(QStringLiteral("source")).toString(), false, false, {}};

        for (const auto& tag : object.value(QStringLiteral("tags")).toArray()) {
            diagnostic.unnecessary = diagnostic.unnecessary || tag.toInt() == 1;
            diagnostic.deprecated = diagnostic.deprecated || tag.toInt() == 2;
        }

        for (const auto& entry : object.value(QStringLiteral("relatedInformation")).toArray()) {
            const QJsonObject related = entry.toObject();
            const SourceLocation location = LanguageServerClientHelper::locationOf(related.value(QStringLiteral("location")).toObject());
            if (!location.path.isEmpty()) {
                diagnostic.related.append({location, related.value(QStringLiteral("message")).toString()});
            }
        }

        diagnostics.append(diagnostic);
    }

    return diagnostics;
}

// Every transport thread is registered while it runs, because the process must not end while one of them is still inside the code it was given.
QMutex& LanguageServerClientHelper::transportGuard() {
    static QMutex guard;
    return guard;
}

QSet<QThread*>& LanguageServerClientHelper::liveTransports() {
    static QSet<QThread*> threads;
    return threads;
}

void LanguageServerClient::drainTransports() {
    QSet<QThread*> pending;
    {
        const QMutexLocker locked(&LanguageServerClientHelper::transportGuard());
        pending = LanguageServerClientHelper::liveTransports();
    }

    for (auto* thread : pending) {
        thread->quit();
        thread->wait(transportDrainTimeoutMs);
    }
}

LanguageServerClient::LanguageServerClient(ResolvedLanguageServer server, QString rootPath, QObject* parent) : QObject(parent), m_server(std::move(server)), m_rootPath(std::move(rootPath)), m_transportThread(new QThread), m_transport(new LanguageServerTransport(m_server, m_rootPath)) {
    m_stopTimer.setSingleShot(true);
    m_stopTimer.setInterval(2000);
    m_initializeTimer.setSingleShot(true);
    m_initializeTimer.setInterval(LanguageRegistry::limits().initializeTimeoutMs);

    m_transport->moveToThread(m_transportThread);
    {
        const QMutexLocker locked(&LanguageServerClientHelper::transportGuard());
        LanguageServerClientHelper::liveTransports().insert(m_transportThread);
    }
    // clang-format off
    connect(m_transportThread, &QThread::finished, m_transportThread, [thread = m_transportThread]() { const QMutexLocker locked(&LanguageServerClientHelper::transportGuard()); LanguageServerClientHelper::liveTransports().remove(thread); });
    connect(m_transportThread, &QThread::finished, m_transport, &QObject::deleteLater);
    connect(m_transportThread, &QThread::finished, m_transportThread, &QObject::deleteLater);
    connect(m_transport, &LanguageServerTransport::started, this, [this]() { m_running = true; processStarted(); });
    connect(m_transport, &LanguageServerTransport::messageReceived, this, [this](const QJsonObject& message) { handleMessage(message); });
    connect(m_transport, &LanguageServerTransport::diagnosticText, this, [this](const QString& text) { if (!m_stopping) { emit serverLog(text); } });
    connect(m_transport, &LanguageServerTransport::startFailed, this, [this](const QString& message) { if (!m_stopping) { emit serverError(message); } });
    connect(m_transport, &LanguageServerTransport::protocolFailed, this, [this](const QString& message) { emit serverError(message); m_stopping = true; });
    connect(m_transport, &LanguageServerTransport::exited, this, [this](int exitCode) { m_running = false; processFinished(exitCode); });
    connect(&m_stopTimer, &QTimer::timeout, this, [this]() { callTransport("kill"); });
    connect(&m_initializeTimer, &QTimer::timeout, this, [this]() { emit serverLog(QStringLiteral("The language server did not answer initialization")); callTransport("kill"); });
    // clang-format on
    m_transportThread->start();
}

// The thread is asked to end and releases itself when it does, because the interface never waits for a child process to exit.
// Everything this object receives is disconnected by its own destruction, so the transport of another thread is never touched from here.
LanguageServerClient::~LanguageServerClient() {
    callTransport("shutdown");
}

// The transport is gone once its thread has ended, so nothing it is asked to do reaches an object that is no longer there.
void LanguageServerClient::callTransport(const char* method) {
    if (m_transport != nullptr) {
        QMetaObject::invokeMethod(m_transport, method, Qt::QueuedConnection);
    }
}

void LanguageServerClient::start() {
    if (!m_running && !m_stopping) {
        callTransport("start");
    }
}

void LanguageServerClient::openDocument(const QString& path, const QString& text, const QString& languageId) {
    Document& document = m_documents[path];
    document.text = text;
    document.languageId = languageId;

    if (m_ready && !document.opened) {
        sendOpen(path, document);
    }
}

// A reload replaces everything the server holds, and a change without a range is the full content in both synchronization modes.
void LanguageServerClient::replaceDocument(const QString& path, const QString& text) {
    auto document = m_documents.find(path);

    if (document == m_documents.end()) {
        return;
    }
    if (document->text == text) {
        return;
    }

    document->text = text;
    sendChange(path, *document, QJsonArray{QJsonObject{{QStringLiteral("text"), text}}});
}

// An edit that does not fit the copy the server has means the two drifted apart, and only a complete replacement can bring them back together.
bool LanguageServerClient::editDocument(const QString& path, const QVector<DocumentEdit>& edits) {
    auto document = m_documents.find(path);

    if (document == m_documents.end()) {
        return false;
    }
    if (edits.isEmpty()) {
        return true;
    }

    QJsonArray contentChanges;

    for (const auto& edit : edits) {
        if (edit.offset < 0 || edit.removedLength < 0 || edit.offset + edit.removedLength > document->text.size()) {
            return false;
        }
        if (m_capabilities.sync == DocumentSyncKind::Incremental) {
            const QJsonObject start = positionOf(document->text, edit.offset);
            const QJsonObject end = positionAfter(start, QStringView(document->text).sliced(edit.offset, edit.removedLength));
            contentChanges.append(QJsonObject{{QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), start}, {QStringLiteral("end"), end}}}, {QStringLiteral("rangeLength"), edit.removedLength}, {QStringLiteral("text"), edit.addedText}});
        }
        document->text.replace(edit.offset, edit.removedLength, edit.addedText);
    }

    if (m_capabilities.sync == DocumentSyncKind::Full) {
        contentChanges.append(QJsonObject{{QStringLiteral("text"), document->text}});
    }

    sendChange(path, *document, contentChanges);
    return true;
}

void LanguageServerClient::saveDocument(const QString& path, const QString& text) {
    auto document = m_documents.find(path);

    if (document == m_documents.end()) {
        return;
    }

    document->text = text;

    if (!m_ready || !document->opened || !m_capabilities.save) {
        return;
    }

    QJsonObject parameters{{QStringLiteral("textDocument"), LanguageServerClientHelper::textDocument(uri(path))}};

    if (m_capabilities.saveIncludesText) {
        parameters.insert(QStringLiteral("text"), text);
    }

    sendNotification(QStringLiteral("textDocument/didSave"), parameters);
}

void LanguageServerClient::closeDocument(const QString& path) {
    auto document = m_documents.find(path);

    if (document == m_documents.end()) {
        return;
    }

    if (m_ready && document->opened && m_capabilities.openClose) {
        sendNotification(QStringLiteral("textDocument/didClose"), {{QStringLiteral("textDocument"), LanguageServerClientHelper::textDocument(uri(path))}});
    }

    m_documents.erase(document);
}

// A superseded position request is cancelled at the server, so its answer never reaches a cursor that has already moved.
void LanguageServerClient::requestCompletion(const QString& path, int line, int character) {
    if (!m_ready || !m_capabilities.completion || !m_documents.contains(path)) {
        return;
    }

    trackedRequest(QStringLiteral("textDocument/completion"), documentPosition(path, line, character), m_completionRequests, path);
}

// The documentation of a proposal often arrives only when it is asked for, so the highlighted row resolves itself.
void LanguageServerClient::requestCompletionDocumentation(const QString& path, int row, const QJsonObject& item) {
    if (!m_ready || !m_capabilities.completionResolve || item.isEmpty()) {
        return;
    }

    for (auto request = m_completionResolveRequests.begin(); request != m_completionResolveRequests.end();) {
        if (request.value().first != path) {
            ++request;
            continue;
        }
        cancelRequest(request.key());
        request = m_completionResolveRequests.erase(request);
    }

    m_completionResolveRequests.insert(sendRequest(QStringLiteral("completionItem/resolve"), item), {path, row});
}

void LanguageServerClient::requestSymbolQuery(const QString& path, int line, int character, SymbolQueryKind kind) {
    if (!m_ready || !supports(kind) || !m_documents.contains(path)) {
        return;
    }

    QJsonObject parameters = documentPosition(path, line, character);

    if (kind == SymbolQueryKind::References) {
        parameters.insert(QStringLiteral("context"), QJsonObject{{QStringLiteral("includeDeclaration"), true}});
    }

    m_symbolQueryRequests.insert(sendRequest(LanguageServerClientHelper::symbolQueryMethod(kind), parameters), {path, kind});
}

void LanguageServerClient::requestHover(const QString& path, int line, int character) {
    if (!m_ready || !m_capabilities.hover || !m_documents.contains(path)) {
        return;
    }

    trackedRequest(QStringLiteral("textDocument/hover"), documentPosition(path, line, character), m_hoverRequests, path);
}

void LanguageServerClient::requestSignatureHelp(const QString& path, int line, int character) {
    if (!m_ready || !m_capabilities.signatureHelp || !m_documents.contains(path)) {
        return;
    }

    trackedRequest(QStringLiteral("textDocument/signatureHelp"), documentPosition(path, line, character), m_signatureHelpRequests, path);
}

void LanguageServerClient::requestDocumentHighlights(const QString& path, int line, int character) {
    if (!m_ready || !m_capabilities.documentHighlights || !m_documents.contains(path)) {
        return;
    }

    trackedRequest(QStringLiteral("textDocument/documentHighlight"), documentPosition(path, line, character), m_highlightRequests, path);
}

void LanguageServerClient::requestDocumentSymbols(const QString& path) {
    if (!m_ready || !m_capabilities.documentSymbols || !m_documents.contains(path)) {
        return;
    }

    trackedRequest(QStringLiteral("textDocument/documentSymbol"), {{QStringLiteral("textDocument"), LanguageServerClientHelper::textDocument(uri(path))}}, m_documentSymbolRequests, path);
}

void LanguageServerClient::requestWorkspaceSymbols(const QString& query) {
    if (!m_ready || !m_capabilities.workspaceSymbols) {
        return;
    }

    for (const int requestId : m_workspaceSymbolRequests) {
        cancelRequest(requestId);
    }

    m_workspaceSymbolRequests.clear();
    m_workspaceSymbolRequests.insert(sendRequest(QStringLiteral("workspace/symbol"), {{QStringLiteral("query"), query}}));
}

void LanguageServerClient::requestSemanticTokens(const QString& path) {
    if (!m_ready || !m_capabilities.semanticTokens || !m_documents.contains(path)) {
        return;
    }

    trackedRequest(QStringLiteral("textDocument/semanticTokens/full"), {{QStringLiteral("textDocument"), LanguageServerClientHelper::textDocument(uri(path))}}, m_semanticTokenRequests, path);
}

// A server that publishes diagnostics only on request is asked for them, because nothing else will ever deliver them.
void LanguageServerClient::requestDiagnostics(const QString& path) {
    if (!m_ready || !m_capabilities.pullDiagnostics || !m_documents.contains(path)) {
        return;
    }

    trackedRequest(QStringLiteral("textDocument/diagnostic"), {{QStringLiteral("textDocument"), LanguageServerClientHelper::textDocument(uri(path))}}, m_diagnosticRequests, path);
}

// A file changed outside the editor still changes the analysis, so the server hears about it even when no document is open on it.
void LanguageServerClient::notifyWatchedFilesChanged(const QStringList& paths, int changeType) {
    if (!m_ready || paths.isEmpty()) {
        return;
    }

    QJsonArray changes;

    for (const auto& path : paths) {
        changes.append(QJsonObject{{QStringLiteral("uri"), uri(path)}, {QStringLiteral("type"), changeType}});
    }

    sendNotification(QStringLiteral("workspace/didChangeWatchedFiles"), {{QStringLiteral("changes"), changes}});
}

bool LanguageServerClient::ready() const {
    return m_ready;
}

const ResolvedLanguageServer& LanguageServerClient::configuration() const {
    return m_server;
}

const QStringList& LanguageServerClient::completionTriggerCharacters() const {
    return m_capabilities.completionTriggerCharacters;
}

bool LanguageServerClient::supports(SymbolQueryKind kind) const {
    switch (kind) {
    case SymbolQueryKind::Definition:
        return m_capabilities.definition;
    case SymbolQueryKind::Declaration:
        return m_capabilities.declaration;
    case SymbolQueryKind::TypeDefinition:
        return m_capabilities.typeDefinition;
    case SymbolQueryKind::Implementation:
        return m_capabilities.implementation;
    case SymbolQueryKind::References:
        return m_capabilities.references;
    }

    return false;
}

const QStringList& LanguageServerClient::signatureHelpTriggerCharacters() const {
    return m_capabilities.signatureHelpTriggerCharacters;
}

void LanguageServerClient::stop() {
    if (m_stopping) {
        return;
    }

    m_stopping = true;
    m_initializeTimer.stop();

    if (!m_running) {
        emit stopped();
        return;
    }

    m_stopTimer.start();

    if (!m_ready) {
        callTransport("requestTermination");
        return;
    }

    m_shutdownRequestId = sendRequest(QStringLiteral("shutdown"), {});
}

void LanguageServerClient::processStarted() {
    const QJsonObject completion{{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("contextSupport"), true}, {QStringLiteral("completionItem"), QJsonObject{{QStringLiteral("snippetSupport"), false}, {QStringLiteral("documentationFormat"), QJsonArray{QStringLiteral("plaintext")}}}}};
    const QJsonObject synchronization{{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("didSave"), true}};
    const QJsonObject hover{{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("contentFormat"), QJsonArray{QStringLiteral("plaintext")}}};
    const QJsonObject definition{{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("linkSupport"), true}};
    const QJsonObject signatureHelp{{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("signatureInformation"), QJsonObject{{QStringLiteral("documentationFormat"), QJsonArray{QStringLiteral("plaintext")}}, {QStringLiteral("activeParameterSupport"), true}}}};
    const QJsonObject semanticTokens{{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("requests"), QJsonObject{{QStringLiteral("full"), true}}}, {QStringLiteral("tokenTypes"), QJsonArray{}}, {QStringLiteral("tokenModifiers"), QJsonArray{}}, {QStringLiteral("formats"), QJsonArray{QStringLiteral("relative")}}};
    const QJsonObject symbols{{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("hierarchicalDocumentSymbolSupport"), true}};
    const QJsonObject document{{QStringLiteral("synchronization"), synchronization}, {QStringLiteral("completion"), completion}, {QStringLiteral("hover"), hover}, {QStringLiteral("definition"), definition}, {QStringLiteral("declaration"), definition}, {QStringLiteral("typeDefinition"), definition}, {QStringLiteral("implementation"), definition}, {QStringLiteral("references"), QJsonObject{{QStringLiteral("dynamicRegistration"), false}}}, {QStringLiteral("documentHighlight"), QJsonObject{{QStringLiteral("dynamicRegistration"), false}}}, {QStringLiteral("documentSymbol"), symbols}, {QStringLiteral("signatureHelp"), signatureHelp}, {QStringLiteral("semanticTokens"), semanticTokens}, {QStringLiteral("diagnostic"), QJsonObject{{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("relatedDocumentSupport"), false}}}, {QStringLiteral("publishDiagnostics"), QJsonObject{{QStringLiteral("relatedInformation"), false}}}};
    const QJsonObject workspace{{QStringLiteral("workspaceFolders"), true}, {QStringLiteral("configuration"), true}, {QStringLiteral("symbol"), QJsonObject{{QStringLiteral("dynamicRegistration"), false}}}, {QStringLiteral("didChangeWatchedFiles"), QJsonObject{{QStringLiteral("dynamicRegistration"), false}}}};
    const QJsonObject window{{QStringLiteral("workDoneProgress"), true}};
    const QJsonObject capabilities{{QStringLiteral("textDocument"), document}, {QStringLiteral("workspace"), workspace}, {QStringLiteral("window"), window}, {QStringLiteral("general"), QJsonObject{{QStringLiteral("positionEncodings"), QJsonArray{QStringLiteral("utf-16")}}}}};
    m_initializeTimer.start();
    m_initializeRequestId = sendRequest(QStringLiteral("initialize"), {{QStringLiteral("processId"), QCoreApplication::applicationPid()}, {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Workpane")}}}, {QStringLiteral("rootUri"), uri(m_rootPath)}, {QStringLiteral("workspaceFolders"), QJsonArray{QJsonObject{{QStringLiteral("uri"), uri(m_rootPath)}, {QStringLiteral("name"), QFileInfo(m_rootPath).fileName()}}}}, {QStringLiteral("capabilities"), capabilities}});
}

// A server that dies while the workspace is open is started again inside a bounded budget, because one crash must not end the integration for the session.
void LanguageServerClient::processFinished(int exitCode) {
    m_stopTimer.stop();
    m_initializeTimer.stop();
    m_ready = false;
    m_completionRequests.clear();
    m_completionResolveRequests.clear();
    m_symbolQueryRequests.clear();
    m_hoverRequests.clear();
    m_signatureHelpRequests.clear();
    m_highlightRequests.clear();
    m_documentSymbolRequests.clear();
    m_semanticTokenRequests.clear();
    m_diagnosticRequests.clear();
    m_workspaceSymbolRequests.clear();
    m_initializeRequestId = 0;
    m_shutdownRequestId = 0;

    for (auto document = m_documents.begin(); document != m_documents.end(); ++document) {
        document->opened = false;
    }

    if (m_stopping) {
        emit stopped();
        return;
    }

    if (!m_restartWindow.isValid() || m_restartWindow.elapsed() > LanguageRegistry::limits().restartWindowMs) {
        m_restartWindow.start();
        m_restartsUsed = 0;
    }

    if (m_restartsUsed >= LanguageRegistry::limits().maximumRestarts) {
        emit serverError(QStringLiteral("The language server exited with code %1 and reached its restart limit").arg(exitCode));
        emit stopped();
        return;
    }

    ++m_restartsUsed;
    emit serverLog(QStringLiteral("The language server exited with code %1 and is being started again").arg(exitCode));
    start();
}

// A message carrying a method is what the server is asking of us, and only a message without one answers what we asked.
void LanguageServerClient::handleMessage(const QJsonObject& message) {
    const QString method = message.value(QStringLiteral("method")).toString();

    if (!method.isEmpty()) {
        if (message.contains(QStringLiteral("id"))) {
            handleServerRequest(message.value(QStringLiteral("id")), method, message.value(QStringLiteral("params")).toObject());
            return;
        }
        handleServerNotification(method, message.value(QStringLiteral("params")).toObject());
        return;
    }

    // Every request this client sends is numbered from one, so a response identified any other way, or by a number nobody issued, answers none of them.
    const QJsonValue identifier = message.value(QStringLiteral("id"));

    if (!identifier.isDouble() || identifier.toInt() <= 0) {
        return;
    }

    handleResponse(identifier.toInt(), message);
}

// A configuration answer carries one value for every item the server asked about, and we configure nothing.
void LanguageServerClient::handleServerRequest(const QJsonValue& id, const QString& method, const QJsonObject& parameters) {
    if (method == QStringLiteral("workspace/configuration")) {
        QJsonArray configuration;
        for (qsizetype index = 0; index < parameters.value(QStringLiteral("items")).toArray().size(); ++index) {
            configuration.append(QJsonValue::Null);
        }
        sendResponse(id, configuration);
        return;
    }

    if (method == QStringLiteral("client/registerCapability")) {
        for (const auto& value : parameters.value(QStringLiteral("registrations")).toArray()) {
            const QJsonObject registration = value.toObject();
            applyRegistration(registration.value(QStringLiteral("method")).toString(), registration.value(QStringLiteral("registerOptions")).toObject(), true);
        }
        sendResponse(id, QJsonValue::Null);
        emit initialized();
        return;
    }

    // The specification spells this key with the extra syllable, so it is read as the specification writes it.
    if (method == QStringLiteral("client/unregisterCapability")) {
        for (const auto& value : parameters.value(QStringLiteral("unregisterations")).toArray()) {
            applyRegistration(value.toObject().value(QStringLiteral("method")).toString(), {}, false);
        }
        sendResponse(id, QJsonValue::Null);
        return;
    }

    if (method == QStringLiteral("window/workDoneProgress/create")) {
        sendResponse(id, QJsonValue::Null);
        return;
    }

    if (method == QStringLiteral("workspace/workspaceFolders")) {
        sendResponse(id, QJsonArray{QJsonObject{{QStringLiteral("uri"), uri(m_rootPath)}, {QStringLiteral("name"), QFileInfo(m_rootPath).fileName()}}});
        return;
    }

    sendErrorResponse(id, -32601, QStringLiteral("Method not found"));
}

void LanguageServerClient::handleServerNotification(const QString& method, const QJsonObject& parameters) {
    if (method == QStringLiteral("textDocument/publishDiagnostics")) {
        const QString path = documentPathOf(parameters.value(QStringLiteral("uri")).toString());
        emit diagnosticsPublished(path, LanguageServerClientHelper::diagnosticsOf(path, parameters.value(QStringLiteral("diagnostics")).toArray()));
        return;
    }

    if (method == QStringLiteral("$/progress")) {
        const QJsonObject value = parameters.value(QStringLiteral("value")).toObject();
        const QString kind = value.value(QStringLiteral("kind")).toString();
        const QString title = value.value(QStringLiteral("title")).toString();
        const QString detail = value.value(QStringLiteral("message")).toString();
        emit progressChanged(detail.isEmpty() ? title : title + QStringLiteral(" ") + detail, kind != QStringLiteral("end"));
        return;
    }

    if (method == QStringLiteral("window/logMessage")) {
        emit serverLog(parameters.value(QStringLiteral("message")).toString());
        return;
    }

    if (method == QStringLiteral("window/showMessage")) {
        const QString message = parameters.value(QStringLiteral("message")).toString();
        if (parameters.value(QStringLiteral("type")).toInt() == 1) {
            emit serverError(message);
            return;
        }
        emit serverLog(message);
    }
}

void LanguageServerClient::handleResponse(int requestId, const QJsonObject& message) {
    if (requestId == m_shutdownRequestId) {
        handleShutdownResponse(message);
        return;
    }

    if (requestId == m_initializeRequestId) {
        handleInitializeResponse(message);
        return;
    }

    if (m_completionRequests.contains(requestId)) {
        handleCompletionResponse(requestId, message);
        return;
    }

    if (m_completionResolveRequests.contains(requestId)) {
        handleCompletionDocumentationResponse(requestId, message);
        return;
    }

    if (m_symbolQueryRequests.contains(requestId)) {
        handleSymbolQueryResponse(requestId, message);
        return;
    }

    if (m_callHierarchyPrepareRequests.contains(requestId)) {
        handleCallHierarchyPrepareResponse(requestId, message);
        return;
    }

    if (m_callHierarchyRequests.contains(requestId)) {
        handleCallHierarchyResponse(requestId, message);
        return;
    }

    if (m_hoverRequests.contains(requestId)) {
        handleHoverResponse(requestId, message);
        return;
    }

    if (m_signatureHelpRequests.contains(requestId)) {
        handleSignatureHelpResponse(requestId, message);
        return;
    }

    if (m_highlightRequests.contains(requestId)) {
        handleHighlightResponse(requestId, message);
        return;
    }

    if (m_documentSymbolRequests.contains(requestId)) {
        handleDocumentSymbolResponse(requestId, message);
        return;
    }

    if (m_workspaceSymbolRequests.contains(requestId)) {
        handleWorkspaceSymbolResponse(requestId, message);
        return;
    }

    if (m_semanticTokenRequests.contains(requestId)) {
        handleSemanticTokenResponse(requestId, message);
        return;
    }

    if (m_diagnosticRequests.contains(requestId)) {
        handleDiagnosticResponse(requestId, message);
        return;
    }
}

void LanguageServerClient::handleShutdownResponse(const QJsonObject& message) {
    m_shutdownRequestId = 0;

    if (message.contains(QStringLiteral("result"))) {
        sendNotification(QStringLiteral("exit"), {});
        callTransport("requestTermination");
        return;
    }

    emit serverLog(message.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(QStringLiteral("The language server rejected shutdown")));
    callTransport("requestTermination");
}

void LanguageServerClient::handleInitializeResponse(const QJsonObject& message) {
    m_initializeRequestId = 0;
    m_initializeTimer.stop();

    if (!message.contains(QStringLiteral("result"))) {
        emit serverError(message.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(QStringLiteral("The language server rejected initialization")));
        return;
    }

    applyCapabilities(message.value(QStringLiteral("result")).toObject().value(QStringLiteral("capabilities")).toObject());
    m_ready = true;
    sendNotification(QStringLiteral("initialized"), {});

    if (m_capabilities.openClose) {
        for (auto document = m_documents.begin(); document != m_documents.end(); ++document) {
            sendOpen(document.key(), document.value());
        }
    }

    emit initialized();
    return;
}

// A server decides how many candidates it sends, so they are read away from the thread that draws and bounded before they reach it.
QVector<CompletionProposal> LanguageServerClientHelper::decodeCompletions(const QJsonArray& items, int bound) {
    QVector<CompletionProposal> proposals;

    for (const auto& value : items) {
        const QJsonObject item = value.toObject();
        const QString label = item.value(QStringLiteral("label")).toString().trimmed();
        if (label.isEmpty()) {
            continue;
        }
        const QJsonObject edit = item.value(QStringLiteral("textEdit")).toObject();
        const QJsonObject range = edit.contains(QStringLiteral("range")) ? edit.value(QStringLiteral("range")).toObject() : edit.value(QStringLiteral("replace")).toObject();
        CompletionProposal proposal{label, item.value(QStringLiteral("filterText")).toString(label), item.value(QStringLiteral("detail")).toString(), LanguageServerClientHelper::hoverText(item.value(QStringLiteral("documentation"))), label, item.value(QStringLiteral("sortText")).toString(label), item, false, 0, 0, 0, 0};
        if (edit.contains(QStringLiteral("newText"))) {
            proposal.insertText = edit.value(QStringLiteral("newText")).toString();
        } else if (item.contains(QStringLiteral("insertText"))) {
            proposal.insertText = item.value(QStringLiteral("insertText")).toString();
        }
        if (!range.isEmpty()) {
            const QJsonObject start = range.value(QStringLiteral("start")).toObject();
            const QJsonObject end = range.value(QStringLiteral("end")).toObject();
            proposal.hasRange = true;
            proposal.startLine = start.value(QStringLiteral("line")).toInt();
            proposal.startCharacter = start.value(QStringLiteral("character")).toInt();
            proposal.endLine = end.value(QStringLiteral("line")).toInt();
            proposal.endCharacter = end.value(QStringLiteral("character")).toInt();
        }
        proposals.append(proposal);
    }
    // clang-format off
    std::stable_sort(proposals.begin(), proposals.end(), [](const CompletionProposal& first, const CompletionProposal& second) { return first.sortText < second.sortText; });
    // clang-format on

    if (proposals.size() > bound) {
        proposals.resize(bound);
    }

    return proposals;
}

void LanguageServerClient::handleCompletionResponse(int requestId, const QJsonObject& message) {
    const QString path = m_completionRequests.take(requestId);

    if (message.contains(QStringLiteral("error"))) {
        emit serverLog(message.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(QStringLiteral("The language server refused the completion request")));
        return;
    }

    const QJsonValue result = message.value(QStringLiteral("result"));
    const QJsonArray items = result.isArray() ? result.toArray() : result.toObject().value(QStringLiteral("items")).toArray();
    const bool incomplete = result.toObject().value(QStringLiteral("isIncomplete")).toBool(false);
    const int bound = LanguageRegistry::limits().maximumCompletions;
    // clang-format off
    auto decoded = QtConcurrent::run([items, bound]() { return LanguageServerClientHelper::decodeCompletions(items, bound); });
    decoded.then(this, [this, path, incomplete](const QVector<CompletionProposal>& proposals) { emit completionsReady(path, proposals, incomplete); });
    // clang-format on
}

void LanguageServerClient::handleCompletionDocumentationResponse(int requestId, const QJsonObject& message) {
    const QPair<QString, int> pending = m_completionResolveRequests.take(requestId);
    const QJsonObject item = message.value(QStringLiteral("result")).toObject();
    const QString documentation = LanguageServerClientHelper::hoverText(item.value(QStringLiteral("documentation"))).trimmed();
    emit completionDocumentationReady(pending.first, pending.second, documentation.isEmpty() ? item.value(QStringLiteral("detail")).toString() : documentation);
}

// A call hierarchy is asked for in two steps, because the server names the symbol under the cursor before it answers who reaches it.
void LanguageServerClient::requestCallHierarchy(const QString& path, int line, int character, CallDirection direction) {
    if (!m_ready || !m_capabilities.callHierarchy || !m_documents.contains(path)) {
        return;
    }

    m_callHierarchyPrepareRequests.insert(sendRequest(QStringLiteral("textDocument/prepareCallHierarchy"), documentPosition(path, line, character)), {path, direction});
}

bool LanguageServerClient::supportsCallHierarchy() const {
    return m_capabilities.callHierarchy;
}

void LanguageServerClient::handleCallHierarchyPrepareResponse(int requestId, const QJsonObject& message) {
    const QPair<QString, CallDirection> pending = m_callHierarchyPrepareRequests.take(requestId);
    const QJsonArray items = message.value(QStringLiteral("result")).toArray();

    if (items.isEmpty()) {
        emit callHierarchyReady(pending.first, pending.second, {});
        return;
    }

    const QString method = pending.second == CallDirection::Incoming ? QStringLiteral("callHierarchy/incomingCalls") : QStringLiteral("callHierarchy/outgoingCalls");
    m_callHierarchyRequests.insert(sendRequest(method, QJsonObject{{QStringLiteral("item"), items.first()}}), pending);
}

void LanguageServerClient::handleCallHierarchyResponse(int requestId, const QJsonObject& message) {
    const QPair<QString, CallDirection> pending = m_callHierarchyRequests.take(requestId);
    const QString side = pending.second == CallDirection::Incoming ? QStringLiteral("from") : QStringLiteral("to");
    QVector<WorkspaceSymbolEntry> entries;

    for (const auto& value : message.value(QStringLiteral("result")).toArray()) {
        const QJsonObject item = value.toObject().value(side).toObject();
        const SourceLocation location = LanguageServerClientHelper::locationOf(item);

        if (!item.value(QStringLiteral("name")).toString().isEmpty() && !location.path.isEmpty()) {
            entries.append({item.value(QStringLiteral("name")).toString(), item.value(QStringLiteral("detail")).toString(), item.value(QStringLiteral("kind")).toInt(), location});
        }
    }

    emit callHierarchyReady(pending.first, pending.second, entries);
}

void LanguageServerClient::handleSymbolQueryResponse(int requestId, const QJsonObject& message) {
    const PendingRequest request = m_symbolQueryRequests.take(requestId);
    emit symbolLocationsReady(request.path, request.kind, LanguageServerClientHelper::locationsOf(message.value(QStringLiteral("result"))));
}

void LanguageServerClient::handleHoverResponse(int requestId, const QJsonObject& message) {
    const QString path = m_hoverRequests.take(requestId);
    emit hoverReady(path, LanguageServerClientHelper::hoverText(message.value(QStringLiteral("result")).toObject().value(QStringLiteral("contents"))).trimmed());
}

void LanguageServerClient::handleSignatureHelpResponse(int requestId, const QJsonObject& message) {
    const QString path = m_signatureHelpRequests.take(requestId);
    const QJsonObject result = message.value(QStringLiteral("result")).toObject();
    SignatureHelpInfo help{{}, result.value(QStringLiteral("activeSignature")).toInt(0)};

    for (const auto& signature : result.value(QStringLiteral("signatures")).toArray()) {
        help.signatures.append(signature.toObject().value(QStringLiteral("label")).toString());
    }

    emit signatureHelpReady(path, help);
}

void LanguageServerClient::handleHighlightResponse(int requestId, const QJsonObject& message) {
    const QString path = m_highlightRequests.take(requestId);
    QVector<SourceLocation> highlights;

    for (const auto& entry : message.value(QStringLiteral("result")).toArray()) {
        SourceLocation highlight = LanguageServerClientHelper::locationOf(entry.toObject());
        highlight.path = path;
        highlights.append(highlight);
    }

    emit documentHighlightsReady(path, highlights);
}

void LanguageServerClient::handleDocumentSymbolResponse(int requestId, const QJsonObject& message) {
    const QString path = m_documentSymbolRequests.take(requestId);
    emit documentSymbolsReady(path, LanguageServerClientHelper::symbolNodes(message.value(QStringLiteral("result")).toArray(), 0));
}

void LanguageServerClient::handleWorkspaceSymbolResponse(int requestId, const QJsonObject& message) {
    m_workspaceSymbolRequests.remove(requestId);
    QVector<WorkspaceSymbolEntry> symbols;

    for (const auto& entry : message.value(QStringLiteral("result")).toArray()) {
        const QJsonObject item = entry.toObject();
        const SourceLocation location = LanguageServerClientHelper::locationOf(item);
        if (!item.value(QStringLiteral("name")).toString().isEmpty() && !location.path.isEmpty()) {
            symbols.append({item.value(QStringLiteral("name")).toString(), item.value(QStringLiteral("containerName")).toString(), item.value(QStringLiteral("kind")).toInt(), location});
        }
    }

    emit workspaceSymbolsReady(symbols);
}

SemanticTokenSet LanguageServerClientHelper::decodeSemanticTokens(const QJsonArray& data, const QStringList& types, const QStringList& modifiers) {
    const QMap<QString, HighlightRole>& painted = LanguageRegistry::semanticRoles();
    SemanticTokenSet decoded;
    int line = 0;
    int start = 0;

    for (qsizetype index = 0; index + 4 < data.size(); index += 5) {
        const int deltaLine = data.at(index).toInt();
        const int deltaStart = data.at(index + 1).toInt();
        const int type = data.at(index + 3).toInt();
        line += deltaLine;
        start = deltaLine == 0 ? start + deltaStart : deltaStart;

        if (type < 0 || type >= types.size() || !painted.contains(types.at(type))) {
            continue;
        }

        const unsigned int declared = static_cast<unsigned int>(data.at(index + 4).toInt());
        SemanticToken token{line, start, data.at(index + 2).toInt(), types.at(type), false, false};

        for (qsizetype bit = 0; bit < modifiers.size() && bit < 32; ++bit) {
            if ((declared & (1U << bit)) == 0U) {
                continue;
            }
            token.deprecated = token.deprecated || modifiers.at(bit) == QStringLiteral("deprecated");
            token.readOnly = token.readOnly || modifiers.at(bit) == QStringLiteral("readonly");
        }

        decoded[line].append(token);
    }

    return decoded;
}

void LanguageServerClient::handleSemanticTokenResponse(int requestId, const QJsonObject& message) {
    const QString path = m_semanticTokenRequests.take(requestId);
    const QJsonArray data = message.value(QStringLiteral("result")).toObject().value(QStringLiteral("data")).toArray();
    const QStringList types = m_capabilities.semanticTokenTypes;
    const QStringList modifiers = m_capabilities.semanticTokenModifiers;
    // clang-format off
    auto decoded = QtConcurrent::run([data, types, modifiers]() { return LanguageServerClientHelper::decodeSemanticTokens(data, types, modifiers); });
    decoded.then(this, [this, path](const SemanticTokenSet& tokens) { emit semanticTokensReady(path, tokens); });
    // clang-format on
}

void LanguageServerClient::handleDiagnosticResponse(int requestId, const QJsonObject& message) {
    const QString path = m_diagnosticRequests.take(requestId);
    const QJsonObject report = message.value(QStringLiteral("result")).toObject();

    if (report.value(QStringLiteral("kind")).toString() == QStringLiteral("full")) {
        emit diagnosticsPublished(path, LanguageServerClientHelper::diagnosticsOf(path, report.value(QStringLiteral("items")).toArray()));
    }
}

// A capability a server announces after initialization decides what we may send exactly as one it declared in its result.
void LanguageServerClient::applyRegistration(const QString& method, const QJsonObject& options, bool registered) {
    static const QHash<QString, bool Capabilities::*> plain{
        {QStringLiteral("textDocument/definition"), &Capabilities::definition}, {QStringLiteral("textDocument/declaration"), &Capabilities::declaration}, {QStringLiteral("textDocument/typeDefinition"), &Capabilities::typeDefinition}, {QStringLiteral("textDocument/implementation"), &Capabilities::implementation}, {QStringLiteral("textDocument/references"), &Capabilities::references}, {QStringLiteral("textDocument/hover"), &Capabilities::hover}, {QStringLiteral("textDocument/documentHighlight"), &Capabilities::documentHighlights}, {QStringLiteral("textDocument/documentSymbol"), &Capabilities::documentSymbols}, {QStringLiteral("workspace/symbol"), &Capabilities::workspaceSymbols}, {QStringLiteral("textDocument/diagnostic"), &Capabilities::pullDiagnostics}, {QStringLiteral("textDocument/prepareCallHierarchy"), &Capabilities::callHierarchy},
    };

    if (const auto flag = plain.value(method, nullptr); flag != nullptr) {
        m_capabilities.*flag = registered;
        return;
    }

    if (method == QStringLiteral("textDocument/completion")) {
        m_capabilities.completion = registered;
        m_capabilities.completionResolve = registered && options.value(QStringLiteral("resolveProvider")).toBool(false);
        m_capabilities.completionTriggerCharacters = registered ? LanguageServerClientHelper::triggerCharacters(options) : QStringList{};
        return;
    }

    if (method == QStringLiteral("textDocument/signatureHelp")) {
        m_capabilities.signatureHelp = registered;
        m_capabilities.signatureHelpTriggerCharacters = registered ? LanguageServerClientHelper::triggerCharacters(options) : QStringList{};
        return;
    }

    if (method == QStringLiteral("textDocument/semanticTokens")) {
        m_capabilities.semanticTokens = registered;
        m_capabilities.semanticTokenTypes.clear();
        m_capabilities.semanticTokenModifiers.clear();

        if (registered) {
            const QJsonObject legend = options.value(QStringLiteral("legend")).toObject();
            for (const auto& type : legend.value(QStringLiteral("tokenTypes")).toArray()) {
                m_capabilities.semanticTokenTypes.append(type.toString());
            }
            for (const auto& modifier : legend.value(QStringLiteral("tokenModifiers")).toArray()) {
                m_capabilities.semanticTokenModifiers.append(modifier.toString());
            }
        }
        return;
    }

    if (method == QStringLiteral("textDocument/didChange")) {
        const int kind = options.value(QStringLiteral("syncKind")).toInt(1);
        m_capabilities.sync = !registered ? DocumentSyncKind::None : (kind == 2 ? DocumentSyncKind::Incremental : (kind == 1 ? DocumentSyncKind::Full : DocumentSyncKind::None));
        return;
    }

    if (method == QStringLiteral("textDocument/didSave")) {
        m_capabilities.save = registered;
        m_capabilities.saveIncludesText = registered && options.value(QStringLiteral("includeText")).toBool(false);
        return;
    }

    if (method == QStringLiteral("textDocument/didOpen") || method == QStringLiteral("textDocument/didClose")) {
        m_capabilities.openClose = registered;
    }
}

// A server that declares nothing keeps the protocol defaults, and one that declares a capability decides what we are allowed to send.
void LanguageServerClient::applyCapabilities(const QJsonObject& capabilities) {
    m_capabilities = {};
    const QJsonValue synchronization = capabilities.value(QStringLiteral("textDocumentSync"));
    const int kind = synchronization.isObject() ? synchronization.toObject().value(QStringLiteral("change")).toInt(1) : synchronization.toInt(1);
    m_capabilities.sync = kind == 2 ? DocumentSyncKind::Incremental : (kind == 1 ? DocumentSyncKind::Full : DocumentSyncKind::None);

    if (synchronization.isObject()) {
        const QJsonObject object = synchronization.toObject();
        const QJsonValue save = object.value(QStringLiteral("save"));
        m_capabilities.openClose = object.value(QStringLiteral("openClose")).toBool(true);
        m_capabilities.save = LanguageServerClientHelper::declaredProvider(save);
        m_capabilities.saveIncludesText = save.toObject().value(QStringLiteral("includeText")).toBool(false);
    }

    const QJsonValue completion = capabilities.value(QStringLiteral("completionProvider"));
    m_capabilities.completion = LanguageServerClientHelper::declaredProvider(completion);
    m_capabilities.completionResolve = completion.toObject().value(QStringLiteral("resolveProvider")).toBool(false);

    m_capabilities.completionTriggerCharacters = LanguageServerClientHelper::triggerCharacters(completion);

    m_capabilities.definition = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("definitionProvider")));
    m_capabilities.declaration = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("declarationProvider")));
    m_capabilities.typeDefinition = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("typeDefinitionProvider")));
    m_capabilities.implementation = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("implementationProvider")));
    m_capabilities.references = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("referencesProvider")));
    m_capabilities.hover = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("hoverProvider")));
    m_capabilities.documentHighlights = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("documentHighlightProvider")));
    m_capabilities.documentSymbols = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("documentSymbolProvider")));
    m_capabilities.workspaceSymbols = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("workspaceSymbolProvider")));
    m_capabilities.pullDiagnostics = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("diagnosticProvider")));
    m_capabilities.callHierarchy = LanguageServerClientHelper::declaredProvider(capabilities.value(QStringLiteral("callHierarchyProvider")));

    const QJsonValue signatureHelp = capabilities.value(QStringLiteral("signatureHelpProvider"));
    m_capabilities.signatureHelp = LanguageServerClientHelper::declaredProvider(signatureHelp);

    m_capabilities.signatureHelpTriggerCharacters = LanguageServerClientHelper::triggerCharacters(signatureHelp);

    const QJsonObject semanticTokens = capabilities.value(QStringLiteral("semanticTokensProvider")).toObject();

    for (const auto& modifier : semanticTokens.value(QStringLiteral("legend")).toObject().value(QStringLiteral("tokenModifiers")).toArray()) {
        m_capabilities.semanticTokenModifiers.append(modifier.toString());
    }

    for (const auto& type : semanticTokens.value(QStringLiteral("legend")).toObject().value(QStringLiteral("tokenTypes")).toArray()) {
        m_capabilities.semanticTokenTypes.append(type.toString());
    }

    m_capabilities.semanticTokens = !m_capabilities.semanticTokenTypes.isEmpty() && LanguageServerClientHelper::declaredProvider(semanticTokens.value(QStringLiteral("full")));
}

void LanguageServerClient::cancelPendingFor(QHash<int, QString>& requests, const QString& path) {
    for (auto request = requests.begin(); request != requests.end();) {
        if (request.value() != path) {
            ++request;
            continue;
        }
        cancelRequest(request.key());
        request = requests.erase(request);
    }
}

void LanguageServerClient::trackedRequest(const QString& method, const QJsonObject& parameters, QHash<int, QString>& requests, const QString& path) {
    cancelPendingFor(requests, path);
    requests.insert(sendRequest(method, parameters), path);
}

void LanguageServerClient::send(const QJsonObject& message) {
    if (m_transport == nullptr) {
        return;
    }

    QMetaObject::invokeMethod(m_transport, "send", Qt::QueuedConnection, Q_ARG(QJsonObject, message));
}

void LanguageServerClient::sendResponse(const QJsonValue& id, const QJsonValue& result) {
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
}

void LanguageServerClient::sendErrorResponse(const QJsonValue& id, int code, const QString& message) {
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}});
}

void LanguageServerClient::sendNotification(const QString& method, const QJsonObject& parameters) {
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), method}, {QStringLiteral("params"), parameters}});
}

int LanguageServerClient::sendRequest(const QString& method, const QJsonObject& parameters) {
    const int requestId = ++m_nextRequestId;
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), requestId}, {QStringLiteral("method"), method}, {QStringLiteral("params"), parameters}});
    return requestId;
}

void LanguageServerClient::cancelRequest(int requestId) {
    sendNotification(QStringLiteral("$/cancelRequest"), {{QStringLiteral("id"), requestId}});
}

void LanguageServerClient::sendOpen(const QString& path, Document& document) {
    sendNotification(QStringLiteral("textDocument/didOpen"), {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri(path)}, {QStringLiteral("languageId"), document.languageId}, {QStringLiteral("version"), document.version}, {QStringLiteral("text"), document.text}}}});
    document.opened = true;
}

void LanguageServerClient::sendChange(const QString& path, Document& document, const QJsonArray& contentChanges) {
    ++document.version;

    if (!m_ready || !document.opened || contentChanges.isEmpty()) {
        return;
    }

    sendNotification(QStringLiteral("textDocument/didChange"), {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri(path)}, {QStringLiteral("version"), document.version}}}, {QStringLiteral("contentChanges"), contentChanges}});
}

QJsonObject LanguageServerClient::positionOf(const QString& text, int offset) const {
    const qsizetype bounded = std::clamp<qsizetype>(offset, 0, text.size());

    if (bounded == 0) {
        return {{QStringLiteral("line"), 0}, {QStringLiteral("character"), 0}};
    }

    const qsizetype lineStart = text.lastIndexOf(QLatin1Char('\n'), bounded - 1) + 1;
    const qsizetype line = QStringView(text).left(bounded).count(QLatin1Char('\n'));
    return {{QStringLiteral("line"), static_cast<int>(line)}, {QStringLiteral("character"), static_cast<int>(bounded - lineStart)}};
}

// The end of a range is reached by walking the text it covers, because scanning the whole file again for it costs the size of the file on every edit.
QJsonObject LanguageServerClient::positionAfter(const QJsonObject& start, QStringView covered) const {
    const qsizetype breaks = covered.count(QLatin1Char('\n'));

    if (breaks == 0) {
        return {{QStringLiteral("line"), start.value(QStringLiteral("line")).toInt()}, {QStringLiteral("character"), start.value(QStringLiteral("character")).toInt() + static_cast<int>(covered.size())}};
    }

    const qsizetype lastBreak = covered.lastIndexOf(QLatin1Char('\n'));
    return {{QStringLiteral("line"), start.value(QStringLiteral("line")).toInt() + static_cast<int>(breaks)}, {QStringLiteral("character"), static_cast<int>(covered.size() - lastBreak - 1)}};
}

QJsonObject LanguageServerClient::documentPosition(const QString& path, int line, int character) const {
    return {{QStringLiteral("textDocument"), LanguageServerClientHelper::textDocument(uri(path))}, {QStringLiteral("position"), QJsonObject{{QStringLiteral("line"), line}, {QStringLiteral("character"), character}}}};
}

// The server answers with the address it built, so the answer is resolved back to the document the workspace really has open.
QString LanguageServerClient::documentPathOf(const QString& uri) const {
    QString local = QUrl(uri).toLocalFile();

    if (m_documents.contains(local)) {
        return local;
    }

    for (auto document = m_documents.constBegin(); document != m_documents.constEnd(); ++document) {
        if (StatePaths::samePath(document.key(), local)) {
            return document.key();
        }
    }

    return local;
}

QString LanguageServerClient::uri(const QString& path) const {
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

} // namespace workpane::plugins::codeeditor
