#pragma once

#include "CodeEditorState.h"
#include "plugins/PluginInterface.h"

namespace workpane::plugins::codeeditor {

class CodeEditorRepository final {
  public:
    explicit CodeEditorRepository(PluginHost& host);

    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] Result<QVector<CodeWorkspaceState>> load() const;
    [[nodiscard]] CodeEditorSettings loadSettings() const;
    [[nodiscard]] QFuture<Result<void>> save(const QVector<CodeWorkspaceState>& workspaces);
    [[nodiscard]] QFuture<Result<void>> saveSettings(const CodeEditorSettings& settings);

  private:
    PluginHost& m_host;
};

} // namespace workpane::plugins::codeeditor
