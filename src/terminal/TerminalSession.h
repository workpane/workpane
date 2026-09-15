#pragma once

#include "domain/Result.h"
#include "domain/TerminalSessionState.h"
#include "terminal/GhosttyTerminalAdapter.h"
#include "terminal/ShellProfile.h"
#include "terminal/platform/IPtyBackend.h"

#include <QByteArray>
#include <QObject>
#include <QPointF>
#include <QTimer>

#include <functional>
#include <memory>

namespace workpane::terminalcore {

using PtyBackendFactory = std::function<std::unique_ptr<IPtyBackend>()>;

class TerminalSession final : public QObject {
    Q_OBJECT

  public:
    explicit TerminalSession(domain::TerminalSessionState state, ShellProfile profile, domain::TerminalTheme theme, std::unique_ptr<IPtyBackend> backend, QObject* parent = nullptr);
    ~TerminalSession() override;

    [[nodiscard]] const QString& id() const;
    [[nodiscard]] const QString& name() const;
    [[nodiscard]] const QString& cwd() const;
    [[nodiscard]] const QString& shellName() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] int exitCode() const;
    [[nodiscard]] const domain::TerminalSessionState& state() const;
    [[nodiscard]] TerminalRenderSnapshot snapshot();

    void setName(const QString& newName);
    void setTheme(const domain::TerminalTheme& theme);
    void setClipboardWriteAllowed(bool allowed);
    [[nodiscard]] Result<void> start();
    [[nodiscard]] Result<void> write(const QByteArray& bytes);
    [[nodiscard]] Result<void> writeLocalPaths(const QStringList& paths);
    [[nodiscard]] Result<void> sendKey(const QKeyEvent& event);
    [[nodiscard]] Result<void> paste(const QByteArray& text);
    [[nodiscard]] bool pasteExecutesOnArrival(const QByteArray& text) const;
    [[nodiscard]] bool programWantsMouse() const;
    [[nodiscard]] bool programWantsFocus() const;
    [[nodiscard]] Result<void> sendMouse(const MouseReport& report);
    [[nodiscard]] Result<void> sendFocus(bool gained);
    [[nodiscard]] Result<void> beginSelection(const QPointF& position, quint64 timeNanoseconds, quint64 repeatIntervalNanoseconds, double repeatDistance, bool rectangle);
    [[nodiscard]] Result<void> extendSelection(const QPointF& position, bool rectangle);
    void endSelection(const QPointF& position);
    [[nodiscard]] SelectionAutoscroll selectionAutoscroll() const;
    [[nodiscard]] Result<void> advanceSelectionAutoscroll(const QPointF& position, bool rectangle);
    void selectAll();
    void clearSelection();
    void clearScrollback();
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] QString selectionText() const;
    [[nodiscard]] QString addressAt(const QPointF& position) const;
    [[nodiscard]] QList<SearchMatch> search(const QString& query, bool caseSensitive, bool wholeWord, int maximum) const;
    void revealMatch(const SearchMatch& match);
    [[nodiscard]] Result<void> resize(int columns, int rows, int cellWidth, int cellHeight);
    void scrollViewport(qint64 rows);
    void scrollToRow(quint64 row);
    void scrollToTop();
    void scrollToBottom();
    void restart();
    void terminate();

  signals:
    void nameChanged();
    void cwdChanged();
    void statusChanged();
    void renderChanged();
    void bellRang();
    void clipboardWriteRequested(const QString& text);
    void notificationPosted(const QString& title, const QString& body);
    void stateChanged();
    void errorOccurred(const Error& error);

  private slots:
    void processOutput(const QByteArray& bytes);
    void processExited(int processExitCode);
    void processError(const QString& message);
    void writeTerminalResponse(const QByteArray& bytes);
    void updateTitle();
    void updateWorkingDirectory();
    void updateBackendWorkingDirectory(const QString& directory);
    void flushOutput();

  private:
    void setWorkingDirectory(const QString& directory);

    domain::TerminalSessionState m_state;
    ShellProfile m_profile;
    domain::TerminalTheme m_theme;
    std::unique_ptr<IPtyBackend> m_backend;
    GhosttyTerminalAdapter m_emulator;
    int m_columns{80};
    int m_rows{24};
    int m_cellWidth{8};
    int m_cellHeight{16};
    int m_exitCode{-1};
    QByteArray m_pendingOutput;
    QTimer m_outputTimer;
    bool m_outputPaused{false};
};

class PtyBackends final {
  public:
    [[nodiscard]] static std::unique_ptr<IPtyBackend> createSystemPtyBackend();
};

} // namespace workpane::terminalcore
