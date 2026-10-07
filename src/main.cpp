#include "BuildInfo.h"
#include "app/Application.h"
#include "app/CommandLine.h"
#include "localization/Localization.h"
#include "platform/NativeDialogService.h"
#include "platform/NativeSystemServices.h"
#include "platform/ParentConsole.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// The command line is read in the language of the system, and asking for the help or the version answers on the console without opening a window.
int main(int argc, char** argv) {
    const std::vector<std::string> arguments(argv + 1, argv + argc);
    const std::string language = workpane::localization::Localization::resolveLanguage(workpane::platform::NativeSystemServices().locale());
    auto options = workpane::app::CommandLine::parse(arguments);

    if (!options.hasValue()) {
        workpane::platform::NativeDialogService::alertBlocking("Workpane", workpane::app::CommandLine::refusal(options.error(), language));
        return 2;
    }

    if (options.value().help) {
        workpane::platform::ParentConsole::attach();
        std::fputs(workpane::app::CommandLine::usage(language).c_str(), stdout);
        return 0;
    }

    if (options.value().version) {
        workpane::platform::ParentConsole::attach();
        std::printf("Workpane %s\n", std::string(workpane::app::BuildInfo::version).c_str());
        return 0;
    }

    workpane::app::Application application(std::move(options.value()));
    return application.run();
}
