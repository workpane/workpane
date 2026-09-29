#include "app/CommandLineOptions.h"

#include "platform/PathText.h"
namespace workpane::app {

std::vector<std::string> CommandLineOptions::arguments() const {
    if (!dataDirectory.has_value()) {
        return {};
    }

    return {"--data-dir", platform::PathText::utf8(*dataDirectory)};
}

} // namespace workpane::app
