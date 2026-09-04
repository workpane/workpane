#pragma once

#include "AiTaskRepository.h"
#include "plugins/PluginInterface.h"
#include "ui/Components.h"

#include <QColor>
#include <QJsonObject>
#include <QWidget>

#include <optional>

class QLabel;
class QPushButton;
class QScrollArea;
class QTextBrowser;
class QVBoxLayout;

namespace workpane::plugins::ai {

class AiPlugin;

class AiConversationView final : public QWidget {
    Q_OBJECT

  public:
    AiConversationView(AiPlugin& plugin, PluginHost& host, QWidget* parent);

    void setTask(const QString& taskId);
    [[nodiscard]] const QString& taskId() const;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    void rebuild();
    void updateRunState();
    void submit();
    void resetConversation();
    void loadOlder();
    void showStreaming(const QString& text);
    void openPendingTurn();
    void closePendingTurn();
    void scrollToNewest();
    ui::MarkdownView* appendBubble(const ConversationMessage& message, bool pending = false);
    void appendToolRow(QWidget* bubble, const QJsonObject& call, const QColor& ink);
    [[nodiscard]] int bubbleWidth() const;
    void applyBubbleWidths();
    void growComposer();

    AiPlugin& m_plugin;
    PluginHost& m_host;
    QString m_taskId;
    QWidget* m_pendingRow{nullptr};
    QScrollArea* m_scroll{nullptr};
    QWidget* m_messages{nullptr};
    QVBoxLayout* m_messagesLayout{nullptr};
    QLabel* m_empty{nullptr};
    ui::TextField* m_composer{nullptr};
    QPushButton* m_send{nullptr};
    ui::MarkdownView* m_streaming{nullptr};
    int m_fontSize{0};
    std::optional<ConversationRole> m_lastRole;
    bool m_loadingOlder{false};
    bool m_followNewest{true};
    bool m_stickToEnd{true};
};

} // namespace workpane::plugins::ai
