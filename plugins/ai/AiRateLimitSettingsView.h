#pragma once

#include "AiProviderScope.h"
#include "AiTaskRepository.h"
#include "plugins/PluginInterface.h"

#include <QWidget>

class QSpinBox;

namespace workpane::plugins::ai {

class AiPlugin;

class AiRateLimitSettingsView final : public QWidget {
    Q_OBJECT

  public:
    AiRateLimitSettingsView(AiPlugin& plugin, PluginHost& host, AiProviderScope& scope, QWidget* parent);

  private:
    void applyProvider();
    void persist();

    AiPlugin& m_plugin;
    PluginHost& m_host;
    AiProviderScope& m_scope;
    QSpinBox* m_interval{nullptr};
    QSpinBox* m_perMinute{nullptr};
    QSpinBox* m_concurrent{nullptr};
    bool m_loading{false};
};

} // namespace workpane::plugins::ai
