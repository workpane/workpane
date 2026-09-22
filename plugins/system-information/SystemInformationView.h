#pragma once

#include "SystemInformation.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace workpane::plugins {
class PluginHost;
}

namespace workpane::plugins::systeminformation {

class SystemInformationPlugin;

class SystemInformationView final : public QWidget {
  public:
    SystemInformationView(SystemInformationPlugin& plugin, PluginHost& host, QWidget* parent);

  private:
    void render(const SystemSnapshot& snapshot);
    void clearContent();
    void requestRefresh();
    void renderSnapshot();
    void updateRefreshState(bool refreshing);

    SystemInformationPlugin& m_plugin;
    PluginHost& m_host;
    QLabel* m_updated{nullptr};
    QLabel* m_state{nullptr};
    QPushButton* m_refresh{nullptr};
    QWidget* m_content{nullptr};
    QVBoxLayout* m_contentLayout{nullptr};
};

} // namespace workpane::plugins::systeminformation
