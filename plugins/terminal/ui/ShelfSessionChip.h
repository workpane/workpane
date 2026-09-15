#pragma once

#include "plugins/PluginInterface.h"
#include "ui/SessionDrag.h"

#include <QFrame>
#include <QPoint>
#include <QString>

class QKeyEvent;
class QMouseEvent;

namespace workpane::plugins::terminalplugin {

class ShelfSessionChip final : public QFrame, public SessionDragSource {
    Q_OBJECT

  public:
    ShelfSessionChip(QString sessionId, const QString& name, plugins::PluginHost& host, QWidget* parent = nullptr);
    [[nodiscard]] QString draggedSessionId() const override;
    void setDropDestination(SessionDropDestination destination) override;

  signals:
    void activated(const QString& sessionId);
    void closeRequested(const QString& sessionId);
    void slotDropRequested(const QString& sessionId, int slotIndex);

  protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

  private slots:
    void requestClose();

  private:
    void beginDrag();

    QString m_sessionId;
    plugins::PluginHost& m_host;
    SessionDropDestination m_dropDestination;
    QPoint m_dragOrigin;
    bool m_leftButtonPressed{false};
    bool m_dragStarted{false};
};

} // namespace workpane::plugins::terminalplugin
