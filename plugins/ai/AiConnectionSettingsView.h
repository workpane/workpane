#pragma once

#include "AiModelConnection.h"
#include "plugins/PluginInterface.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace workpane::plugins::ai {

class AiPlugin;

class AiConnectionSettingsView final : public QWidget {
    Q_OBJECT

  public:
    AiConnectionSettingsView(AiPlugin& plugin, PluginHost& host, QWidget* parent);

  private:
    void rebuild();
    void addConnection();
    void editConnection();
    void removeConnection();
    void persist(const QVector<ModelConnection>& connections, const QString& defaultKey);
    [[nodiscard]] int selectedRow() const;
    [[nodiscard]] QStringList takenKeys(int excludedRow) const;
    [[nodiscard]] QString selectedDefaultKey() const;

    AiPlugin& m_plugin;
    PluginHost& m_host;
    QVector<ModelConnection> m_connections;
    QComboBox* m_defaultConnection{nullptr};
    QTableWidget* m_grid{nullptr};
    QLabel* m_empty{nullptr};
    bool m_loading{false};
};

} // namespace workpane::plugins::ai
