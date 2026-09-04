#pragma once

#include "AiModelConnection.h"
#include "AiModelDiscovery.h"
#include "plugins/PluginInterface.h"

#include <QDialog>
#include <QHash>
#include <QStringList>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace workpane::ui {
class SecretField;
}

namespace workpane::plugins::ai {

// One configured provider and model pair, which is what a task selects by key.
class AiConnectionDialog final : public QDialog {
    Q_OBJECT

  public:
    AiConnectionDialog(PluginHost& host, const ModelConnection& connection, QStringList takenKeys, QWidget* parent);

    [[nodiscard]] ModelConnection connection() const;
    void accept() override;

  private:
    void applyProviderShape();
    void selectProvider();
    void selectModel();
    void rebuildModels();
    void refreshModels();
    void applyDiscoveredModels(const QStringList& models);
    void clearParameterForm();
    void rebuildParameters();
    void validateExtraParameters();
    [[nodiscard]] const ProviderDescriptor* selectedProvider() const;
    [[nodiscard]] QJsonObject collectParameters() const;
    [[nodiscard]] QString validationMessage(const Error& error) const;
    void showValidation(const QString& message);

    PluginHost& m_host;
    ModelConnection m_original;
    QStringList m_takenKeys;
    AiModelDiscovery m_discovery;
    QStringList m_discoveredModels;
    QComboBox* m_provider{nullptr};
    QComboBox* m_model{nullptr};
    QToolButton* m_refreshModels{nullptr};
    QLineEdit* m_displayName{nullptr};
    ui::SecretField* m_apiKey{nullptr};
    QLineEdit* m_address{nullptr};
    QFormLayout* m_form{nullptr};
    QFormLayout* m_parameterForm{nullptr};
    QWidget* m_extraSection{nullptr};
    QPlainTextEdit* m_extraParameters{nullptr};
    QLabel* m_extraValidation{nullptr};
    QLabel* m_validation{nullptr};
    QPushButton* m_save{nullptr};
    QHash<QString, QWidget*> m_parameterEditors;
    int m_apiKeyRow{-1};
    int m_addressRow{-1};
    bool m_loading{false};
};

} // namespace workpane::plugins::ai
