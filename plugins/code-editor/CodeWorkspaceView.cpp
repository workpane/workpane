#include "CodeWorkspaceView.h"
#include "FileSystemFailure.h"

#include "FileFinder.h"
#include "FileWatch.h"
#include "WorkspaceSearch.h"
#include "ui/Icons.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"

#include <QAction>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <functional>
#include <utility>

namespace workpane::plugins::codeeditor {

// A folder reached through a symbolic link can name the folder that holds it, so the walk carries the depth this project declares rather than the one the tree keeps offering.
constexpr int maximumTreeDepth = 64;

class CodeWorkspaceViewHelper final {
  public:
    static QString charsetName(TextCharset charset);
    static bool validEntryName(const QString& name);
};

QString CodeWorkspaceViewHelper::charsetName(TextCharset charset) {
    switch (charset) {
    case TextCharset::Utf8:
        return QStringLiteral("UTF-8");
    case TextCharset::Utf8Bom:
        return QStringLiteral("UTF-8 BOM");
    case TextCharset::Utf16Le:
        return QStringLiteral("UTF-16 LE");
    case TextCharset::Utf16Be:
        return QStringLiteral("UTF-16 BE");
    case TextCharset::Latin1:
        return QStringLiteral("Latin-1");
    }

    return QStringLiteral("UTF-8");
}

bool CodeWorkspaceViewHelper::validEntryName(const QString& name) {
    return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..") && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) && !name.contains(QChar::Null);
}

CodeWorkspaceView::CodeWorkspaceView(CodeWorkspaceState state, QVector<ResolvedLanguageServer> languageServers, bool wordWrap, CodeEditorFont font, CodeColorScheme scheme, TextCharset defaultCharset, PluginHost& host, QWidget* parent) : QWidget(parent), m_initialState(std::move(state)), m_defaultCharset(defaultCharset), m_host(host), m_fileModel(new QFileSystemModel(this)), m_tree(new QTreeView(this)), m_documents(new ui::TabWidget(host.theme(), this)), m_problems(new QTreeWidget(this)), m_references(new QTreeWidget(this)), m_searchQuery(new QLineEdit(this)), m_searchResults(new QTreeWidget(this)), m_symbols(new QTreeWidget(this)), m_symbolSearch(new QLineEdit(this)), m_bottomPanel(new ui::TabWidget(host.theme(), this)), m_availableLanguageServers(std::move(languageServers)), m_wordWrapEnabled(wordWrap), m_font(std::move(font)), m_colorScheme(std::move(scheme)) {
    // Containment compares canonical paths, so the root it compares against is resolved here rather than trusted from whoever opened the workspace.
    if (const QString canonicalRoot = QFileInfo(m_initialState.rootPath).canonicalFilePath(); !canonicalRoot.isEmpty()) {
        m_initialState.rootPath = canonicalRoot;
    }

    m_fileModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fileModel->setReadOnly(true);
    m_fileModel->setRootPath(m_initialState.rootPath);
    m_tree->setModel(m_fileModel);
    m_tree->setRootIndex(m_fileModel->index(m_initialState.rootPath));
    m_tree->setHeaderHidden(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    for (int column = 1; column < m_fileModel->columnCount(); ++column) {
        m_tree->hideColumn(column);
    }

    m_documents->setObjectName(QStringLiteral("codeEditorDocuments"));
    m_bottomPanel->setObjectName(QStringLiteral("codeEditorBottomPanel"));
    m_documents->setTabsClosable(true);
    m_documents->setMovable(true);
    m_problems->setHeaderLabels({m_host.translate(QStringLiteral("code-editor.problems.file")), m_host.translate(QStringLiteral("code-editor.problems.line")), m_host.translate(QStringLiteral("code-editor.problems.code")), m_host.translate(QStringLiteral("code-editor.problems.source")), m_host.translate(QStringLiteral("code-editor.problems.message"))});
    m_problems->setObjectName(QStringLiteral("codeEditorProblems"));
    m_problemFilter = new QLineEdit(this);
    m_problemFilter->setObjectName(QStringLiteral("codeEditorProblemFilter"));
    m_problemFilter->setPlaceholderText(m_host.translate(QStringLiteral("code-editor.problems.filter")));
    m_problemsPage = new QWidget(this);
    auto* problemsLayout = new QVBoxLayout(m_problemsPage);
    problemsLayout->setContentsMargins(0, 0, 0, 0);
    problemsLayout->setSpacing(0);
    problemsLayout->addWidget(m_problemFilter);
    problemsLayout->addWidget(m_problems, 1);
    // clang-format off
    connect(m_problemFilter, &QLineEdit::textChanged, this, [this]() { refreshProblems(); });
    // clang-format on
    m_problems->setRootIsDecorated(false);
    m_problems->setAlternatingRowColors(true);
    m_problems->setFrameShape(QFrame::NoFrame);
    m_tree->setObjectName(QStringLiteral("codeEditorTree"));
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setHeaderHidden(true);
    m_tree->setAnimated(false);

    m_references->setHeaderLabels({m_host.translate(QStringLiteral("code-editor.problems.file")), m_host.translate(QStringLiteral("code-editor.problems.line")), m_host.translate(QStringLiteral("code-editor.references.context"))});
    m_references->setObjectName(QStringLiteral("codeEditorReferences"));
    m_references->setRootIsDecorated(false);
    m_references->setAlternatingRowColors(true);
    m_references->setFrameShape(QFrame::NoFrame);
    m_searchQuery->setObjectName(QStringLiteral("codeEditorSearchQuery"));
    m_searchQuery->setPlaceholderText(m_host.translate(QStringLiteral("code-editor.search.placeholder")));
    m_searchQuery->setClearButtonEnabled(true);
    m_searchResults->setObjectName(QStringLiteral("codeEditorSearchResults"));
    m_searchResults->setHeaderLabels({m_host.translate(QStringLiteral("code-editor.problems.file")), m_host.translate(QStringLiteral("code-editor.problems.line")), m_host.translate(QStringLiteral("code-editor.search.line-text"))});
    m_searchResults->setRootIsDecorated(false);
    m_searchResults->setAlternatingRowColors(true);
    m_searchResults->setFrameShape(QFrame::NoFrame);
    m_searchPage = new QWidget(this);
    auto* searchLayout = new QVBoxLayout(m_searchPage);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(0);
    searchLayout->addWidget(m_searchQuery);
    searchLayout->addWidget(m_searchResults, 1);
    // clang-format off
    connect(m_searchQuery, &QLineEdit::returnPressed, this, [this]() { refreshWorkspaceSearch(); });
    connect(m_searchResults, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { openLocation(item->data(0, Qt::UserRole).toString(), item->data(1, Qt::UserRole).toInt(), 0); });
    // clang-format on

    m_bottomPanel->addTab(m_problemsPage, m_host.translate(QStringLiteral("code-editor.problems.title")));
    m_bottomPanel->addTab(m_references, m_host.translate(QStringLiteral("code-editor.references.title")));
    m_bottomPanel->addTab(m_searchPage, m_host.translate(QStringLiteral("code-editor.search.title")));
    m_bottomPanel->setMinimumHeight(LanguageRegistry::limits().bottomPanelMinimumHeight);

    m_symbols->setObjectName(QStringLiteral("codeEditorSymbols"));
    m_symbols->setHeaderHidden(true);
    m_symbols->setFrameShape(QFrame::NoFrame);
    m_symbols->setAnimated(false);
    m_symbolSearch->setObjectName(QStringLiteral("codeEditorSymbolSearch"));
    m_symbolSearch->setPlaceholderText(m_host.translate(QStringLiteral("code-editor.symbols.search")));
    m_symbolSearch->setClearButtonEnabled(true);
    auto* symbolPanel = new QWidget(this);
    symbolPanel->setObjectName(QStringLiteral("codeEditorSymbolPanel"));
    symbolPanel->setAttribute(Qt::WA_StyledBackground);
    auto* symbolLayout = new QVBoxLayout(symbolPanel);
    symbolLayout->setContentsMargins(6, 6, 6, 0);
    symbolLayout->setSpacing(6);
    symbolLayout->addWidget(m_symbolSearch);
    symbolLayout->addWidget(m_symbols, 1);

    auto* editorArea = new QSplitter(Qt::Vertical, this);
    editorArea->setObjectName(QStringLiteral("codeEditorSplitter"));
    editorArea->setHandleWidth(1);
    editorArea->setChildrenCollapsible(false);
    editorArea->addWidget(m_documents);
    editorArea->addWidget(m_bottomPanel);
    m_bottomPanel->setVisible(!m_availableLanguageServers.isEmpty());
    editorArea->setStretchFactor(0, 1);
    editorArea->setStretchFactor(1, 0);
    // The panel below the editor is sized by the splitter, so it opens tall enough to read a few rows and grows to wherever it is dragged.
    editorArea->setSizes({LanguageRegistry::limits().bottomPanelInitialHeight * 3, LanguageRegistry::limits().bottomPanelInitialHeight});
    auto* sidebar = new QSplitter(Qt::Vertical, this);
    sidebar->setObjectName(QStringLiteral("codeEditorSplitter"));
    sidebar->setHandleWidth(1);
    sidebar->setChildrenCollapsible(false);
    auto* treePanel = new QWidget(this);
    auto* treeLayout = new QVBoxLayout(treePanel);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(0);
    auto* filterArea = new QWidget(treePanel);
    auto* filterLayout = new QVBoxLayout(filterArea);
    filterLayout->setContentsMargins(9, 9, 9, 9);
    filterLayout->setSpacing(0);
    m_fileFilter = new ui::FilterField(m_host.translate(QStringLiteral("code-editor.tree.filter")), m_host.translate(QStringLiteral("code-editor.tree.filter-placeholder")), filterArea);
    m_fileFilter->setObjectName(QStringLiteral("codeEditorFileFilter"));
    filterLayout->addWidget(m_fileFilter);
    treeLayout->addWidget(filterArea);
    treeLayout->addWidget(ui::Components::horizontalDivider(treePanel));
    treeLayout->addWidget(m_tree, 1);
    sidebar->addWidget(treePanel);
    sidebar->addWidget(symbolPanel);
    symbolPanel->setVisible(!m_availableLanguageServers.isEmpty());
    sidebar->setStretchFactor(0, 1);
    auto* splitter = new QSplitter(this);
    splitter->setObjectName(QStringLiteral("codeEditorSplitter"));
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(sidebar);
    splitter->addWidget(editorArea);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 900});

    auto* status = new QWidget(this);
    status->setObjectName(QStringLiteral("codeEditorStatusBar"));
    status->setFixedHeight(m_host.theme().metric(ui::ThemeMetric::StatusBarHeight));
    auto* statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(12, 0, 8, 0);
    statusLayout->setSpacing(16);
    m_cursorLocation = new QLabel(status);
    m_cursorLocation->setObjectName(QStringLiteral("codeEditorCursorLocation"));
    m_indentation = new QLabel(status);
    m_indentation->setObjectName(QStringLiteral("mutedLabel"));
    m_lineEnding = new QLabel(status);
    m_lineEnding->setObjectName(QStringLiteral("mutedLabel"));
    // The encoding is the control that changes it, as it is in every editor that shows one.
    m_encoding = new QToolButton(status);
    m_encoding->setObjectName(QStringLiteral("codeEditorEncoding"));
    m_encoding->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_encoding->setCursor(Qt::PointingHandCursor);
    m_encoding->setToolTip(m_host.translate(QStringLiteral("code-editor.status.encoding-action")));
    m_wordWrap = new QToolButton(status);
    m_wordWrap->setObjectName(QStringLiteral("codeEditorWordWrap"));
    m_wordWrap->setCheckable(true);
    m_wordWrap->setChecked(m_wordWrapEnabled);
    m_wordWrap->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_wordWrap->setText(m_host.translate(QStringLiteral("code-editor.status.word-wrap")));
    statusLayout->addWidget(m_cursorLocation);
    statusLayout->addWidget(m_indentation);
    statusLayout->addWidget(m_lineEnding);
    statusLayout->addWidget(m_encoding);
    m_analysis = new QLabel(status);
    m_analysis->setObjectName(QStringLiteral("mutedLabel"));
    statusLayout->addWidget(m_analysis);
    connect(m_encoding, &QToolButton::clicked, this, &CodeWorkspaceView::showEncodingMenu);
    statusLayout->addStretch();
    statusLayout->addWidget(m_wordWrap);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter, 1);
    layout->addWidget(status);

    auto* closeDocumentAction = new QAction(this);
    closeDocumentAction->setObjectName(QStringLiteral("codeEditorCloseDocument"));
    closeDocumentAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Close));
    closeDocumentAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    auto* findFileAction = new QAction(m_host.translate(QStringLiteral("code-editor.finder.title")), this);
    findFileAction->setObjectName(QStringLiteral("codeEditorFindFileAction"));
    findFileAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_P));
    findFileAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(findFileAction);
    connect(findFileAction, &QAction::triggered, this, &CodeWorkspaceView::showFileFinder);
    addAction(closeDocumentAction);

    // clang-format off
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) { const QString path = m_fileModel->filePath(index); if (QFileInfo(path).isFile()) { openFile(path); } });
    connect(m_fileFilter, &ui::FilterField::filterChanged, this, [this](const QString& text) { m_fileNeedle = text; applyFileFilter(); if (!text.isEmpty()) { m_tree->expandAll(); } });
    connect(m_fileModel, &QFileSystemModel::directoryLoaded, this, [this](const QString& loaded) { narrowLoadedTree(loaded); });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, [this](const QPoint& position) { showContextMenu(position); });
    connect(m_documents, &QTabWidget::tabCloseRequested, this, [this](int index) { closeDocument(index); });
    connect(m_documents, &QTabWidget::currentChanged, this, [this](int) { updateStatusBar(); refreshSymbolPanel(); emit stateChanged(); });
    connect(closeDocumentAction, &QAction::triggered, this, [this]() { closeCurrentDocument(); });
    connect(m_wordWrap, &QToolButton::toggled, this, [this](bool enabled) { if (enabled != m_wordWrapEnabled) { m_wordWrapEnabled = enabled; setWordWrap(enabled); emit wordWrapToggled(); } });
    connect(m_documents->tabBar(), &QTabBar::tabMoved, this, [this](int, int) { emit stateChanged(); });
    // clang-format off
    connect(m_documents->tabBar(), &QTabBar::tabBarDoubleClicked, this, [this](int index) { revealInTree(index); });
    connect(m_problems, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { openLocation(item->data(0, Qt::UserRole).toString(), item->data(1, Qt::UserRole).toInt(), 0); });
    connect(m_references, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { openLocation(item->data(0, Qt::UserRole).toString(), item->data(1, Qt::UserRole).toInt(), item->data(2, Qt::UserRole).toInt()); });
    connect(m_symbols, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { openLocation(item->data(0, Qt::UserRole).toString(), item->data(1, Qt::UserRole).toInt(), item->data(2, Qt::UserRole).toInt()); });
    connect(m_symbolSearch, &QLineEdit::textChanged, this, [this](const QString& query) { searchSymbols(query); });
    connect(m_fileModel, &QFileSystemModel::rowsInserted, this, [this](const QModelIndex& parentIndex, int first, int last) { QStringList paths; for (int row = first; row <= last; ++row) { paths.append(m_fileModel->filePath(m_fileModel->index(row, 0, parentIndex))); } notifyWatchedChange(paths, 1); });
    connect(m_fileModel, &QFileSystemModel::rowsAboutToBeRemoved, this, [this](const QModelIndex& parentIndex, int first, int last) { QStringList paths; for (int row = first; row <= last; ++row) { paths.append(m_fileModel->filePath(m_fileModel->index(row, 0, parentIndex))); } notifyWatchedChange(paths, 3); });
    connect(m_fileModel, &QFileSystemModel::fileRenamed, this, [this](const QString& directory, const QString& oldName, const QString& newName) { notifyWatchedChange({QDir(directory).filePath(oldName)}, 3); notifyWatchedChange({QDir(directory).filePath(newName)}, 1); });
    connect(&m_editorConfigWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& changed) { FileWatch::rearm(m_editorConfigWatcher, changed); reloadEditorConfigs(); });
    connect(&m_editorConfigWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& changed) { FileWatch::rearm(m_editorConfigWatcher, changed); reloadEditorConfigs(); });
    // clang-format on

    if (!QFileInfo(m_initialState.rootPath).isDir()) {
        // clang-format off
        QTimer::singleShot(0, this, [this]() { emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.workspace-unavailable")) + QStringLiteral("\n") + m_initialState.rootPath); });
        // clang-format on
    }
    // clang-format on

    for (const auto& documentState : m_initialState.documents) {
        if (QFileInfo(documentState.path).isFile() && containsPath(documentState.path)) {
            openFile(documentState.path, documentState.cursorPosition);
        }
    }

    for (int index = 0; index < m_initialState.documents.size(); ++index) {
        if (m_initialState.documents.at(index).active && index < m_documents->count()) {
            m_documents->setCurrentIndex(index);
        }
    }

    updateStatusBar();
}

CodeWorkspaceView::~CodeWorkspaceView() = default;

const QString& CodeWorkspaceView::workspaceId() const {
    return m_initialState.id;
}

const QString& CodeWorkspaceView::rootPath() const {
    return m_initialState.rootPath;
}

QString CodeWorkspaceView::title() const {
    return QFileInfo(m_initialState.rootPath).fileName();
}

CodeWorkspaceState CodeWorkspaceView::state() const {
    CodeWorkspaceState current = m_initialState;
    current.documents.clear();
    current.updatedAtUtc = QDateTime::currentDateTimeUtc();

    int position = 0;

    for (auto* openDocument : documents()) {
        current.documents.append({openDocument->path(), position, openDocument->cursorPosition(), openDocument == currentDocument()});
        ++position;
    }

    return current;
}

bool CodeWorkspaceView::canClose() {
    for (auto* openDocument : documents()) {
        if (openDocument->dirty() && !m_host.confirm(this, m_host.translate(QStringLiteral("code-editor.close.title")), m_host.translate(QStringLiteral("code-editor.close.workspace-message")), openDocument->path(), m_host.translate(QStringLiteral("code-editor.close.discard")), true)) {
            return false;
        }
    }

    return true;
}

void CodeWorkspaceView::openFile(const QString& path, int cursorPosition) {
    const QString normalized = containedPath(path);

    if (normalized.isEmpty() || !QFileInfo(normalized).isFile()) {
        emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.path-outside")));
        return;
    }

    if (auto* existing = document(normalized); existing != nullptr) {
        m_documents->setCurrentWidget(existing);
        return;
    }

    auto* openDocument = new CodeDocument(normalized, m_initialState.rootPath, m_wordWrapEnabled, m_font, m_colorScheme, m_defaultCharset, m_host, m_documents);
    const int index = m_documents->addTab(openDocument, openDocument->title());
    m_documents->setTabToolTip(index, normalized);
    m_documents->setCurrentIndex(index);
    // clang-format off
    connect(openDocument, &CodeDocument::titleChanged, this, [this, openDocument]() { updateDocumentTitle(openDocument); });
    connect(openDocument, &CodeDocument::stateChanged, this, [this]() { updateStatusBar(); emit stateChanged(); });
    connect(openDocument, &CodeDocument::editorConfigChanged, this, [this]() { updateStatusBar(); });
    connect(openDocument, &CodeDocument::loaded, this, [cursorPosition](CodeDocument* loadedDocument) { loadedDocument->setCursorPosition(cursorPosition); }, Qt::SingleShotConnection);
    connect(openDocument, &CodeDocument::loaded, this, [this](CodeDocument* loadedDocument) { loadedDocument->setLanguageServer(languageServer(loadedDocument->language())); });
    connect(openDocument, &CodeDocument::operationFailed, this, [this](const QString& message) { emit operationFailed(message); });
    connect(openDocument, &CodeDocument::externalChangeConflict, this, [this](const QString& changedPath) { emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.external-conflict")) + QStringLiteral("\n") + changedPath); });
    connect(openDocument, &CodeDocument::externalFileRemoved, this, [this, openDocument](const QString& removedPath) { if (openDocument->dirty()) { emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.external-removed")) + QStringLiteral("\n") + removedPath); } else { closeDocument(m_documents->indexOf(openDocument)); } });
    connect(openDocument, &CodeDocument::referencesReady, this, [this](const QString&, const QVector<SourceLocation>& locations) { showReferences(locations); });
    connect(openDocument, &CodeDocument::callHierarchyReady, this, [this](const QString&, CallDirection direction, const QVector<WorkspaceSymbolEntry>& entries) { showCallHierarchy(direction, entries); });
    connect(openDocument, &CodeDocument::outlineChanged, this, [this](const QString& symbolPath, const QVector<DocumentSymbolNode>& symbols) { updateOutline(symbolPath, symbols); });
    connect(openDocument, &CodeDocument::analysisProgress, this, [this](const QString& message, bool active) { m_analysis->setText(active ? message : QString{}); });
    connect(openDocument, &CodeDocument::navigationRequested, this, [this](const QString& target, int line, int character) { openLocation(target, line, character); });
    // clang-format on
    refreshEditorConfigWatch();
    emit stateChanged();
}

// A definition or a diagnostic can point at a file that is not open yet, so the location decides which document carries the cursor.
void CodeWorkspaceView::openLocation(const QString& path, int line, int character) {
    const QString normalized = containedPath(path);

    if (normalized.isEmpty() || !QFileInfo(normalized).isFile()) {
        emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.path-outside")));
        return;
    }

    if (auto* target = document(normalized); target != nullptr) {
        m_documents->setCurrentWidget(target);
        target->setCursorLocation(line, character);
        return;
    }

    openFile(normalized);
    auto* opened = document(normalized);

    if (opened == nullptr) {
        return;
    }
    // clang-format off
    connect(opened, &CodeDocument::loaded, this, [line, character](CodeDocument* loadedDocument) { loadedDocument->setCursorLocation(line, character); }, Qt::SingleShotConnection);
    // clang-format on
}

// The outline belongs to the document being read, and the search results belong to the whole workspace.
// The outline is answered again on every analysis, so rebuilding it is worth doing only when it actually says something different.
void CodeWorkspaceView::updateOutline(const QString& path, const QVector<DocumentSymbolNode>& symbols) {
    auto* current = currentDocument();

    if (current == nullptr || current->path() != path || !m_symbolSearch->text().isEmpty()) {
        return;
    }
    if (m_outlinePath == path && m_outline == symbols) {
        return;
    }

    m_outlinePath = path;
    m_outline = symbols;
    m_symbols->clear();
    // clang-format off
    const std::function<void(QTreeWidgetItem*, const QVector<DocumentSymbolNode>&)> appendNodes = [&](QTreeWidgetItem* parent, const QVector<DocumentSymbolNode>& nodes) {
        for (const auto& node : nodes) {
            auto* item = new QTreeWidgetItem(QStringList{node.detail.isEmpty() ? node.name : node.name + QStringLiteral("  ") + node.detail});
            item->setData(0, Qt::UserRole, path);
            item->setData(1, Qt::UserRole, node.line);
            item->setData(2, Qt::UserRole, node.character);
            if (parent == nullptr) { m_symbols->addTopLevelItem(item); } else { parent->addChild(item); }
            appendNodes(item, node.children);
        }
    };
    // clang-format on
    appendNodes(nullptr, symbols);
    m_symbols->expandAll();
}

// A symbol used everywhere would answer with more rows than anyone reads, so the list is bounded and the tab says how many arrived.
void CodeWorkspaceView::showReferences(const QVector<SourceLocation>& locations) {
    m_references->clear();

    for (const auto& location : locations.mid(0, LanguageRegistry::limits().maximumReferences)) {
        auto* item = new QTreeWidgetItem({QFileInfo(location.path).fileName(), QString::number(location.line + 1), QDir(m_initialState.rootPath).relativeFilePath(location.path)});
        item->setData(0, Qt::UserRole, location.path);
        item->setData(1, Qt::UserRole, location.line);
        item->setData(2, Qt::UserRole, location.character);
        m_references->addTopLevelItem(item);
    }

    const QString shown = QString::number(m_references->topLevelItemCount()) + (locations.size() > LanguageRegistry::limits().maximumReferences ? QStringLiteral("+") : QString{});
    m_bottomPanel->setTabText(m_bottomPanel->indexOf(m_references), m_host.translate(QStringLiteral("code-editor.references.title")) + QStringLiteral(" (") + shown + QStringLiteral(")"));
    m_bottomPanel->setCurrentWidget(m_references);
}

// A list of calls is a list of places, so it fills the surface the references already use and the title says which list it is.
void CodeWorkspaceView::showCallHierarchy(CallDirection direction, const QVector<WorkspaceSymbolEntry>& entries) {
    m_references->clear();

    for (const auto& entry : entries.mid(0, LanguageRegistry::limits().maximumReferences)) {
        auto* item = new QTreeWidgetItem({QFileInfo(entry.location.path).fileName(), QString::number(entry.location.line + 1), entry.container.isEmpty() ? entry.name : entry.name + QStringLiteral("   ") + entry.container});
        item->setData(0, Qt::UserRole, entry.location.path);
        item->setData(1, Qt::UserRole, entry.location.line);
        item->setData(2, Qt::UserRole, entry.location.character);
        m_references->addTopLevelItem(item);
    }

    const QString key = direction == CallDirection::Incoming ? QStringLiteral("code-editor.calls.incoming") : QStringLiteral("code-editor.calls.outgoing");
    m_bottomPanel->setTabText(m_bottomPanel->indexOf(m_references), m_host.translate(key) + QStringLiteral(" (") + QString::number(m_references->topLevelItemCount()) + QStringLiteral(")"));
    m_bottomPanel->setCurrentWidget(m_references);
}

void CodeWorkspaceView::showWorkspaceSymbols(const QVector<WorkspaceSymbolEntry>& symbols) {
    if (m_symbolSearch->text().isEmpty()) {
        return;
    }

    // The panel now shows the workspace, so the outline it held is no longer what is on screen.
    m_outlinePath.clear();
    m_outline.clear();
    m_symbols->clear();

    for (const auto& symbol : symbols) {
        auto* item = new QTreeWidgetItem(QStringList{symbol.container.isEmpty() ? symbol.name : symbol.container + QStringLiteral("::") + symbol.name});
        item->setData(0, Qt::UserRole, symbol.location.path);
        item->setData(1, Qt::UserRole, symbol.location.line);
        item->setData(2, Qt::UserRole, symbol.location.character);
        m_symbols->addTopLevelItem(item);
    }
}

void CodeWorkspaceView::searchSymbols(const QString& query) {
    if (query.isEmpty()) {
        m_symbols->clear();
        refreshSymbolPanel();
        return;
    }

    for (auto* server : m_languageServers) {
        server->requestWorkspaceSymbols(query);
    }
}

void CodeWorkspaceView::refreshSymbolPanel() {
    auto* current = currentDocument();

    if (current == nullptr) {
        m_outlinePath.clear();
        m_outline.clear();
        m_symbols->clear();
        return;
    }

    current->refreshAnalysis();
}

// A rule file that appears, changes or disappears decides how the open documents are indented from that moment on.
void CodeWorkspaceView::refreshEditorConfigWatch() {
    QStringList directories;

    for (auto* openDocument : documents()) {
        QDir directory = QFileInfo(openDocument->path()).dir();
        while (containsPath(directory.absolutePath()) || directory.absolutePath() == m_initialState.rootPath) {
            const QString absolute = directory.absolutePath();
            if (!directories.contains(absolute)) {
                directories.append(absolute);
            }
            if (absolute == m_initialState.rootPath || !directory.cdUp()) {
                break;
            }
        }
    }

    QStringList files;

    for (const auto& directory : directories) {
        const QString candidate = QDir(directory).filePath(QStringLiteral(".editorconfig"));
        if (QFileInfo::exists(candidate)) {
            files.append(candidate);
        }
    }

    // Qt drops a watched path it saw disappear, so the workspace keeps its own list of what it asked to watch.
    if (!m_watchedEditorConfigPaths.isEmpty()) {
        m_editorConfigWatcher.removePaths(m_watchedEditorConfigPaths);
    }

    m_watchedEditorConfigPaths = directories + files;

    if (!m_watchedEditorConfigPaths.isEmpty()) {
        m_editorConfigWatcher.addPaths(m_watchedEditorConfigPaths);
    }
}

QStringList CodeWorkspaceView::watchedEditorConfigPaths() const {
    return m_watchedEditorConfigPaths;
}

void CodeWorkspaceView::reloadEditorConfigs() {
    for (auto* openDocument : documents()) {
        openDocument->reloadEditorConfig();
    }

    refreshEditorConfigWatch();
    updateStatusBar();
}

// A file the analysis depends on can change without ever being opened, so every running server hears about the tree it is indexing.
void CodeWorkspaceView::notifyWatchedChange(const QStringList& paths, int changeType) {
    QStringList contained;

    for (const auto& path : paths) {
        if (containsPath(path)) {
            contained.append(path);
        }
    }

    for (auto* server : m_languageServers) {
        server->notifyWatchedFilesChanged(contained, changeType);
    }
}

void CodeWorkspaceView::connectSymbolSources(LanguageServerClient* server) {
    // clang-format off
    connect(server, &LanguageServerClient::workspaceSymbolsReady, this, [this](const QVector<WorkspaceSymbolEntry>& symbols) { showWorkspaceSymbols(symbols); });
    // A server reports on every file it analyses, including ones nobody opened, so the surface listens to the server itself.
    connect(server, &LanguageServerClient::diagnosticsPublished, this, [this, languageId = server->configuration().languageId](const QString& path, const QVector<LanguageDiagnostic>& diagnostics) { updateDiagnostics(languageId, path, diagnostics); });
    // clang-format on
}

void CodeWorkspaceView::saveAll() {
    for (auto* openDocument : documents()) {
        openDocument->save();
    }
}

void CodeWorkspaceView::setLanguageServers(QVector<ResolvedLanguageServer> languageServers) {
    m_availableLanguageServers = std::move(languageServers);
    QVector<LanguageServerClient*> removedServers;

    for (auto server = m_languageServers.begin(); server != m_languageServers.end();) {
        bool available = false;
        for (const auto& candidate : m_availableLanguageServers) {
            available = available || (candidate.languageId == server.key() && candidate.executablePath == server.value()->configuration().executablePath && candidate.arguments == server.value()->configuration().arguments);
        }
        if (available) {
            ++server;
            continue;
        }
        removedServers.append(server.value());
        server = m_languageServers.erase(server);
    }

    for (auto* openDocument : documents()) {
        openDocument->setLanguageServer(languageServer(openDocument->language()));
    }

    for (auto* server : removedServers) {
        connect(server, &LanguageServerClient::stopped, server, &QObject::deleteLater);
        server->stop();
    }

    if (m_availableLanguageServers.isEmpty()) {
        m_problems->clear();
        m_references->clear();
        m_symbols->clear();
        m_analysis->clear();
    }

    m_bottomPanel->setVisible(!m_availableLanguageServers.isEmpty());

    if (auto* panel = m_symbolSearch->parentWidget(); panel != nullptr) {
        panel->setVisible(!m_availableLanguageServers.isEmpty());
    }
}

void CodeWorkspaceView::setEditorFont(const CodeEditorFont& font) {
    m_font = font;

    for (auto* openDocument : documents()) {
        openDocument->setEditorFont(font);
    }
}

void CodeWorkspaceView::setColorScheme(const CodeColorScheme& scheme) {
    m_colorScheme = scheme;

    for (auto* openDocument : documents()) {
        openDocument->setColorScheme(scheme);
    }
}

void CodeWorkspaceView::setWordWrap(bool enabled) {
    m_wordWrapEnabled = enabled;
    m_wordWrap->setChecked(enabled);

    for (auto* openDocument : documents()) {
        openDocument->setWordWrap(enabled);
    }
}

bool CodeWorkspaceView::wordWrapEnabled() const {
    return m_wordWrapEnabled;
}

void CodeWorkspaceView::closeCurrentDocument() {
    if (m_documents->currentIndex() >= 0) {
        closeDocument(m_documents->currentIndex());
    }
}

void CodeWorkspaceView::updateStatusBar() {
    auto* current = currentDocument();

    if (current == nullptr) {
        m_cursorLocation->clear();
        m_indentation->clear();
        m_lineEnding->clear();
        m_encoding->setText(QString{});
        m_encoding->setEnabled(false);
        return;
    }

    const auto& configuration = current->editorConfig();
    m_cursorLocation->setText(m_host.translate(QStringLiteral("code-editor.status.cursor")).arg(current->cursorLine()).arg(current->cursorColumn()));
    m_indentation->setText(m_host.translate(configuration.indentStyle.value_or(IndentStyle::Space) == IndentStyle::Tab ? QStringLiteral("code-editor.status.tab-size") : QStringLiteral("code-editor.status.space-size")).arg(EditorConfigs::resolvedIndentWidth(configuration)));
    m_lineEnding->setText(current->lineEnding() == LineEnding::Crlf ? QStringLiteral("CRLF") : current->lineEnding() == LineEnding::Cr ? QStringLiteral("CR") : QStringLiteral("LF"));
    m_encoding->setText(CodeWorkspaceViewHelper::charsetName(current->charset()));
    m_encoding->setEnabled(true);
}

void CodeWorkspaceView::showContextMenu(const QPoint& position) {
    const QString path = selectedPath();
    const QString directory = targetDirectory(path);
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* newFile = menu->addAction(ui::IconCatalog::icon(ui::IconName::Add, m_host.theme()), m_host.translate(QStringLiteral("code-editor.actions.new-file")));
    auto* newDirectory = menu->addAction(ui::IconCatalog::icon(ui::IconName::Folder, m_host.theme()), m_host.translate(QStringLiteral("code-editor.actions.new-folder")));
    auto* rename = menu->addAction(m_host.translate(QStringLiteral("code-editor.actions.rename")));
    auto* move = menu->addAction(m_host.translate(QStringLiteral("code-editor.actions.move")));
    auto* remove = menu->addAction(ui::IconCatalog::icon(ui::IconName::Clear, m_host.theme()), m_host.translate(QStringLiteral("code-editor.actions.delete")));
    const bool mutableSelection = !path.isEmpty() && path != m_initialState.rootPath;
    rename->setEnabled(mutableSelection);
    move->setEnabled(mutableSelection);
    remove->setEnabled(mutableSelection);
    // clang-format off
    connect(newFile, &QAction::triggered, this, [this, directory]() { createFile(directory); });
    connect(newDirectory, &QAction::triggered, this, [this, directory]() { createDirectory(directory); });
    connect(rename, &QAction::triggered, this, [this, path]() { renamePath(path); });
    connect(move, &QAction::triggered, this, [this, path]() { movePath(path); });
    connect(remove, &QAction::triggered, this, [this, path]() { removePath(path); });
    // clang-format on
    menu->popup(m_tree->viewport()->mapToGlobal(position));
}

QString CodeWorkspaceView::promptedEntryPath(const QString& titleKey, const QString& directory) {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, m_host.translate(titleKey), m_host.translate(QStringLiteral("code-editor.actions.name")), QLineEdit::Normal, {}, &accepted).trimmed();

    if (!accepted || !CodeWorkspaceViewHelper::validEntryName(name)) {
        return {};
    }

    const QString path = containedPath(QDir(directory).filePath(name));

    if (path.isEmpty()) {
        emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.path-outside")));
    }

    return path;
}

void CodeWorkspaceView::createFile(const QString& directory) {
    const QString path = promptedEntryPath(QStringLiteral("code-editor.actions.new-file"), directory);

    if (path.isEmpty()) {
        return;
    }

    auto future = m_host.createFile(path);
    // clang-format off
    future.then(this, [this, path](Result<void> result) { reportResult(result); if (result.hasValue()) { openFile(path); } });
    // clang-format on
}

void CodeWorkspaceView::createDirectory(const QString& directory) {
    const QString path = promptedEntryPath(QStringLiteral("code-editor.actions.new-folder"), directory);

    if (path.isEmpty()) {
        return;
    }

    auto future = m_host.createDirectory(path);
    // clang-format off
    future.then(this, [this](Result<void> result) { reportResult(result); });
    // clang-format on
}

void CodeWorkspaceView::renamePath(const QString& path) {
    bool accepted = false;
    const QFileInfo source(path);
    const QString name = QInputDialog::getText(this, m_host.translate(QStringLiteral("code-editor.actions.rename")), m_host.translate(QStringLiteral("code-editor.actions.name")), QLineEdit::Normal, source.fileName(), &accepted);

    if (!accepted || name == source.fileName()) {
        return;
    }

    renameEntry(path, name);
}

// The destination is the resolved path the move really uses, so the open document follows exactly what reached the disk.
void CodeWorkspaceView::renameEntry(const QString& path, const QString& name) {
    if (!CodeWorkspaceViewHelper::validEntryName(name)) {
        emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.operation")));
        return;
    }

    const QString source = containedPath(path);
    const QString destination = containedPath(QDir(QFileInfo(path).absolutePath()).filePath(name));

    if (source.isEmpty() || destination.isEmpty()) {
        emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.path-outside")));
        return;
    }

    auto future = m_host.movePath(source, destination);
    // clang-format off
    future.then(this, [this, source, destination](Result<void> result) { reportResult(result); if (result.hasValue()) { updateDocumentPaths(source, destination); } });
    // clang-format on
}

void CodeWorkspaceView::movePath(const QString& path) {
    const QString directory = QFileDialog::getExistingDirectory(this, m_host.translate(QStringLiteral("code-editor.actions.move")), m_initialState.rootPath);
    const QString containedDirectory = containedPath(directory);

    if (directory.isEmpty() || containedDirectory.isEmpty()) {
        return;
    }

    const QString source = containedPath(path);
    const QString destination = QDir(containedDirectory).filePath(QFileInfo(path).fileName());
    auto future = m_host.movePath(source, destination);
    // clang-format off
    future.then(this, [this, source, destination](Result<void> result) { reportResult(result); if (result.hasValue()) { updateDocumentPaths(source, destination); } });
    // clang-format on
}

void CodeWorkspaceView::removePath(const QString& path) {
    const QFileInfo information(path);

    if (!m_host.confirm(this, m_host.translate(QStringLiteral("code-editor.delete.title")), m_host.translate(information.isDir() ? QStringLiteral("code-editor.delete.folder-message") : QStringLiteral("code-editor.delete.file-message")), path, m_host.translate(QStringLiteral("code-editor.actions.delete")), true)) {
        return;
    }

    const QString removed = containedPath(path);

    if (removed.isEmpty()) {
        emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.path-outside")));
        return;
    }

    auto future = information.isDir() && !information.isSymLink() ? m_host.removeDirectory(removed) : m_host.removeFile(removed);
    // clang-format off
    future.then(this, [this, removed](Result<void> result) { reportResult(result); if (result.hasValue()) { for (int index = m_documents->count() - 1; index >= 0; --index) { auto* openDocument = documentAt(index); if (openDocument->path() == removed || openDocument->path().startsWith(removed + QLatin1Char('/'))) { m_documents->removeTab(index); openDocument->deleteLater(); } } emit stateChanged(); } });
    // clang-format on
}

void CodeWorkspaceView::closeDocument(int index) {
    auto* openDocument = documentAt(index);

    if (openDocument == nullptr) {
        return;
    }
    if (openDocument->dirty() && !m_host.confirm(this, m_host.translate(QStringLiteral("code-editor.close.title")), m_host.translate(QStringLiteral("code-editor.close.file-message")), openDocument->path(), m_host.translate(QStringLiteral("code-editor.close.discard")), true)) {
        return;
    }

    const QString closedPath = openDocument->path();
    m_documents->removeTab(index);
    openDocument->deleteLater();
    updateStatusBar();
    refreshSymbolPanel();
    refreshEditorConfigWatch();
    emit stateChanged();
}

// The reader chooses an encoding to read the bytes again in, or one to write them in, which is what every editor offers here.
// The size of a folder is decided by whoever opened it, so it is walked away from the interface and only as far as the bound the catalog declares.
void CodeWorkspaceView::showFileFinder() {
    const QString root = m_initialState.rootPath;
    const int bound = LanguageRegistry::limits().maximumWorkspaceFiles;
    // clang-format off
    auto scan = QtConcurrent::run([root, bound]() {
        QStringList paths;
        QDirIterator entries(root, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
        while (entries.hasNext() && paths.size() < bound) {
            const QString path = entries.next();
            if (!path.contains(QStringLiteral("/.git/"))) {
                paths.append(QDir(root).relativeFilePath(path));
            }
        }
        return paths;
    });
    scan.then(this, [this, root, bound](const QStringList& paths) {
        auto* finder = new FileFinder(root, paths, paths.size() < bound, m_host, this);
        finder->setAttribute(Qt::WA_DeleteOnClose);
        connect(finder, &FileFinder::pathChosen, this, [this](const QString& path) { openFile(path); });
        ui::Components::showDialogWindow(finder, m_host.translate(QStringLiteral("code-editor.finder.title")));
    });
    // clang-format on
}

void CodeWorkspaceView::showEncodingMenu() {
    auto* document = currentDocument();

    if (document == nullptr) {
        return;
    }

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* reopen = menu->addMenu(m_host.translate(QStringLiteral("code-editor.status.reopen-with-encoding")));
    auto* save = menu->addMenu(m_host.translate(QStringLiteral("code-editor.status.save-with-encoding")));

    for (const auto charset : EditorConfigs::textCharsets()) {
        const QString name = CodeWorkspaceViewHelper::charsetName(charset);
        auto* reopenEntry = reopen->addAction(name);
        reopenEntry->setData(QStringLiteral("reopen/") + EditorConfigs::textCharsetName(charset));
        auto* saveEntry = save->addAction(name);
        saveEntry->setData(QStringLiteral("save/") + EditorConfigs::textCharsetName(charset));
    }

    connect(menu, &QMenu::triggered, this, &CodeWorkspaceView::applyEncodingChoice);
    menu->popup(QCursor::pos());
}

// Reading the bytes again replaces what the buffer holds, so a document with unsaved work is asked about first.
void CodeWorkspaceView::applyEncodingChoice(QAction* action) {
    auto* document = currentDocument();

    if (document == nullptr) {
        return;
    }

    const QString choice = action->data().toString();
    const auto charset = EditorConfigs::parseTextCharset(choice.section(QLatin1Char('/'), 1));

    if (!charset.has_value()) {
        return;
    }

    if (choice.startsWith(QStringLiteral("save/"))) {
        document->saveWithCharset(*charset);
        return;
    }

    if (document->dirty() && !m_host.confirm(this, m_host.translate(QStringLiteral("code-editor.plugin.title")), m_host.translate(QStringLiteral("code-editor.status.reopen-title")), m_host.translate(QStringLiteral("code-editor.status.reopen-message")), m_host.translate(QStringLiteral("code-editor.status.reopen-action")), true)) {
        return;
    }

    document->reopenWithCharset(*charset);
}

void CodeWorkspaceView::updateDocumentTitle(CodeDocument* document) {
    const int index = m_documents->indexOf(document);

    if (index >= 0) {
        m_documents->setTabText(index, document->title());
        m_documents->setTabToolTip(index, document->path());
    }
}

// The tree follows the document the reader points at, so a double click on its tab selects the file it belongs to.
void CodeWorkspaceView::applyFileFilter() {
    narrowTree(m_fileModel->index(m_initialState.rootPath), 0);
}

// A folder that finished loading narrows itself and the path that leads to it, because narrowing the whole tree once per folder that arrives costs the tree squared.
void CodeWorkspaceView::narrowLoadedTree(const QString& path) {
    const QModelIndex root = m_fileModel->index(m_initialState.rootPath);
    const QModelIndex loaded = m_fileModel->index(path);
    QModelIndex ancestor = loaded;
    int depth = 0;

    while (ancestor.isValid() && ancestor != root && depth <= maximumTreeDepth) {
        ancestor = ancestor.parent();
        ++depth;
    }

    if (!loaded.isValid() || ancestor != root) {
        return;
    }

    if (loaded == root) {
        narrowTree(root, 0);
        return;
    }

    const bool named = loaded.data(Qt::DisplayRole).toString().contains(m_fileNeedle, Qt::CaseInsensitive);
    const bool leadsToOne = narrowTree(loaded, depth);
    const bool visible = m_fileNeedle.isEmpty() || named || leadsToOne;
    m_tree->setRowHidden(loaded.row(), loaded.parent(), !visible);

    // Loading a folder only ever adds names the filter can match, so the folders above it can only become visible.
    for (QModelIndex entry = loaded.parent(); visible && entry.isValid() && entry != root; entry = entry.parent()) {
        m_tree->setRowHidden(entry.row(), entry.parent(), false);
    }
}

// A folder that leads to a name the filter matched stays, because a match nobody can reach is not a result.
bool CodeWorkspaceView::narrowTree(const QModelIndex& parent, int depth) {
    if (depth >= maximumTreeDepth) {
        return false;
    }

    if (!m_fileNeedle.isEmpty() && m_fileModel->canFetchMore(parent)) {
        m_fileModel->fetchMore(parent);
    }

    bool anyVisible = false;

    for (int row = 0; row < m_fileModel->rowCount(parent); ++row) {
        const QModelIndex entry = m_fileModel->index(row, 0, parent);
        const bool named = entry.data(Qt::DisplayRole).toString().contains(m_fileNeedle, Qt::CaseInsensitive);
        const bool leadsToOne = m_fileModel->isDir(entry) && narrowTree(entry, depth + 1);
        const bool visible = m_fileNeedle.isEmpty() || named || leadsToOne;
        m_tree->setRowHidden(row, parent, !visible);
        anyVisible = anyVisible || visible;
    }

    return anyVisible;
}

void CodeWorkspaceView::revealInTree(int index) {
    auto* openDocument = documentAt(index);

    if (openDocument == nullptr) {
        return;
    }

    const QModelIndex entry = m_fileModel->index(openDocument->path());

    if (!entry.isValid()) {
        return;
    }

    m_tree->setCurrentIndex(entry);
    m_tree->scrollTo(entry, QAbstractItemView::PositionAtCenter);
    m_tree->setFocus();
}

void CodeWorkspaceView::updateDocumentPaths(const QString& source, const QString& destination) {
    for (auto* openDocument : documents()) {
        if (openDocument->path() == source) {
            openDocument->updatePath(destination);
        } else if (openDocument->path().startsWith(source + QLatin1Char('/'))) {
            openDocument->updatePath(destination + openDocument->path().mid(source.size()));
        } else {
            continue;
        }
        openDocument->setLanguageServer(languageServer(openDocument->language()));
    }

    for (int index = 0; index < m_problems->topLevelItemCount(); ++index) {
        auto* item = m_problems->topLevelItem(index);
        const QString path = item->data(0, Qt::UserRole).toString();
        if (path != source && !path.startsWith(source + QLatin1Char('/'))) {
            continue;
        }
        const QString updatedPath = destination + path.mid(source.size());
        item->setText(0, QFileInfo(updatedPath).fileName());
        item->setData(0, Qt::UserRole, updatedPath);
    }

    emit stateChanged();
}

// An empty answer for a file is the server saying it has nothing left to report there, which is how a fixed problem disappears.
void CodeWorkspaceView::updateDiagnostics(const QString& languageId, const QString& path, const QVector<LanguageDiagnostic>& diagnostics) {
    const QString contained = containedPath(path);

    if (contained.isEmpty()) {
        return;
    }

    auto& owned = m_diagnostics[languageId];

    if (diagnostics.isEmpty()) {
        if (!owned.remove(contained)) {
            return;
        }
    } else {
        if (owned.value(contained) == diagnostics) {
            return;
        }
        owned.insert(contained, diagnostics);
    }

    refreshProblems();
}

void CodeWorkspaceView::refreshProblems() {
    QVector<QPair<QString, LanguageDiagnostic>> rows;

    for (const auto& owned : m_diagnostics) {
        for (auto entry = owned.constBegin(); entry != owned.constEnd(); ++entry) {
            for (const auto& diagnostic : entry.value()) {
                rows.append({entry.key(), diagnostic});
            }
        }
    }
    // clang-format off
    std::sort(rows.begin(), rows.end(), [](const QPair<QString, LanguageDiagnostic>& first, const QPair<QString, LanguageDiagnostic>& second) { return first.first == second.first ? first.second.startLine < second.second.startLine : first.first < second.first; });
    // clang-format on

    // The panel carries every file the workspace analysed, so a written filter is what narrows it to the one being looked for.
    const QString filter = m_problemFilter->text().trimmed();

    if (!filter.isEmpty()) {
        // clang-format off
        rows.removeIf([&filter](const QPair<QString, LanguageDiagnostic>& row) { return !row.first.contains(filter, Qt::CaseInsensitive) && !row.second.message.contains(filter, Qt::CaseInsensitive); });
        // clang-format on
    }

    m_problems->clear();

    for (const auto& row : rows.mid(0, LanguageRegistry::limits().maximumProblems)) {
        auto* item = new QTreeWidgetItem({QFileInfo(row.first).fileName(), QString::number(row.second.startLine + 1), row.second.code, row.second.source, row.second.message});
        item->setData(0, Qt::UserRole, row.first);
        item->setData(1, Qt::UserRole, row.second.startLine);

        // What a diagnostic points at is opened from the row it belongs to, so the reader reaches the other end without searching for it.
        for (const auto& related : row.second.related) {
            auto* child = new QTreeWidgetItem({QFileInfo(related.location.path).fileName(), QString::number(related.location.line + 1), QString{}, QString{}, related.message});
            child->setData(0, Qt::UserRole, related.location.path);
            child->setData(1, Qt::UserRole, related.location.line);
            item->addChild(child);
        }

        m_problems->addTopLevelItem(item);
    }

    const QString shown = QString::number(m_problems->topLevelItemCount()) + (rows.size() > LanguageRegistry::limits().maximumProblems ? QStringLiteral("+") : QString{});
    m_bottomPanel->setTabText(m_bottomPanel->indexOf(m_problemsPage), m_host.translate(QStringLiteral("code-editor.problems.title")) + QStringLiteral(" (") + shown + QStringLiteral(")"));
}

// A workspace of any size is read away from the interface, and a query typed after this one discards what this one found.
void CodeWorkspaceView::refreshWorkspaceSearch() {
    const QString query = m_searchQuery->text().trimmed();
    const QString root = m_initialState.rootPath;
    const qint64 maximumBytes = LanguageRegistry::limits().maximumFileBytes;
    const int maximumMatches = LanguageRegistry::limits().maximumSearchMatches;
    const quint64 revision = ++m_searchRevision;
    // clang-format off
    auto found = QtConcurrent::run([root, query, maximumBytes, maximumMatches]() { return WorkspaceSearches::searchWorkspace(root, query, maximumBytes, maximumMatches); });
    found.then(this, [this, revision](const WorkspaceSearchResult& result) {
        if (revision != m_searchRevision) {
            return;
        }
        m_searchResults->clear();
        for (const auto& match : result.matches) {
            auto* item = new QTreeWidgetItem({QFileInfo(match.path).fileName(), QString::number(match.line + 1), match.text});
            item->setData(0, Qt::UserRole, QDir(m_initialState.rootPath).filePath(match.path));
            item->setData(1, Qt::UserRole, match.line);
            m_searchResults->addTopLevelItem(item);
        }
        const QString shown = QString::number(result.matches.size()) + (result.complete ? QString{} : QStringLiteral("+"));
        m_bottomPanel->setTabText(m_bottomPanel->indexOf(m_searchPage), m_host.translate(QStringLiteral("code-editor.search.title")) + QStringLiteral(" (") + shown + QStringLiteral(")"));
    });
    // clang-format on
}

CodeDocument* CodeWorkspaceView::documentAt(int index) const {
    return qobject_cast<CodeDocument*>(m_documents->widget(index));
}

CodeDocument* CodeWorkspaceView::currentDocument() const {
    return qobject_cast<CodeDocument*>(m_documents->currentWidget());
}

QVector<CodeDocument*> CodeWorkspaceView::documents() const {
    QVector<CodeDocument*> open;
    open.reserve(m_documents->count());

    for (int index = 0; index < m_documents->count(); ++index) {
        if (auto* candidate = documentAt(index); candidate != nullptr) {
            open.append(candidate);
        }
    }

    return open;
}

CodeDocument* CodeWorkspaceView::document(const QString& path) const {
    for (auto* openDocument : documents()) {
        if (openDocument->path() == path) {
            return openDocument;
        }
    }

    return nullptr;
}

LanguageServerClient* CodeWorkspaceView::languageServer(const LanguageDefinition& language) {
    if (language.id == QStringLiteral("plaintext")) {
        return nullptr;
    }
    if (m_languageServers.contains(language.id)) {
        return m_languageServers.value(language.id);
    }

    for (const auto& configuration : m_availableLanguageServers) {
        if (configuration.languageId != language.id) {
            continue;
        }
        auto* server = new LanguageServerClient(configuration, m_initialState.rootPath, this);
        m_languageServers.insert(language.id, server);
        connectSymbolSources(server);
        // A server that gave up on its own leaves the workspace, so the next document that needs it starts a new one instead of talking to a dead process.
        // clang-format off
        connect(server, &LanguageServerClient::stopped, this, [this, languageId = language.id, server]() { if (m_languageServers.value(languageId) == server) { m_languageServers.remove(languageId); server->deleteLater(); } if (m_diagnostics.remove(languageId)) { refreshProblems(); } });
        // clang-format on
        // clang-format off
        connect(server, &LanguageServerClient::serverError, this, [this, language](const QString& message) { m_host.log(LogLevel::Error, QStringLiteral("code-editor.lsp"), message, {{QStringLiteral("language"), language.id}}); emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.language-server")) + QStringLiteral("\n") + message); });
        connect(server, &LanguageServerClient::serverLog, this, [this, language](const QString& message) { m_host.log(LogLevel::Debug, QStringLiteral("code-editor.lsp"), message, {{QStringLiteral("language"), language.id}}); });
        // clang-format on
        server->start();
        return server;
    }

    return nullptr;
}

QString CodeWorkspaceView::selectedPath() const {
    const QModelIndex index = m_tree->currentIndex();
    return index.isValid() ? QDir::cleanPath(m_fileModel->filePath(index)) : m_initialState.rootPath;
}

QString CodeWorkspaceView::targetDirectory(const QString& path) const {
    return QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath();
}

bool CodeWorkspaceView::containsPath(const QString& path) const {
    return !containedPath(path).isEmpty();
}

QString CodeWorkspaceView::containedPath(const QString& path) const {
    const QFileInfo information(QDir::cleanPath(path));
    QString resolved;

    if (information.exists() || information.isSymLink()) {
        resolved = information.canonicalFilePath();
    } else {
        const QString parent = QFileInfo(information.absolutePath()).canonicalFilePath();
        if (!parent.isEmpty()) {
            resolved = QDir(parent).filePath(information.fileName());
        }
    }

    resolved = QDir::cleanPath(resolved);

    if (resolved == m_initialState.rootPath || resolved.startsWith(m_initialState.rootPath + QLatin1Char('/'))) {
        return resolved;
    }

    return {};
}

void CodeWorkspaceView::reportResult(const Result<void>& result) {
    if (!result.hasValue()) {
        emit operationFailed(FileSystemFailures::fileSystemFailureMessage(result.error(), m_host));
    }
}

} // namespace workpane::plugins::codeeditor
