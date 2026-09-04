#pragma once

#include "CodeDocument.h"
#include "CodeEditorState.h"
#include "ui/Components.h"
#include "ui/TabBar.h"

#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>

namespace workpane::plugins::codeeditor {

class CodeWorkspaceView final : public QWidget {
    Q_OBJECT

  public:
    CodeWorkspaceView(CodeWorkspaceState state, QVector<ResolvedLanguageServer> languageServers, bool wordWrap, CodeEditorFont font, CodeColorScheme scheme, TextCharset defaultCharset, PluginHost& host, QWidget* parent = nullptr);
    ~CodeWorkspaceView() override;

    [[nodiscard]] const QString& workspaceId() const;
    [[nodiscard]] const QString& rootPath() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] CodeWorkspaceState state() const;
    [[nodiscard]] bool canClose();
    void openFile(const QString& path, int cursorPosition = 0);
    void openLocation(const QString& path, int line, int character);
    void saveAll();
    void setLanguageServers(QVector<ResolvedLanguageServer> languageServers);
    void setWordWrap(bool enabled);
    void setEditorFont(const CodeEditorFont& font);
    void setColorScheme(const CodeColorScheme& scheme);
    [[nodiscard]] bool wordWrapEnabled() const;
    void renameEntry(const QString& path, const QString& name);
    void closeCurrentDocument();
    void reloadEditorConfigs();
    [[nodiscard]] QStringList watchedEditorConfigPaths() const;

  signals:
    void stateChanged();
    void wordWrapToggled();
    void operationFailed(const QString& message);

  private:
    void showContextMenu(const QPoint& position);
    void updateStatusBar();
    [[nodiscard]] QString promptedEntryPath(const QString& titleKey, const QString& directory);
    void createFile(const QString& directory);
    void createDirectory(const QString& directory);
    void renamePath(const QString& path);
    void movePath(const QString& path);
    void removePath(const QString& path);
    void closeDocument(int index);
    void showFileFinder();
    void showEncodingMenu();
    void applyEncodingChoice(QAction* action);
    void updateDocumentTitle(CodeDocument* document);
    void applyFileFilter();
    void narrowLoadedTree(const QString& path);
    bool narrowTree(const QModelIndex& parent, int depth);
    void revealInTree(int index);
    void updateDocumentPaths(const QString& source, const QString& destination);
    void updateDiagnostics(const QString& languageId, const QString& path, const QVector<LanguageDiagnostic>& diagnostics);
    void refreshProblems();
    void refreshWorkspaceSearch();
    void updateOutline(const QString& path, const QVector<DocumentSymbolNode>& symbols);
    void showReferences(const QVector<SourceLocation>& locations);
    void showCallHierarchy(CallDirection direction, const QVector<WorkspaceSymbolEntry>& entries);
    void showWorkspaceSymbols(const QVector<WorkspaceSymbolEntry>& symbols);
    void searchSymbols(const QString& query);
    void refreshSymbolPanel();
    void refreshEditorConfigWatch();
    void notifyWatchedChange(const QStringList& paths, int changeType);
    void connectSymbolSources(LanguageServerClient* server);
    [[nodiscard]] CodeDocument* document(const QString& path) const;
    // Every page of the document strip is one of these, so the tab widget is read through this rather than cast at each caller.
    [[nodiscard]] CodeDocument* documentAt(int index) const;
    [[nodiscard]] CodeDocument* currentDocument() const;
    // The open documents in the order the strip shows them, so a caller iterates a typed list rather than a strip of widgets.
    [[nodiscard]] QVector<CodeDocument*> documents() const;
    [[nodiscard]] LanguageServerClient* languageServer(const LanguageDefinition& language);
    [[nodiscard]] QString selectedPath() const;
    [[nodiscard]] QString targetDirectory(const QString& path) const;
    [[nodiscard]] QString containedPath(const QString& path) const;
    [[nodiscard]] bool containsPath(const QString& path) const;
    void reportResult(const Result<void>& result);

    // Diagnostics belong to the server that published them and to the file they name, so the surface shows the workspace and not only what is open.
    QHash<QString, QHash<QString, QVector<LanguageDiagnostic>>> m_diagnostics;
    QString m_outlinePath;
    QVector<DocumentSymbolNode> m_outline;
    CodeWorkspaceState m_initialState;
    TextCharset m_defaultCharset{TextCharset::Latin1};
    PluginHost& m_host;
    QFileSystemModel* m_fileModel{nullptr};
    QTreeView* m_tree{nullptr};
    ui::FilterField* m_fileFilter{nullptr};
    QString m_fileNeedle;
    ui::TabWidget* m_documents{nullptr};
    QTreeWidget* m_problems{nullptr};
    QWidget* m_problemsPage{nullptr};
    QLineEdit* m_problemFilter{nullptr};
    QTreeWidget* m_references{nullptr};
    QWidget* m_searchPage{nullptr};
    QLineEdit* m_searchQuery{nullptr};
    QTreeWidget* m_searchResults{nullptr};
    quint64 m_searchRevision{0};
    QTreeWidget* m_symbols{nullptr};
    QLineEdit* m_symbolSearch{nullptr};
    ui::TabWidget* m_bottomPanel{nullptr};
    QLabel* m_analysis{nullptr};
    QFileSystemWatcher m_editorConfigWatcher;
    QStringList m_watchedEditorConfigPaths;
    QLabel* m_cursorLocation{nullptr};
    QLabel* m_indentation{nullptr};
    QLabel* m_lineEnding{nullptr};
    QToolButton* m_encoding{nullptr};
    QToolButton* m_wordWrap{nullptr};
    QVector<ResolvedLanguageServer> m_availableLanguageServers;
    QHash<QString, LanguageServerClient*> m_languageServers;
    bool m_wordWrapEnabled{false};
    CodeEditorFont m_font;
    CodeColorScheme m_colorScheme;
};

} // namespace workpane::plugins::codeeditor
