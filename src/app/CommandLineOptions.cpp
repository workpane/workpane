#include "app/CommandLineOptions.h"

#include "platform/PathTextHelper.h"
namespace workpane::app {

std::vector<std::string> CommandLineOptions::arguments() const {
    if (!dataDirectory.has_value()) {
        return {};
    }

    return {"--data-dir", platform::PathTextHelper::utf8(*dataDirectory)};
}

} // namespace workpane::app
