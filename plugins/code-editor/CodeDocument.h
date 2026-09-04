#pragma once

#include "CodeEditorState.h"
#include "CodeEditorWidget.h"
#include "CodeSyntaxHighlighter.h"
#include "EditorConfig.h"
#include "LanguageServerClient.h"
#include "plugins/PluginInterface.h"
#include "ui/FindBar.h"

#include <QFileSystemWatcher>
#include <QTextDocument>
#include <QTimer>

#include <memory>
#include <optional>

namespace workpane::plugins::codeeditor {

class CodeDocument final : public QWidget {
    Q_OBJECT

  public:
    CodeDocument(const QString& path, const QString& rootPath, bool wordWrap, CodeEditorFont font, CodeColorScheme scheme, TextCharset defaultCharset, PluginHost& host, QWidget* parent = nullptr);
    ~CodeDocument() override;

    [[nodiscard]] const QString& path() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] int cursorPosition() const;
    [[nodiscard]] int cursorLine() const;
    [[nodiscard]] int cursorColumn() const;
    [[nodiscard]] const LanguageDefinition& language() const;
    [[nodiscard]] const EditorConfigProperties& editorConfig() const;
    [[nodiscard]] LineEnding lineEnding() const;
    [[nodiscard]] TextCharset charset() const;
    [[nodiscard]] CodeEditorWidget& editor() const;
    void setCursorPosition(int position);
    void setCursorLocation(int line, int character);
    void setLanguageServer(LanguageServerClient* server);
    void setWordWrap(bool enabled);
    void setEditorFont(const CodeEditorFont& font);
    void setColorScheme(const CodeColorScheme& scheme);
    void updatePath(const QString& path);
    void reloadEditorConfig();
    void reopenWithCharset(TextCharset charset);
    void saveWithCharset(TextCharset charset);
    void recheckExternalChange();
    void requestSymbolQuery(SymbolQueryKind kind);
    void requestCallHierarchy(CallDirection direction);
    void refreshAnalysis();
    void reload();
    void save();

  signals:
    void titleChanged();
    void stateChanged();
    void loaded(CodeDocument* document);
    void editorConfigChanged();
    void operationFailed(const QString& message);
    void externalChangeConflict(const QString& path);
    void externalFileRemoved(const QString& path);
    void diagnosticsChanged(const QString& path, const QVector<LanguageDiagnostic>& diagnostics);
    void navigationRequested(const QString& path, int line, int character);
    void referencesReady(const QString& path, const QVector<SourceLocation>& locations);
    void callHierarchyReady(const QString& path, CallDirection direction, const QVector<WorkspaceSymbolEntry>& entries);
    void outlineChanged(const QString& path, const QVector<DocumentSymbolNode>& symbols);
    void analysisProgress(const QString& message, bool active);

  private slots:
    void watchedFileChanged();

  private:
    struct DecodedContent final {
        QString text;
        QString errorKey;
        QString errorDetail;
        QByteArray digest;
        TextCharset charset{TextCharset::Utf8};
        LineEnding lineEnding{LineEnding::Lf};
    };

    void applyContent(const QByteArray& content);
    void applyDecoded(const DecodedContent& decoded);
    void documentChanged(int position, int removedCharacters, int addedCharacters);
    void loadEditorConfig();
    void applyEditorConfig(EditorConfigProperties properties);
    void findText(const QString& query, bool forward);
    void searchFromAnchor(const QString& query);
    void refreshSearchMatches(const QString& query);
    [[nodiscard]] QTextDocument::FindFlags searchFlags(bool forward) const;
    void scheduleExternalChange();
    void synchronizeLanguageServer();
    void requestCompletionOnTrigger(const QString& addedText);
    void requestAnalysis();
    void showContextMenu(const QPoint& position);
    // The plain text of Qt rewrites a non-breaking space and a line separator, so the buffer is read raw everywhere its exact content matters.
    [[nodiscard]] QString bufferText() const;
    [[nodiscard]] std::optional<QByteArray> encodedContent() const;
    void writeContent();

    QString m_path;
    QString m_rootPath;
    TextCharset m_defaultCharset{TextCharset::Latin1};
    std::optional<TextCharset> m_selectedCharset;
    bool m_rereading{false};
    PluginHost& m_host;
    CodeEditorWidget* m_editor{nullptr};
    ui::FindBar* m_findBar{nullptr};
    LanguageDefinition m_language;
    EditorConfigProperties m_editorConfig;
    QByteArray m_storedDigest;
    QByteArray m_appliedDigest;
    LineEnding m_detectedLineEnding{LineEnding::Lf};
    TextCharset m_detectedCharset{TextCharset::Utf8};
    std::unique_ptr<CodeSyntaxHighlighter> m_highlighter;
    CodeColorScheme m_scheme;
    QFileSystemWatcher m_watcher;
    QTimer m_externalChangeTimer;
    QTimer m_languageServerTimer;
    QTimer m_analysisTimer;
    QTimer m_highlightTimer;
    QVector<DocumentEdit> m_pendingEdits;
    QVector<QJsonObject> m_completionItems;
    QPointer<LanguageServerClient> m_languageServer;
    int m_searchAnchor{0};
    quint64 m_loadGeneration{0};
    quint64 m_contentRevision{0};
    quint64 m_editorConfigGeneration{0};
    bool m_loading{false};
    bool m_dirty{false};
    bool m_saving{false};
    bool m_saveRequested{false};
};

} // namespace workpane::plugins::codeeditor
