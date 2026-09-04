#pragma once

#include "domain/Result.h"
#include "plugins/PluginInterface.h"

#include <QString>

namespace workpane::plugins::codeeditor {

// A notification carries a sentence of the catalog while the diagnostic of the filesystem stays in the log.
class FileSystemFailures final {
  public:
    [[nodiscard]] static QString fileSystemFailureMessage(const Error& error, PluginHost& host);
};

} // namespace workpane::plugins::codeeditor
