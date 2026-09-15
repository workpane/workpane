#pragma once

#include "plugins/PluginInterface.h"

#include <QVector>
#include <QWidget>

namespace workpane::ui {

class Theme;
class Toast;

class ToastOverlay final : public QWidget {
    Q_OBJECT

  public:
    explicit ToastOverlay(const Theme& theme, QWidget* host);

    void showNotification(const QString& title, const QString& message, plugins::AlertSeverity severity);
    void applyTheme(const Theme& theme);
    void dismissAll();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void removeToast(Toast* toast);
    void relayout();

    const Theme* m_theme{nullptr};
    QVector<Toast*> m_toasts;
};

} // namespace workpane::ui
