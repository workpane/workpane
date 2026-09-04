#include "terminal/TerminalSession.h"

#ifdef Q_OS_WIN
#include "terminal/platform/windows/ConPtyBackend.h"
#else
#include "terminal/platform/posix/PosixPtyBackend.h"
#endif

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <utility>

namespace workpane::terminalcore {

constexpr qsizetype maximumOutputChunkSize = 128 * 1024;
constexpr qsizetype maximumPendingOutputSize = 1024 * 1024;
constexpr qsizetype outputResumeSize = maximumPendingOutputSize / 2;

TerminalSession::TerminalSession(domain::TerminalSessionState state, ShellProfile profile, domain::TerminalTheme theme, std::unique_ptr<IPtyBackend> backend, QObject* parent) : QObject(parent), m_state(std::move(state)), m_profile(std::move(profile)), m_theme(std::move(theme)), m_backend(std::move(backend)) {
    connect(m_backend.get(), &IPtyBackend::outputReady, this, &TerminalSession::processOutput);
    connect(m_backend.get(), &IPtyBackend::processExited, this, &TerminalSession::processExited);
    connect(m_backend.get(), &IPtyBackend::backendError, this, &TerminalSession::processError);
    connect(m_backend.get(), &IPtyBackend::workingDirectoryChanged, this, &TerminalSession::updateBackendWorkingDirectory);
    connect(&m_emulator, &GhosttyTerminalAdapter::responseReady, this, &TerminalSession::writeTerminalResponse);
    connect(&m_emulator, &GhosttyTerminalAdapter::titleChanged, this, &TerminalSession::updateTitle);
    connect(&m_emulator, &GhosttyTerminalAdapter::workingDirectoryChanged, this, &TerminalSession::updateWorkingDirectory);
    connect(&m_emulator, &GhosttyTerminalAdapter::bellRang, this, &TerminalSession::bellRang);
    connect(&m_emulator, &GhosttyTerminalAdapter::clipboardWriteRequested, this, &TerminalSession::clipboardWriteRequested);
    connect(&m_emulator, &GhosttyTerminalAdapter::notificationPosted, this, &TerminalSession::notificationPosted);
    m_outputTimer.setSingleShot(true);
    connect(&m_outputTimer, &QTimer::timeout, this, &TerminalSession::flushOutput);
}

TerminalSession::~TerminalSession() {
    terminate();
}

const QString& TerminalSession::id() const {
    return m_state.id;
}

const QString& TerminalSession::name() const {
    return m_state.name;
}

const QString& TerminalSession::cwd() const {
    return m_state.cwd;
}

const QString& TerminalSession::shellName() const {
    return m_profile.name;
}

QString TerminalSession::status() const {
    switch (m_state.processState) {
    case domain::TerminalProcessState::Starting:
        return QStringLiteral("Starting");
    case domain::TerminalProcessState::Running:
        return QStringLiteral("Running");
    case domain::TerminalProcessState::Exited:
        return QStringLiteral("Exited");
    case domain::TerminalProcessState::Failed:
        return QStringLiteral("Failed");
    }

    return QStringLiteral("Starting");
}

int TerminalSession::exitCode() const {
    return m_exitCode;
}

const domain::TerminalSessionState& TerminalSession::state() const {
    return m_state;
}

TerminalRenderSnapshot TerminalSession::snapshot() {
    return m_emulator.snapshot();
}

void TerminalSession::setName(const QString& newName) {
    const QString normalized = newName.trimmed();

    if (normalized.isEmpty() || normalized == m_state.name) {
        return;
    }

    m_state.name = normalized;
    m_state.updatedAt = QDateTime::currentMSecsSinceEpoch();
    emit nameChanged();
    emit stateChanged();
}

void TerminalSession::setTheme(const domain::TerminalTheme& theme) {
    const auto result = m_emulator.setTheme(theme);

    if (!result.hasValue()) {
        emit errorOccurred(result.error());
        return;
    }

    m_theme = theme;
    emit renderChanged();
}

Result<void> TerminalSession::start() {
    if (m_backend->running()) {
        return Result<void>::failure({"terminal_already_running", "The terminal process is already running", m_state.id});
    }

    m_state.processState = domain::TerminalProcessState::Starting;
    emit statusChanged();

    const auto emulatorResult = m_emulator.initialize(m_columns, m_rows, m_cellWidth, m_cellHeight, m_theme);

    if (!emulatorResult.hasValue()) {
        m_state.processState = domain::TerminalProcessState::Failed;
        emit statusChanged();
        return emulatorResult;
    }

    const auto backendResult = m_backend->start(m_profile, m_state.cwd, m_state.historyFile, m_columns, m_rows);

    if (!backendResult.hasValue()) {
        m_state.processState = domain::TerminalProcessState::Failed;
        emit statusChanged();
        return backendResult;
    }

    m_state.processState = domain::TerminalProcessState::Running;
    m_exitCode = -1;
    emit statusChanged();
    emit renderChanged();
    return Result<void>::success();
}

Result<void> TerminalSession::write(const QByteArray& bytes) {
    return m_backend->write(bytes);
}

Result<void> TerminalSession::writeLocalPaths(const QStringList& paths) {
    return write(ShellPaths::formatLocalPathsForShell(m_profile, paths).toUtf8());
}

Result<void> TerminalSession::sendKey(const QKeyEvent& event) {
    const auto encoded = m_emulator.encodeKey(event);

    if (!encoded.hasValue()) {
        return Result<void>::failure(encoded.error());
    }

    return m_backend->write(encoded.value());
}

Result<void> TerminalSession::paste(const QByteArray& text) {
    const auto encoded = m_emulator.encodePaste(text);

    if (!encoded.hasValue()) {
        return Result<void>::failure(encoded.error());
    }

    return m_backend->write(encoded.value());
}

bool TerminalSession::pasteExecutesOnArrival(const QByteArray& text) const {
    return m_emulator.pasteExecutesOnArrival(text);
}

bool TerminalSession::programWantsMouse() const {
    return m_emulator.programWantsMouse();
}

bool TerminalSession::programWantsFocus() const {
    return m_emulator.programWantsFocus();
}

Result<void> TerminalSession::sendMouse(const MouseReport& report) {
    const auto encoded = m_emulator.encodeMouse(report);

    if (!encoded.hasValue()) {
        return Result<void>::failure(encoded.error());
    }
    if (encoded.value().isEmpty()) {
        return Result<void>::success();
    }

    return m_backend->write(encoded.value());
}

Result<void> TerminalSession::beginSelection(const QPointF& position, quint64 timeNanoseconds, quint64 repeatIntervalNanoseconds, double repeatDistance, bool rectangle) {
    const auto result = m_emulator.beginSelection(position, timeNanoseconds, repeatIntervalNanoseconds, repeatDistance, rectangle);
    emit renderChanged();
    return result;
}

Result<void> TerminalSession::extendSelection(const QPointF& position, bool rectangle) {
    const auto result = m_emulator.extendSelection(position, rectangle);
    emit renderChanged();
    return result;
}

void TerminalSession::endSelection(const QPointF& position) {
    m_emulator.endSelection(position);
}

SelectionAutoscroll TerminalSession::selectionAutoscroll() const {
    return m_emulator.selectionAutoscroll();
}

Result<void> TerminalSession::advanceSelectionAutoscroll(const QPointF& position, bool rectangle) {
    const auto result = m_emulator.advanceSelectionAutoscroll(position, rectangle);
    emit renderChanged();
    return result;
}

void TerminalSession::selectAll() {
    m_emulator.selectAll();
    emit renderChanged();
}

QList<SearchMatch> TerminalSession::search(const QString& query, bool caseSensitive, bool wholeWord, int maximum) const {
    return m_emulator.search(query, caseSensitive, wholeWord, maximum);
}

void TerminalSession::revealMatch(const SearchMatch& match) {
    if (m_emulator.revealMatch(match)) {
        emit renderChanged();
    }
}

void TerminalSession::setClipboardWriteAllowed(bool allowed) {
    m_emulator.setClipboardWriteAllowed(allowed);
}

QString TerminalSession::addressAt(const QPointF& position) const {
    return m_emulator.addressAt(position);
}

void TerminalSession::clearScrollback() {
    m_emulator.clearScrollback();
    emit renderChanged();
}

void TerminalSession::clearSelection() {
    m_emulator.clearSelection();
    emit renderChanged();
}

bool TerminalSession::hasSelection() const {
    return m_emulator.hasSelection();
}

QString TerminalSession::selectionText() const {
    return m_emulator.selectionText();
}

Result<void> TerminalSession::sendFocus(bool gained) {
    const auto encoded = m_emulator.encodeFocus(gained);

    if (!encoded.hasValue()) {
        return Result<void>::failure(encoded.error());
    }

    return m_backend->write(encoded.value());
}

Result<void> TerminalSession::resize(int columns, int rows, int cellWidth, int cellHeight) {
    if (columns == m_columns && rows == m_rows && cellWidth == m_cellWidth && cellHeight == m_cellHeight) {
        return Result<void>::success();
    }

    const auto emulatorResult = m_emulator.resize(columns, rows, cellWidth, cellHeight);

    if (!emulatorResult.hasValue()) {
        return emulatorResult;
    }

    if (m_backend->running()) {
        const auto backendResult = m_backend->resize(columns, rows, cellWidth, cellHeight);
        if (!backendResult.hasValue()) {
            return backendResult;
        }
    }

    m_columns = columns;
    m_rows = rows;
    m_cellWidth = cellWidth;
    m_cellHeight = cellHeight;
    emit renderChanged();
    return Result<void>::success();
}

void TerminalSession::scrollViewport(qint64 rows) {
    m_emulator.scrollViewport(rows);
    emit renderChanged();
}

void TerminalSession::scrollToRow(quint64 row) {
    m_emulator.scrollToRow(row);
    emit renderChanged();
}

void TerminalSession::scrollToTop() {
    m_emulator.scrollToTop();
    emit renderChanged();
}

void TerminalSession::scrollToBottom() {
    m_emulator.scrollToBottom();
    emit renderChanged();
}

void TerminalSession::restart() {
    terminate();
    const auto result = start();

    if (!result.hasValue()) {
        emit errorOccurred(result.error());
    }
}

void TerminalSession::terminate() {
    m_outputTimer.stop();
    m_pendingOutput.clear();
    m_outputPaused = false;
    m_backend->setOutputPaused(false);
    m_backend->terminate();
}

void TerminalSession::processOutput(const QByteArray& bytes) {
    if (bytes.isEmpty()) {
        return;
    }

    m_pendingOutput.append(bytes);

    if (!m_outputPaused && m_pendingOutput.size() >= maximumPendingOutputSize) {
        m_outputPaused = true;
        m_backend->setOutputPaused(true);
    }

    if (!m_outputTimer.isActive()) {
        m_outputTimer.start(0);
    }
}

void TerminalSession::flushOutput() {
    const qsizetype chunkSize = std::min(maximumOutputChunkSize, m_pendingOutput.size());
    const QByteArray chunk = m_pendingOutput.first(chunkSize);
    m_pendingOutput.remove(0, chunkSize);
    m_emulator.write(chunk);
    emit renderChanged();

    if (m_outputPaused && m_pendingOutput.size() <= outputResumeSize) {
        m_outputPaused = false;
        m_backend->setOutputPaused(false);
    }

    if (!m_pendingOutput.isEmpty()) {
        m_outputTimer.start(0);
    }
}

void TerminalSession::processExited(int processExitCode) {
    m_state.processState = domain::TerminalProcessState::Exited;
    m_exitCode = processExitCode;
    emit statusChanged();
    emit renderChanged();
}

void TerminalSession::processError(const QString& message) {
    m_state.processState = domain::TerminalProcessState::Failed;
    emit statusChanged();
    emit errorOccurred({"terminal_backend_failed", message, {}});
}

void TerminalSession::writeTerminalResponse(const QByteArray& bytes) {
    const auto result = m_backend->write(bytes);

    if (!result.hasValue()) {
        emit errorOccurred(result.error());
    }
}

void TerminalSession::updateTitle() {
    m_state.dynamicTitle = m_emulator.title();
    m_state.updatedAt = QDateTime::currentMSecsSinceEpoch();
    emit stateChanged();
}

void TerminalSession::updateWorkingDirectory() {
    QString directory = m_emulator.workingDirectory();
    const QUrl url(directory);

    if (url.isValid() && url.isLocalFile()) {
        directory = url.toLocalFile();
    }

    setWorkingDirectory(directory);
}

void TerminalSession::updateBackendWorkingDirectory(const QString& directory) {
    setWorkingDirectory(directory);
}

void TerminalSession::setWorkingDirectory(const QString& directory) {
    const QFileInfo candidate(directory);

    if (!candidate.isAbsolute() || !candidate.isDir()) {
        return;
    }

    const QString normalized = QDir::cleanPath(candidate.absoluteFilePath());

    if (normalized.isEmpty() || normalized == m_state.cwd) {
        return;
    }

    m_state.cwd = normalized;
    m_state.updatedAt = QDateTime::currentMSecsSinceEpoch();
    emit cwdChanged();
    emit stateChanged();
}

std::unique_ptr<IPtyBackend> PtyBackends::createSystemPtyBackend() {
#ifdef Q_OS_WIN
    return std::make_unique<ConPtyBackend>();
#else
    return std::make_unique<PosixPtyBackend>();
#endif
}

} // namespace workpane::terminalcore
