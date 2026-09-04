#pragma once

#include "plugins/PluginManager.h"

#include <QWidget>

#include <functional>

class QLineEdit;
class QLabel;
class QListWidget;
class QWidget;
class QStackedWidget;

namespace workpane::ui {

using SettingsSectionFactory = std::function<QWidget*(const QString&, const QString&, QWidget*)>;

struct CoreSettingsContribution final {
    plugins::SettingsGroup group;
    SettingsSectionFactory createSection;
};

class SettingsView final : public QWidget {
    Q_OBJECT

  public:
    explicit SettingsView(plugins::PluginManager& pluginManager, QVector<CoreSettingsContribution> coreSettings = {}, QWidget* parent = nullptr);
    [[nodiscard]] bool isValid() const;

  private slots:
    void filterSettings(const QString& query);
    void selectPlugin(int row);

  private:
    [[nodiscard]] bool appendGroup(const plugins::SettingsGroup& group, const SettingsSectionFactory& createSection);

    plugins::PluginManager& m_pluginManager;
    QListWidget* m_plugins{nullptr};
    QStackedWidget* m_pages{nullptr};
    QLabel* m_noResults{nullptr};
    bool m_valid{true};
};

} // namespace workpane::ui
