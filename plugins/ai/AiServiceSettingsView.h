#pragma once

#include "AiTaskRepository.h"
#include "plugins/PluginInterface.h"

#include <QWidget>

class QComboBox;
class QFormLayout;
class QLineEdit;

namespace workpane::ui {
class SecretField;
}

namespace workpane::plugins::ai {

class AiPlugin;

class AiSearchSettingsView final : public QWidget {
    Q_OBJECT

  public:
    AiSearchSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent);

  private:
    void applyService();
    void persist();

    AiPlugin& m_plugin;
    PluginHost& m_host;
    QFormLayout* m_form{nullptr};
    QComboBox* m_service{nullptr};
    QLineEdit* m_instance{nullptr};
    ui::SecretField* m_apiKey{nullptr};
};

class AiSpeechSettingsView final : public QWidget {
    Q_OBJECT

  public:
    AiSpeechSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent);

  private:
    void applyService();
    void persist();
    [[nodiscard]] QString selectedService() const;
    [[nodiscard]] QString selectedVoice() const;

    AiPlugin& m_plugin;
    PluginHost& m_host;
    QFormLayout* m_form{nullptr};
    QComboBox* m_service{nullptr};
    QComboBox* m_declaredVoice{nullptr};
    QLineEdit* m_voice{nullptr};
    ui::SecretField* m_apiKey{nullptr};
    bool m_loading{false};
};

} // namespace workpane::plugins::ai
