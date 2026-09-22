#pragma once

#include <QDialog>

namespace workpane::ui {

class ConfirmationDialog final : public QDialog {
    Q_OBJECT

  public:
    ConfirmationDialog(const QString& title, const QString& message, const QString& cancelText, const QString& confirmText, bool destructive, QWidget* parent = nullptr);

    [[nodiscard]] static bool confirm(QWidget* parent, const QString& windowTitle, const QString& title, const QString& message, const QString& cancelText, const QString& confirmText, bool destructive = false);
};

} // namespace workpane::ui
