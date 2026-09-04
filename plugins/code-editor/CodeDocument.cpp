#include "CodeDocument.h"
#include "FileSystemFailure.h"

#include "FileWatch.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QScrollBar>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace workpane::plugins::codeeditor {

struct EditorConfigCollection final {
    QVector<EditorConfigFile> files;
    int pending{0};
};

class CodeDocumentHelper final {
  public:
    static QByteArray contentDigest(const QByteArray& content);
    static LineEnding detectLineEnding(const QString& text);
    static QString unsupportedEncodingName(const QByteArray& content);
    static std::optional<TextCharset> markedCharset(const QByteArray& content);
    static std::optional<QString> decodeText(const QByteArray& content, TextCharset charset);
    static QStringConverter::Encoding converterOf(TextCharset charset);
    static QByteArray markOf(TextCharset charset);
    static std::optional<QByteArray> encodeText(const QString& text, TextCharset charset);
    static QString lineEndingText(LineEnding ending);
    static ui::FindBarLabels findBarLabels(PluginHost& host);
};

ui::FindBarLabels CodeDocumentHelper::findBarLabels(PluginHost& host) {
    return {host.translate(QStringLiteral("code-editor.find.label")), host.translate(QStringLiteral("code-editor.find.case-sensitive")), host.translate(QStringLiteral("code-editor.find.whole-word")), host.translate(QStringLiteral("code-editor.find.previous")), host.translate(QStringLiteral("code-editor.find.next")), host.translate(QStringLiteral("code-editor.find.close")), host.translate(QStringLiteral("code-editor.find.not-found"))};
}

QByteArray CodeDocumentHelper::contentDigest(const QByteArray& content) {
    return QCryptographicHash::hash(content, QCryptographicHash::Sha256);
}

LineEnding CodeDocumentHelper::detectLineEnding(const QString& text) {
    if (text.contains(QStringLiteral("\r\n"))) {
        return LineEnding::Crlf;
    }
    if (text.contains(QLatin1Char('\r'))) {
        return LineEnding::Cr;
    }

    return LineEnding::Lf;
}

QString CodeDocumentHelper::unsupportedEncodingName(const QByteArray& content) {
    if (content.startsWith(QByteArrayLiteral("\x00\x00\xFE\xFF")) || content.startsWith(QByteArrayLiteral("\xFF\xFE\x00\x00"))) {
        return QStringLiteral("UTF-32");
    }
    if (content.startsWith(QByteArrayLiteral("\x2B\x2F\x76"))) {
        return QStringLiteral("UTF-7");
    }

    return {};
}

// The mark a file starts with names its encoding, so a file carrying one is never guessed at.
std::optional<TextCharset> CodeDocumentHelper::markedCharset(const QByteArray& content) {
    if (content.startsWith(QByteArrayLiteral("\xEF\xBB\xBF"))) {
        return TextCharset::Utf8Bom;
    }
    if (content.startsWith(QByteArrayLiteral("\xFF\xFE"))) {
        return TextCharset::Utf16Le;
    }
    if (content.startsWith(QByteArrayLiteral("\xFE\xFF"))) {
        return TextCharset::Utf16Be;
    }

    return std::nullopt;
}

QStringConverter::Encoding CodeDocumentHelper::converterOf(TextCharset charset) {
    switch (charset) {
    case TextCharset::Utf16Le:
        return QStringConverter::Utf16LE;
    case TextCharset::Utf16Be:
        return QStringConverter::Utf16BE;
    case TextCharset::Latin1:
        return QStringConverter::Latin1;
    case TextCharset::Utf8:
    case TextCharset::Utf8Bom:
        return QStringConverter::Utf8;
    }

    return QStringConverter::Utf8;
}

QByteArray CodeDocumentHelper::markOf(TextCharset charset) {
    switch (charset) {
    case TextCharset::Utf8Bom:
        return QByteArrayLiteral("\xEF\xBB\xBF");
    case TextCharset::Utf16Le:
        return QByteArrayLiteral("\xFF\xFE");
    case TextCharset::Utf16Be:
        return QByteArrayLiteral("\xFE\xFF");
    case TextCharset::Utf8:
    case TextCharset::Latin1:
        return {};
    }

    return {};
}

// The mark is stripped only when the file really starts with it, because an encoding chosen by hand names no mark the bytes have to carry.
std::optional<QString> CodeDocumentHelper::decodeText(const QByteArray& content, TextCharset charset) {
    const QByteArray mark = CodeDocumentHelper::markOf(charset);
    const QByteArray payload = content.startsWith(mark) ? content.mid(mark.size()) : content;
    QStringDecoder decoder(CodeDocumentHelper::converterOf(charset));
    const QString text = decoder.decode(payload);

    if (decoder.hasError()) {
        return std::nullopt;
    }

    return text;
}

// An encoding that cannot spell a character the buffer holds would write a question mark in its place, so the loss is reported instead of written.
std::optional<QByteArray> CodeDocumentHelper::encodeText(const QString& text, TextCharset charset) {
    QStringEncoder encoder(CodeDocumentHelper::converterOf(charset));
    const QByteArray encoded = encoder.encode(text);

    if (encoder.hasError()) {
        return std::nullopt;
    }

    return CodeDocumentHelper::markOf(charset) + encoded;
}

QString CodeDocumentHelper::lineEndingText(LineEnding ending) {
    switch (ending) {
    case LineEnding::Lf:
        return QStringLiteral("\n");
    case LineEnding::Crlf:
        return QStringLiteral("\r\n");
    case LineEnding::Cr:
        return QStringLiteral("\r");
    }

    return QStringLiteral("\n");
}

CodeDocument::CodeDocument(const QString& path, const QString& rootPath, bool wordWrap, CodeEditorFont font, CodeColorScheme scheme, TextCharset defaultCharset, PluginHost& host, QWidget* parent) : QWidget(parent), m_path(QDir::cleanPath(path)), m_rootPath(QDir::cleanPath(rootPath)), m_defaultCharset(defaultCharset), m_host(host), m_editor(new CodeEditorWidget(host.theme(), scheme, this)), m_findBar(new ui::FindBar(host.theme(), CodeDocumentHelper::findBarLabels(host), this)), m_language(LanguageRegistry::languageForPath(m_path)), m_scheme(std::move(scheme)) {
    m_editor->setEditorFont(font.family, font.size);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_editor, 1);
    layout->addWidget(m_findBar);
    m_findBar->hide();
    m_editor->setWordWrap(wordWrap);
    m_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    m_highlighter = std::make_unique<CodeSyntaxHighlighter>(m_editor->document(), m_language, m_scheme);
    m_externalChangeTimer.setSingleShot(true);
    m_externalChangeTimer.setInterval(LanguageRegistry::limits().externalChangeDebounceMs);
    m_languageServerTimer.setSingleShot(true);
    m_languageServerTimer.setInterval(LanguageRegistry::limits().changeDebounceMs);
    m_analysisTimer.setSingleShot(true);
    m_analysisTimer.setInterval(LanguageRegistry::limits().analysisDebounceMs);
    m_highlightTimer.setSingleShot(true);
    m_highlightTimer.setInterval(LanguageRegistry::limits().highlightDebounceMs);
    // clang-format off
    connect(m_editor->document(), &QTextDocument::contentsChange, this, [this](int position, int removed, int added) { documentChanged(position, removed, added); });
    connect(m_editor, &CodeEditorWidget::saveRequested, this, [this]() { save(); });
    connect(m_editor, &CodeEditorWidget::findRequested, this, [this]() { m_searchAnchor = m_editor->textCursor().selectionStart(); m_findBar->activate(m_editor->textCursor().selectedText()); });
    connect(m_editor, &CodeEditorWidget::findNextRequested, this, [this](bool forward) { findText(m_findBar->query(), forward); });
    connect(m_editor, &CodeEditorWidget::completionRequested, this, [this](int line, int character) { synchronizeLanguageServer(); if (m_languageServer != nullptr) { m_languageServer->requestCompletion(m_path, line, character); } });
    connect(m_editor, &CodeEditorWidget::definitionRequested, this, [this](int line, int character) { synchronizeLanguageServer(); if (m_languageServer != nullptr) { m_languageServer->requestSymbolQuery(m_path, line, character, SymbolQueryKind::Definition); } });
    connect(m_editor, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) { showContextMenu(position); });
    connect(m_editor, &CodeEditorWidget::referencesRequested, this, [this](int line, int character) { synchronizeLanguageServer(); if (m_languageServer != nullptr) { m_languageServer->requestSymbolQuery(m_path, line, character, SymbolQueryKind::References); } });
    connect(m_editor, &CodeEditorWidget::completionDocumentationRequested, this, [this](int row) { if (m_languageServer != nullptr) { m_languageServer->requestCompletionDocumentation(m_path, row, m_completionItems.value(row)); } });
    connect(m_editor, &CodeEditorWidget::hoverRequested, this, [this](int line, int character) { synchronizeLanguageServer(); if (m_languageServer != nullptr) { m_languageServer->requestHover(m_path, line, character); } });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this]() { emit stateChanged(); });
    connect(m_findBar, &ui::FindBar::searchRequested, this, [this](const QString& query, bool forward) { findText(query, forward); });
    connect(m_findBar, &ui::FindBar::queryChanged, this, [this]() { searchFromAnchor(m_findBar->query()); });
    connect(m_findBar, &ui::FindBar::dismissed, this, [this]() { m_findBar->hide(); m_editor->setSearchMatches({}); m_editor->setFocus(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& changed) { FileWatch::rearm(m_watcher, changed); scheduleExternalChange(); });
    connect(&m_externalChangeTimer, &QTimer::timeout, this, [this]() { watchedFileChanged(); });
    // The platform can leave a watched file unreported, so coming back to the application looks at the file again.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) { if (state == Qt::ApplicationActive) { scheduleExternalChange(); } });
    connect(&m_languageServerTimer, &QTimer::timeout, this, [this]() { synchronizeLanguageServer(); });
    connect(&m_analysisTimer, &QTimer::timeout, this, [this]() { requestAnalysis(); });
    connect(&m_highlightTimer, &QTimer::timeout, this, [this]() { if (m_languageServer != nullptr) { m_languageServer->requestDocumentHighlights(m_path, m_editor->textCursor().blockNumber(), m_editor->textCursor().positionInBlock()); } });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this]() { m_highlightTimer.start(); });
    // clang-format on
    // A finished read runs its continuation immediately, so the first load waits for the caller to connect before anything is announced.
    // clang-format off
    QTimer::singleShot(0, this, [this]() { loadEditorConfig(); reload(); });
    // clang-format on
}

CodeDocument::~CodeDocument() {
    disconnect(m_editor->document(), nullptr, this, nullptr);
    m_externalChangeTimer.stop();
    m_languageServerTimer.stop();
    m_analysisTimer.stop();
    m_highlightTimer.stop();
    m_highlighter.reset();

    if (m_languageServer != nullptr) {
        m_languageServer->closeDocument(m_path);
    }
}

const QString& CodeDocument::path() const {
    return m_path;
}

QString CodeDocument::title() const {
    return QFileInfo(m_path).fileName() + (m_dirty ? QStringLiteral(" *") : QString{});
}

bool CodeDocument::dirty() const {
    return m_dirty;
}

int CodeDocument::cursorPosition() const {
    return m_editor->textCursor().position();
}

int CodeDocument::cursorLine() const {
    return m_editor->textCursor().blockNumber() + 1;
}

int CodeDocument::cursorColumn() const {
    return m_editor->textCursor().positionInBlock() + 1;
}

const LanguageDefinition& CodeDocument::language() const {
    return m_language;
}

// A declared rule wins, and everything the file already carries survives when no rule declares otherwise.
LineEnding CodeDocument::lineEnding() const {
    return m_editorConfig.lineEnding.value_or(m_detectedLineEnding);
}

// An encoding the reader chose by hand outranks the one the project declares, which outranks the one the file was read in.
TextCharset CodeDocument::charset() const {
    return m_selectedCharset.value_or(m_editorConfig.charset.value_or(m_detectedCharset));
}

void CodeDocument::reopenWithCharset(TextCharset charset) {
    m_selectedCharset = charset;
    m_rereading = true;
    reload();
}

// A file already written in another encoding is rewritten whole, so this one saves even when nothing was typed.
void CodeDocument::saveWithCharset(TextCharset charset) {
    m_selectedCharset = charset;
    writeContent();
}

const EditorConfigProperties& CodeDocument::editorConfig() const {
    return m_editorConfig;
}

CodeEditorWidget& CodeDocument::editor() const {
    return *m_editor;
}

void CodeDocument::setCursorPosition(int position) {
    QTextCursor cursor = m_editor->textCursor();
    cursor.setPosition(std::clamp(position, 0, m_editor->document()->characterCount() - 1));
    m_editor->setTextCursor(cursor);
}

void CodeDocument::setCursorLocation(int line, int character) {
    QTextBlock block = m_editor->document()->findBlockByNumber(std::max(0, line));

    if (!block.isValid()) {
        block = m_editor->document()->lastBlock();
    }

    QTextCursor cursor(block);
    cursor.setPosition(std::min(block.position() + std::max(0, character), block.position() + block.length() - 1));
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
}

void CodeDocument::setLanguageServer(LanguageServerClient* server) {
    if (m_languageServer == server) {
        return;
    }

    if (m_languageServer != nullptr) {
        m_languageServer->closeDocument(m_path);
        disconnect(m_languageServer, nullptr, this, nullptr);
    }

    m_languageServer = server;

    if (server == nullptr) {
        m_editor->setDiagnostics({});
        m_editor->setOccurrences({});
        m_highlighter->setSemanticTokens({});
        emit diagnosticsChanged(m_path, {});
        emit outlineChanged(m_path, {});
        return;
    }
    // clang-format off
    connect(server, &LanguageServerClient::diagnosticsPublished, this, [this](const QString& path, const QVector<LanguageDiagnostic>& diagnostics) { if (path == m_path) { m_editor->setDiagnostics(diagnostics); emit diagnosticsChanged(path, diagnostics); } });
    connect(server, &LanguageServerClient::documentHighlightsReady, this, [this](const QString& path, const QVector<SourceLocation>& highlights) { if (path == m_path) { m_editor->setOccurrences(highlights); } });
    connect(server, &LanguageServerClient::semanticTokensReady, this, [this](const QString& path, const SemanticTokenSet& tokens) { if (path == m_path) { m_highlighter->setSemanticTokens(tokens); } });
    connect(server, &LanguageServerClient::documentSymbolsReady, this, [this](const QString& path, const QVector<DocumentSymbolNode>& symbols) { if (path == m_path) { emit outlineChanged(path, symbols); } });
    connect(server, &LanguageServerClient::signatureHelpReady, this, [this](const QString& path, const SignatureHelpInfo& help) { if (path == m_path) { m_editor->showSignatureHelp(help); } });
    connect(server, &LanguageServerClient::initialized, this, [this]() { m_analysisTimer.start(); });
    connect(server, &LanguageServerClient::callHierarchyReady, this, [this](const QString& path, CallDirection direction, const QVector<WorkspaceSymbolEntry>& entries) { if (path == m_path) { emit callHierarchyReady(path, direction, entries); } });
    connect(server, &LanguageServerClient::progressChanged, this, [this](const QString& message, bool active) { emit analysisProgress(message, active); });
    connect(server, &LanguageServerClient::completionsReady, this, [this](const QString& path, const QVector<CompletionProposal>& proposals, bool incomplete) { if (path != m_path) { return; } m_completionItems.clear(); for (const auto& proposal : proposals) { m_completionItems.append(proposal.item); } m_editor->showCompletions(proposals, incomplete); });
    connect(server, &LanguageServerClient::completionDocumentationReady, this, [this](const QString& path, int row, const QString& documentation) { if (path == m_path) { m_editor->setCompletionDocumentation(row, documentation); } });
    connect(server, &LanguageServerClient::symbolLocationsReady, this, [this](const QString& path, SymbolQueryKind kind, const QVector<SourceLocation>& locations) { if (path != m_path) { return; } if (kind == SymbolQueryKind::References) { emit referencesReady(path, locations); return; } if (!locations.isEmpty()) { emit navigationRequested(locations.first().path, locations.first().line, locations.first().character); } });
    connect(server, &LanguageServerClient::hoverReady, this, [this](const QString& path, const QString& contents) { if (path == m_path) { m_editor->showHover(contents); } });
    // clang-format on
    server->openDocument(m_path, bufferText(), LanguageRegistry::protocolLanguageId(m_path));
    m_analysisTimer.start();
}

void CodeDocument::setEditorFont(const CodeEditorFont& font) {
    m_editor->setEditorFont(font.family, font.size);
}

// The highlighter carries the colours it was built with, so a scheme change rebuilds it and reapplies the surface under it.
void CodeDocument::setColorScheme(const CodeColorScheme& scheme) {
    m_scheme = scheme;
    m_editor->setColorScheme(m_scheme);
    m_highlighter = std::make_unique<CodeSyntaxHighlighter>(m_editor->document(), m_language, m_scheme);

    if (m_languageServer != nullptr && m_editor->document()->blockCount() <= LanguageRegistry::limits().maximumSemanticTokenLines) {
        m_languageServer->requestSemanticTokens(m_path);
    }
}

void CodeDocument::setWordWrap(bool enabled) {
    m_editor->setWordWrap(enabled);
}

void CodeDocument::updatePath(const QString& path) {
    m_externalChangeTimer.stop();

    if (m_languageServer != nullptr) {
        m_languageServer->closeDocument(m_path);
        disconnect(m_languageServer, nullptr, this, nullptr);
        m_languageServer = nullptr;
    }

    m_watcher.removePath(m_path);
    m_path = QDir::cleanPath(path);
    m_language = LanguageRegistry::languageForPath(m_path);
    m_highlighter = std::make_unique<CodeSyntaxHighlighter>(m_editor->document(), m_language, m_scheme);
    m_watcher.addPath(m_path);
    loadEditorConfig();
    emit titleChanged();
    emit stateChanged();
}

void CodeDocument::reload() {
    const quint64 generation = ++m_loadGeneration;
    m_loading = true;
    auto future = m_host.readFile(m_path, LanguageRegistry::limits().maximumFileBytes);
    // clang-format off
    future.then(this, [this, generation](Result<QByteArray> result) {
        if (generation != m_loadGeneration) {
            return;
        }
        m_loading = false;
        if (!result.hasValue()) {
            emit operationFailed(FileSystemFailures::fileSystemFailureMessage(result.error(), m_host));
            return;
        }
        applyContent(result.value());
    });
    // clang-format on
}

void CodeDocument::save() {
    if (!m_dirty) {
        return;
    }

    writeContent();
}

// A save asked for while one is still being written is kept and runs after it, because an edit the reader saved is never dropped.
void CodeDocument::writeContent() {
    if (m_saving) {
        m_saveRequested = true;
        return;
    }

    const auto encoded = encodedContent();

    if (!encoded.has_value()) {
        emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.charset-unrepresentable")).arg(EditorConfigs::textCharsetName(charset())));
        return;
    }

    m_saving = true;
    const quint64 revision = m_contentRevision;
    const QByteArray digest = CodeDocumentHelper::contentDigest(encoded.value());
    auto future = m_host.writeFile(m_path, encoded.value());
    // clang-format off
    future.then(this, [this, revision, digest](Result<void> result) {
        m_saving = false;
        const bool requestedAgain = std::exchange(m_saveRequested, false);
        if (!result.hasValue()) {
            emit operationFailed(FileSystemFailures::fileSystemFailureMessage(result.error(), m_host));
            return;
        }
        m_storedDigest = digest;
        if (revision == m_contentRevision) {
            m_dirty = false;
            emit titleChanged();
        }
        if (!m_watcher.files().contains(m_path)) {
            m_watcher.addPath(m_path);
        }
        synchronizeLanguageServer();
        if (m_languageServer != nullptr) {
            m_languageServer->saveDocument(m_path, bufferText());
        }
        emit stateChanged();
        if (requestedAgain) {
            writeContent();
        }
    });
    // clang-format on
}

QString CodeDocument::bufferText() const {
    QString text = m_editor->document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

std::optional<QByteArray> CodeDocument::encodedContent() const {
    QStringList lines = bufferText().split(QLatin1Char('\n'));

    if (m_editorConfig.trimTrailingWhitespace.value_or(false)) {
        for (auto& line : lines) {
            while (!line.isEmpty() && (line.back() == QLatin1Char(' ') || line.back() == QLatin1Char('\t') || line.back() == QLatin1Char('\r'))) {
                line.chop(1);
            }
        }
    }

    const QString ending = CodeDocumentHelper::lineEndingText(lineEnding());
    QString text = lines.join(ending);

    if (m_editorConfig.insertFinalNewline.value_or(false) && !text.isEmpty() && !text.endsWith(ending)) {
        text += ending;
    }

    return CodeDocumentHelper::encodeText(text, charset());
}

void CodeDocument::loadEditorConfig() {
    const QStringList paths = EditorConfigs::editorConfigSearchPaths(m_path, m_rootPath);
    const quint64 generation = ++m_editorConfigGeneration;

    if (paths.isEmpty()) {
        applyEditorConfig({});
        return;
    }

    auto collection = std::make_shared<EditorConfigCollection>();
    collection->files.resize(paths.size());
    collection->pending = static_cast<int>(paths.size());

    for (qsizetype index = 0; index < paths.size(); ++index) {
        collection->files[index].directoryPath = QFileInfo(paths.at(index)).absolutePath();
        auto future = m_host.readFile(paths.at(index), LanguageRegistry::limits().maximumFileBytes);
        // clang-format off
        future.then(this, [this, collection, index, generation](Result<QByteArray> result) {
            collection->files[index].content = result.hasValue() ? QString::fromUtf8(result.value()) : QString{};
            --collection->pending;
            if (collection->pending == 0 && generation == m_editorConfigGeneration) {
                applyEditorConfig(EditorConfigs::resolveEditorConfig(m_path, collection->files));
            }
        });
        // clang-format on
    }
}

void CodeDocument::applyEditorConfig(EditorConfigProperties properties) {
    m_editorConfig = std::move(properties);
    m_editor->setIndentation(m_editorConfig.indentStyle.value_or(IndentStyle::Space), EditorConfigs::resolvedIndentWidth(m_editorConfig));

    if (!m_editorConfig.unsupportedCharsets.isEmpty()) {
        emit operationFailed(m_host.translate(QStringLiteral("code-editor.error.editorconfig-charset")) + QStringLiteral("\n") + m_editorConfig.unsupportedCharsets.join(QStringLiteral(", ")));
    }

    emit editorConfigChanged();
}

// Inspecting, hashing and decoding bytes is work whose size the file decides, so it happens away from the interface and only the finished text returns to it.
void CodeDocument::applyContent(const QByteArray& content) {
    const quint64 generation = m_loadGeneration;
    // clang-format off
    auto decoded = QtConcurrent::run([content, fallback = m_defaultCharset, chosen = m_selectedCharset]() {
        DecodedContent result;
        const QString unsupported = CodeDocumentHelper::unsupportedEncodingName(content);
        if (!unsupported.isEmpty()) {
            result.errorKey = QStringLiteral("code-editor.error.encoding-unsupported");
            result.errorDetail = unsupported;
            return result;
        }
        // An encoding the reader chose by hand is the one the bytes are read in, whatever mark they carry.
        if (chosen.has_value()) {
            const auto chosenText = CodeDocumentHelper::decodeText(content, *chosen);
            if (!chosenText.has_value()) {
                result.errorKey = QStringLiteral("code-editor.error.encoding");
                return result;
            }
            result.charset = *chosen;
            result.digest = CodeDocumentHelper::contentDigest(content);
            result.text = *chosenText;
            result.lineEnding = CodeDocumentHelper::detectLineEnding(result.text);
            return result;
        }

        const std::optional<TextCharset> marked = CodeDocumentHelper::markedCharset(content);
        if (!marked.has_value() && content.contains('\0')) {
            result.errorKey = QStringLiteral("code-editor.error.binary-file");
            return result;
        }

        // A file carrying no mark is UTF-8 when it spells valid UTF-8, and otherwise it is read in the encoding the settings declare for it.
        TextCharset charset = marked.value_or(TextCharset::Utf8);
        auto text = CodeDocumentHelper::decodeText(content, charset);
        if (!text.has_value() && !marked.has_value()) {
            charset = fallback;
            text = CodeDocumentHelper::decodeText(content, charset);
        }
        if (!text.has_value()) {
            result.errorKey = QStringLiteral("code-editor.error.encoding");
            return result;
        }
        result.charset = charset;
        result.digest = CodeDocumentHelper::contentDigest(content);
        result.text = *text;
        result.lineEnding = CodeDocumentHelper::detectLineEnding(result.text);
        return result;
    });
    decoded.then(this, [this, generation](const DecodedContent& result) { if (generation == m_loadGeneration) { applyDecoded(result); } });
    // clang-format on
}

void CodeDocument::applyDecoded(const DecodedContent& decoded) {
    // The same bytes read in another encoding spell another text, so a reading the reader asked for is applied whatever they say.
    const bool reread = std::exchange(m_rereading, false);

    if (!decoded.errorKey.isEmpty()) {
        const QString message = m_host.translate(decoded.errorKey);
        emit operationFailed(decoded.errorDetail.isEmpty() ? message : message + QStringLiteral(" ") + decoded.errorDetail);
        return;
    }

    // What the file says is judged only once it is decoded, so the reader can type while it is being read without losing what was typed.
    if (!reread) {
        if (decoded.digest == m_storedDigest) {
            return;
        }
        if (m_dirty) {
            emit externalChangeConflict(m_path);
            return;
        }
    }

    m_storedDigest = decoded.digest;
    m_detectedCharset = decoded.charset;
    m_detectedLineEnding = decoded.lineEnding;

    // The digest says whether the buffer already holds this text, which comparing it whole would answer at the cost of the whole document.
    if (reread || decoded.digest != m_appliedDigest) {
        const int cursorPosition = m_editor->textCursor().position();
        const int scrollValue = m_editor->verticalScrollBar()->value();
        m_loading = true;
        m_editor->setPlainText(decoded.text);
        m_loading = false;

        QTextCursor restored = m_editor->textCursor();
        restored.setPosition(std::min(cursorPosition, m_editor->document()->characterCount() - 1));
        m_editor->setTextCursor(restored);
        m_editor->verticalScrollBar()->setValue(std::min(scrollValue, m_editor->verticalScrollBar()->maximum()));
    }

    m_appliedDigest = decoded.digest;
    m_dirty = false;
    ++m_contentRevision;

    if (!m_watcher.files().contains(m_path)) {
        m_watcher.addPath(m_path);
    }

    emit titleChanged();
    emit loaded(this);
    emit stateChanged();
    m_pendingEdits.clear();

    if (m_languageServer != nullptr) {
        m_languageServer->openDocument(m_path, decoded.text, LanguageRegistry::protocolLanguageId(m_path));
        m_languageServer->replaceDocument(m_path, decoded.text);
    }
}

// Every edit is queued against the text the server already holds, so a debounced batch still describes exactly what changed.
void CodeDocument::documentChanged(int position, int removedCharacters, int addedCharacters) {
    if (m_loading) {
        return;
    }

    ++m_contentRevision;

    if (!m_dirty) {
        m_dirty = true;
        emit titleChanged();
    }

    QTextCursor added(m_editor->document());
    added.setPosition(position);
    added.setPosition(position + addedCharacters, QTextCursor::KeepAnchor);
    const QString addedText = added.selectedText().replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n'));
    m_pendingEdits.append({position, removedCharacters, addedText});
    // Both waits are measured from the last keystroke rather than stacked, and the analysis one is the longer so the server already holds the change.
    m_languageServerTimer.start();
    m_analysisTimer.start();
    requestCompletionOnTrigger(addedText);
    emit stateChanged();
}

// A trigger character is only meaningful after the server has seen it, so the queued edits are flushed before the request leaves.
void CodeDocument::requestCompletionOnTrigger(const QString& addedText) {
    if (m_languageServer == nullptr || addedText.size() != 1) {
        return;
    }

    const bool completes = m_languageServer->completionTriggerCharacters().contains(addedText);
    const bool signature = m_languageServer->signatureHelpTriggerCharacters().contains(addedText);

    if (!completes && !signature) {
        return;
    }

    synchronizeLanguageServer();
    const int line = m_editor->textCursor().blockNumber();
    const int character = m_editor->textCursor().positionInBlock();

    if (completes) {
        m_languageServer->requestCompletion(m_path, line, character);
    }

    if (signature) {
        m_languageServer->requestSignatureHelp(m_path, line, character);
    }
}

// A search is only useful when the reader can see every match and where the cursor is inside them.
void CodeDocument::findText(const QString& query, bool forward) {
    if (query.isEmpty()) {
        refreshSearchMatches(query);
        return;
    }

    const QTextDocument::FindFlags flags = searchFlags(forward);

    if (!m_editor->find(query, flags)) {
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
        m_editor->setTextCursor(cursor);
        m_editor->find(query, flags);
    }

    refreshSearchMatches(query);
}

// The search restarts where the reader was when the bar opened, so typing never drags the cursor forward through the file.
void CodeDocument::searchFromAnchor(const QString& query) {
    if (query.isEmpty()) {
        refreshSearchMatches(query);
        return;
    }

    QTextCursor cursor = m_editor->textCursor();
    cursor.setPosition(std::min(m_searchAnchor, m_editor->document()->characterCount() - 1));
    m_editor->setTextCursor(cursor);
    findText(query, true);
}

QTextDocument::FindFlags CodeDocument::searchFlags(bool forward) const {
    QTextDocument::FindFlags flags;

    if (!forward) {
        flags |= QTextDocument::FindBackward;
    }

    if (m_findBar->caseSensitive()) {
        flags |= QTextDocument::FindCaseSensitively;
    }

    if (m_findBar->wholeWord()) {
        flags |= QTextDocument::FindWholeWords;
    }

    return flags;
}

// Counting stops at a bound because a query matching a whole large file must not freeze the interface it is searching.
void CodeDocument::refreshSearchMatches(const QString& query) {
    QVector<QPair<int, int>> matches;
    int current = 0;

    if (!query.isEmpty()) {
        const QTextDocument::FindFlags flags = searchFlags(true);
        const int cursorEnd = m_editor->textCursor().selectionEnd();
        QTextCursor match = m_editor->document()->find(query, 0, flags);
        while (!match.isNull() && matches.size() < LanguageRegistry::limits().maximumSearchMatches) {
            matches.append({match.selectionStart(), match.selectionEnd() - match.selectionStart()});
            if (match.selectionEnd() == cursorEnd) {
                current = static_cast<int>(matches.size());
            }
            match = m_editor->document()->find(query, match, flags);
        }
    }

    m_editor->setSearchMatches(matches);
    m_findBar->reportMatches(current, static_cast<int>(matches.size()), matches.size() == LanguageRegistry::limits().maximumSearchMatches);
}

// The file is looked at again on demand, because a notification the platform never sent leaves the editor holding what the file no longer says.
void CodeDocument::recheckExternalChange() {
    scheduleExternalChange();
}

void CodeDocument::scheduleExternalChange() {
    m_externalChangeTimer.start();
}

// What the file now says is read whole and judged once it is decoded, because hashing it is work whose size the file decides.
void CodeDocument::watchedFileChanged() {
    if (!QFileInfo::exists(m_path)) {
        emit externalFileRemoved(m_path);
        return;
    }

    if (!m_watcher.files().contains(m_path)) {
        m_watcher.addPath(m_path);
    }

    if (m_saving) {
        return;
    }

    const QString watched = m_path;
    auto future = m_host.readFile(m_path, LanguageRegistry::limits().maximumFileBytes);
    // clang-format off
    future.then(this, [this, watched](Result<QByteArray> result) {
        if (result.hasValue() && watched == m_path) {
            applyContent(result.value());
        }
    });
    // clang-format on
}

// Every answer that describes the file is asked for again once the server has the current text, because an outline of the previous version explains nothing.
void CodeDocument::requestAnalysis() {
    if (m_languageServer == nullptr) {
        return;
    }

    synchronizeLanguageServer();
    m_languageServer->requestDocumentSymbols(m_path);
    m_languageServer->requestDiagnostics(m_path);

    // Semantic tokens repaint the complete document, so a file above this bound keeps the pattern colors instead of freezing on every change.
    if (m_editor->document()->blockCount() <= LanguageRegistry::limits().maximumSemanticTokenLines) {
        m_languageServer->requestSemanticTokens(m_path);
    }
}

// The menu answers where the pointer is, so the query it offers is about the symbol under the pointer and not about the caret left elsewhere.
void CodeDocument::showContextMenu(const QPoint& position) {
    const QPoint viewportPosition = m_editor->viewport()->mapFromGlobal(m_editor->mapToGlobal(position));
    const QTextCursor clicked = m_editor->cursorForPosition(viewportPosition);
    const QTextCursor selection = m_editor->textCursor();

    if (!selection.hasSelection() || clicked.position() < selection.selectionStart() || clicked.position() > selection.selectionEnd()) {
        m_editor->setTextCursor(clicked);
    }

    auto* menu = m_editor->createStandardContextMenu(viewportPosition);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QVector<QPair<SymbolQueryKind, QString>> queries{{SymbolQueryKind::Definition, QStringLiteral("code-editor.actions.go-to-definition")}, {SymbolQueryKind::Declaration, QStringLiteral("code-editor.actions.go-to-declaration")}, {SymbolQueryKind::TypeDefinition, QStringLiteral("code-editor.actions.go-to-type-definition")}, {SymbolQueryKind::Implementation, QStringLiteral("code-editor.actions.go-to-implementation")}, {SymbolQueryKind::References, QStringLiteral("code-editor.actions.find-references")}};
    bool separated = false;

    const QVector<QPair<CallDirection, QString>> calls{{CallDirection::Incoming, QStringLiteral("code-editor.actions.incoming-calls")}, {CallDirection::Outgoing, QStringLiteral("code-editor.actions.outgoing-calls")}};

    for (const auto& query : queries) {
        if (m_languageServer == nullptr || !m_languageServer->supports(query.first)) {
            continue;
        }
        if (!separated) {
            menu->addSeparator();
            separated = true;
        }
        auto* action = menu->addAction(m_host.translate(query.second));
        // clang-format off
        connect(action, &QAction::triggered, this, [this, kind = query.first]() { requestSymbolQuery(kind); });
        // clang-format on
    }

    for (const auto& call : calls) {
        if (m_languageServer == nullptr || !m_languageServer->supportsCallHierarchy()) {
            continue;
        }
        if (!separated) {
            menu->addSeparator();
            separated = true;
        }
        auto* action = menu->addAction(m_host.translate(call.second));
        // clang-format off
        connect(action, &QAction::triggered, this, [this, direction = call.first]() { requestCallHierarchy(direction); });
        // clang-format on
    }

    menu->popup(m_editor->viewport()->mapToGlobal(viewportPosition));
}

void CodeDocument::requestCallHierarchy(CallDirection direction) {
    if (m_languageServer == nullptr) {
        return;
    }

    synchronizeLanguageServer();
    m_languageServer->requestCallHierarchy(m_path, m_editor->textCursor().blockNumber(), m_editor->textCursor().positionInBlock(), direction);
}

void CodeDocument::refreshAnalysis() {
    requestAnalysis();
}

void CodeDocument::requestSymbolQuery(SymbolQueryKind kind) {
    if (m_languageServer == nullptr) {
        return;
    }

    synchronizeLanguageServer();
    m_languageServer->requestSymbolQuery(m_path, m_editor->textCursor().blockNumber(), m_editor->textCursor().positionInBlock(), kind);
}

void CodeDocument::reloadEditorConfig() {
    loadEditorConfig();
}

void CodeDocument::synchronizeLanguageServer() {
    m_languageServerTimer.stop();

    if (m_pendingEdits.isEmpty() || m_languageServer == nullptr) {
        m_pendingEdits.clear();
        return;
    }

    if (!m_languageServer->editDocument(m_path, m_pendingEdits)) {
        m_languageServer->replaceDocument(m_path, bufferText());
    }

    m_pendingEdits.clear();
}

} // namespace workpane::plugins::codeeditor
