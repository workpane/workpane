#pragma once

#include "domain/Workspace.h"
#include "plugins/PluginInterface.h"

namespace workpane::plugins::terminalplugin {

class TerminalWorkspaceRepository final {
  public:
    explicit TerminalWorkspaceRepository(PluginHost& host);

    [[nodiscard]] Result<void> saveInitial(const domain::Workspace& workspace);
    [[nodiscard]] QFuture<Result<void>> save(const domain::Workspace& workspace);
    [[nodiscard]] Result<domain::Workspace> loadLastOpened() const;

  private:
    PluginHost& m_host;
};

} // namespace workpane::plugins::terminalplugin
