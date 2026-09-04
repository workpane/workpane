#include "ui/ConfirmationDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace workpane::ui {

ConfirmationDialog::ConfirmationDialog(const QString& title, const QString& message, const QString& cancelText, const QString& confirmText, bool destructive, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("confirmationDialog"));
    setModal(true);
    setMinimumWidth(440);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(8);
    auto* titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("confirmationTitle"));
    titleLabel->setWordWrap(true);
    auto* messageLabel = new QLabel(message, this);
    messageLabel->setObjectName(QStringLiteral("confirmationMessage"));
    messageLabel->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(this);
    auto* cancelButton = buttons->addButton(cancelText, QDialogButtonBox::RejectRole);
    auto* confirmButton = buttons->addButton(confirmText, QDialogButtonBox::AcceptRole);
    cancelButton->setObjectName(QStringLiteral("cancelButton"));
    confirmButton->setObjectName(destructive ? QStringLiteral("destructiveButton") : QStringLiteral("primaryButton"));
    cancelButton->setDefault(true);
    cancelButton->setFocus();

    root->addWidget(titleLabel);
    root->addWidget(messageLabel);
    root->addSpacing(10);
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool ConfirmationDialog::confirm(QWidget* parent, const QString& windowTitle, const QString& title, const QString& message, const QString& cancelText, const QString& confirmText, bool destructive) {
    ConfirmationDialog dialog(title, message, cancelText, confirmText, destructive, parent);
    dialog.setWindowTitle(windowTitle);
    return dialog.exec() == QDialog::Accepted;
}

} // namespace workpane::ui
