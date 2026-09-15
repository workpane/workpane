#pragma once

#include "LanguageRegistry.h"
#include "LanguageServerTransport.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>

namespace workpane::plugins::codeeditor {

struct SourceLocation final {
    QString path;
    int line{0};
    int character{0};
    int endLine{0};
    int endCharacter{0};

    bool operator==(const SourceLocation& other) const = default;
};

struct RelatedDiagnostic final {
    SourceLocation location;
    QString message;

    bool operator==(const RelatedDiagnostic& other) const = default;
};

struct LanguageDiagnostic final {
    QString path;
    int startLine{0};
    int startCharacter{0};
    int endLine{0};
    int endCharacter{0};
    int severity{0};
    QString message;
    QString code;
    QString source;
    bool unnecessary{false};
    bool deprecated{false};
    QVector<RelatedDiagnostic> related;

    bool operator==(const LanguageDiagnostic& other) const = default;
};

struct CompletionProposal final {
    QString label;
    QString filterText;
    QString detail;
    QString documentation;
    QString insertText;
    QString sortText;
    QJsonObject item;
    bool hasRange{false};
    int startLine{0};
    int startCharacter{0};
    int endLine{0};
    int endCharacter{0};
};

struct DocumentEdit final {
    int offset{0};
    int removedLength{0};
    QString addedText;
};

struct DocumentSymbolNode final {
    QString name;
    QString detail;
    int kind{0};
    int line{0};
    int character{0};
    QVector<DocumentSymbolNode> children;

    // The comparison is defined once the type is complete, because a defaulted one asks the container of itself for an operator it cannot have yet.
    [[nodiscard]] bool operator==(const DocumentSymbolNode& other) const;
};

inline bool DocumentSymbolNode::operator==(const DocumentSymbolNode& other) const {
    return name == other.name && detail == other.detail && kind == other.kind && line == other.line && character == other.character && children == other.children;
}

struct WorkspaceSymbolEntry final {
    QString name;
    QString container;
    int kind{0};
    SourceLocation location;
};

struct SignatureHelpInfo final {
    QStringList signatures;
    int activeSignature{0};
};

struct SemanticToken final {
    int line{0};
    int startCharacter{0};
    int length{0};
    QString type;
    bool deprecated{false};
    bool readOnly{false};

    bool operator==(const SemanticToken& other) const = default;
};

// The server decides how many tokens it sends, so they are decoded and grouped away from the thread that draws.
using SemanticTokenSet = QHash<int, QVector<SemanticToken>>;

enum class SymbolQueryKind { Definition, Declaration, TypeDefinition, Implementation, References };

enum class CallDirection { Incoming, Outgoing };

class LanguageServerClient final : public QObject {
    Q_OBJECT

  public:
    LanguageServerClient(ResolvedLanguageServer server, QString rootPath, QObject* parent = nullptr);
    ~LanguageServerClient() override;

    // A transport thread outlives the client that asked it to end, so teardown waits for the ones still finishing before the code they run is unloaded.
    static void drainTransports();

    void start();
    void openDocument(const QString& path, const QString& text, const QString& languageId);
    void replaceDocument(const QString& path, const QString& text);
    bool editDocument(const QString& path, const QVector<DocumentEdit>& edits);
    void saveDocument(const QString& path, const QString& text);
    void closeDocument(const QString& path);
    void requestCompletion(const QString& path, int line, int character);
    void requestCompletionDocumentation(const QString& path, int row, const QJsonObject& item);
    void requestSymbolQuery(const QString& path, int line, int character, SymbolQueryKind kind);
    void requestCallHierarchy(const QString& path, int line, int character, CallDirection direction);
    void requestHover(const QString& path, int line, int character);
    void requestSignatureHelp(const QString& path, int line, int character);
    void requestDocumentHighlights(const QString& path, int line, int character);
    void requestDocumentSymbols(const QString& path);
    void requestWorkspaceSymbols(const QString& query);
    void requestSemanticTokens(const QString& path);
    void requestDiagnostics(const QString& path);
    void notifyWatchedFilesChanged(const QStringList& paths, int changeType);
    void stop();
    [[nodiscard]] bool ready() const;
    [[nodiscard]] const ResolvedLanguageServer& configuration() const;
    [[nodiscard]] const QStringList& completionTriggerCharacters() const;
    [[nodiscard]] bool supports(SymbolQueryKind kind) const;
    [[nodiscard]] bool supportsCallHierarchy() const;
    [[nodiscard]] const QStringList& signatureHelpTriggerCharacters() const;

  signals:
    void diagnosticsPublished(const QString& path, const QVector<LanguageDiagnostic>& diagnostics);
    void completionsReady(const QString& path, const QVector<CompletionProposal>& proposals, bool incomplete);
    void completionDocumentationReady(const QString& path, int row, const QString& documentation);
    void symbolLocationsReady(const QString& path, SymbolQueryKind kind, const QVector<SourceLocation>& locations);
    void hoverReady(const QString& path, const QString& contents);
    void signatureHelpReady(const QString& path, const SignatureHelpInfo& help);
    void documentHighlightsReady(const QString& path, const QVector<SourceLocation>& highlights);
    void documentSymbolsReady(const QString& path, const QVector<DocumentSymbolNode>& symbols);
    void workspaceSymbolsReady(const QVector<WorkspaceSymbolEntry>& symbols);
    void callHierarchyReady(const QString& path, CallDirection direction, const QVector<WorkspaceSymbolEntry>& entries);
    void semanticTokensReady(const QString& path, const SemanticTokenSet& tokens);
    void progressChanged(const QString& message, bool active);
    // The requests a document made before initialization were never sent, so it is told when the server can finally answer them.
    void initialized();
    void serverError(const QString& message);
    void serverLog(const QString& message);
    void stopped();

  private:
    enum class DocumentSyncKind { None, Full, Incremental };

    struct Document final {
        QString text;
        QString languageId;
        int version{1};
        bool opened{false};
    };

    struct Capabilities final {
        DocumentSyncKind sync{DocumentSyncKind::Full};
        bool openClose{true};
        bool save{false};
        bool saveIncludesText{false};
        bool completion{true};
        bool completionResolve{false};
        bool definition{false};
        bool declaration{false};
        bool typeDefinition{false};
        bool implementation{false};
        bool references{false};
        bool hover{false};
        bool signatureHelp{false};
        bool documentHighlights{false};
        bool documentSymbols{false};
        bool workspaceSymbols{false};
        bool semanticTokens{false};
        bool callHierarchy{false};
        bool pullDiagnostics{false};
        QStringList completionTriggerCharacters;
        QStringList signatureHelpTriggerCharacters;
        QStringList semanticTokenTypes;
        QStringList semanticTokenModifiers;
    };

    void processStarted();
    void processFinished(int exitCode);
    void handleMessage(const QJsonObject& message);
    void handleServerRequest(const QJsonValue& id, const QString& method, const QJsonObject& parameters);
    void handleServerNotification(const QString& method, const QJsonObject& parameters);
    void handleResponse(int requestId, const QJsonObject& message);
    void handleShutdownResponse(const QJsonObject& message);
    void handleInitializeResponse(const QJsonObject& message);
    void handleCompletionResponse(int requestId, const QJsonObject& message);
    void handleCompletionDocumentationResponse(int requestId, const QJsonObject& message);
    void handleSymbolQueryResponse(int requestId, const QJsonObject& message);
    void handleCallHierarchyPrepareResponse(int requestId, const QJsonObject& message);
    void handleCallHierarchyResponse(int requestId, const QJsonObject& message);
    void handleHoverResponse(int requestId, const QJsonObject& message);
    void handleSignatureHelpResponse(int requestId, const QJsonObject& message);
    void handleHighlightResponse(int requestId, const QJsonObject& message);
    void handleDocumentSymbolResponse(int requestId, const QJsonObject& message);
    void handleWorkspaceSymbolResponse(int requestId, const QJsonObject& message);
    void handleSemanticTokenResponse(int requestId, const QJsonObject& message);
    void handleDiagnosticResponse(int requestId, const QJsonObject& message);
    void applyCapabilities(const QJsonObject& capabilities);
    void applyRegistration(const QString& method, const QJsonObject& options, bool registered);
    void send(const QJsonObject& message);
    void sendResponse(const QJsonValue& id, const QJsonValue& result);
    void callTransport(const char* method);
    void sendErrorResponse(const QJsonValue& id, int code, const QString& message);
    void sendNotification(const QString& method, const QJsonObject& parameters);
    int sendRequest(const QString& method, const QJsonObject& parameters);
    void cancelRequest(int requestId);
    void cancelPendingFor(QHash<int, QString>& requests, const QString& path);
    void trackedRequest(const QString& method, const QJsonObject& parameters, QHash<int, QString>& requests, const QString& path);
    void sendOpen(const QString& path, Document& document);
    void sendChange(const QString& path, Document& document, const QJsonArray& contentChanges);
    [[nodiscard]] QJsonObject positionOf(const QString& text, int offset) const;
    [[nodiscard]] QJsonObject positionAfter(const QJsonObject& start, QStringView covered) const;
    [[nodiscard]] QJsonObject documentPosition(const QString& path, int line, int character) const;
    [[nodiscard]] QString uri(const QString& path) const;
    [[nodiscard]] QString documentPathOf(const QString& uri) const;

    ResolvedLanguageServer m_server;
    QString m_rootPath;
    QThread* m_transportThread{nullptr};
    QPointer<LanguageServerTransport> m_transport;
    bool m_running{false};
    QTimer m_stopTimer;
    QTimer m_initializeTimer;
    QElapsedTimer m_restartWindow;
    QHash<QString, Document> m_documents;
    struct PendingRequest final {
        QString path;
        SymbolQueryKind kind{SymbolQueryKind::Definition};
    };

    QHash<int, QString> m_completionRequests;
    QHash<int, QPair<QString, int>> m_completionResolveRequests;
    QHash<int, PendingRequest> m_symbolQueryRequests;
    QHash<int, QPair<QString, CallDirection>> m_callHierarchyPrepareRequests;
    QHash<int, QPair<QString, CallDirection>> m_callHierarchyRequests;
    QHash<int, QString> m_hoverRequests;
    QHash<int, QString> m_signatureHelpRequests;
    QHash<int, QString> m_highlightRequests;
    QHash<int, QString> m_documentSymbolRequests;
    QHash<int, QString> m_semanticTokenRequests;
    QHash<int, QString> m_diagnosticRequests;
    QSet<int> m_workspaceSymbolRequests;
    Capabilities m_capabilities;
    int m_nextRequestId{0};
    int m_initializeRequestId{0};
    int m_shutdownRequestId{0};
    int m_restartsUsed{0};
    bool m_ready{false};
    bool m_stopping{false};
};

} // namespace workpane::plugins::codeeditor
