#pragma once

#include "plugins/PluginInterface.h"
#include "terminal/TerminalSession.h"
#include "ui/Components.h"
#include "ui/SessionDrag.h"

#include <QFrame>
#include <QPoint>
#include <QPointer>

class QAction;
class QLabel;
class QToolButton;
class QWidget;

namespace workpane::ui {
class TerminalWidget;
}

namespace workpane::plugins::terminalplugin {

class TerminalPane final : public QFrame, public SessionDragSource {
    Q_OBJECT

  public:
    explicit TerminalPane(terminalcore::TerminalSession& session, plugins::PluginHost& host, QWidget* parent = nullptr);

    [[nodiscard]] QString sessionId() const;
    [[nodiscard]] QString draggedSessionId() const override;
    void setDropDestination(SessionDropDestination destination) override;
    void setSelected(bool isSelected);
    void setFocusMode(bool focusMode);
    void focusTerminal();
    void deactivate();
    void setTerminalFont(const QString& family, int size);
    void setConfirmMultilinePaste(bool enabled);

  signals:
    void selected(const QString& sessionId);
    void closeRequested(const QString& sessionId, const QString& name);
    void focusModeRequested(const QString& sessionId);
    void shelfRequested(const QString& sessionId);
    void slotDropRequested(const QString& sessionId, int slotIndex);
    void interactionError(const Error& error);

  protected:
    [[nodiscard]] bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private slots:
    void selectSession();
    void requestClose();
    void requestFocusMode();
    void showActionsMenu();
    void handleAction(QAction* action);
    void openAddress(const QString& address);
    void offerDirectory(const QString& capability);
    void updateSessionDetails();
    void updateHeaderVisibility();

  private:
    void beginDrag();

    QPointer<terminalcore::TerminalSession> m_session;
    plugins::PluginHost& m_host;
    QString m_sessionId;
    SessionDropDestination m_dropDestination;
    QWidget* m_header{nullptr};
    ui::StatusIndicator* m_focusIndicator{nullptr};
    QLabel* m_nameLabel{nullptr};
    QLabel* m_shellLabel{nullptr};
    QLabel* m_bellIndicator{nullptr};
    QToolButton* m_focusButton{nullptr};
    QToolButton* m_actionsButton{nullptr};
    ui::TerminalWidget* m_terminal{nullptr};
    QWidget* m_exitBar{nullptr};
    QLabel* m_exitLabel{nullptr};
    QPoint m_dragOrigin;
    bool m_dragStarted{false};
    bool m_selected{false};
};

} // namespace workpane::plugins::terminalplugin
