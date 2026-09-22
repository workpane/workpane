#pragma once

#include "domain/Result.h"
#include "plugins/PluginInterface.h"

#include <QString>

namespace workpane::plugins::terminalplugin {

// A notification carries a sentence of the catalog while a fault of the engine stays in the log.
class TerminalFailures final {
  public:
    [[nodiscard]] static QString terminalFailureMessage(const Error& error, PluginHost& host);
};

} // namespace workpane::plugins::terminalplugin
