#include "app/CommandLine.h"

#include "localization/CoreCatalog.h"
#include "localization/Localization.h"

#include <tuple>

namespace workpane::app {

// A data directory is absolute, so the same argument names the same place whichever directory the product was started from, and it is written after a space or an equals sign.
Result<CommandLineOptions> CommandLine::parse(const std::vector<std::string>& arguments) {
    CommandLineOptions options;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];

        if (argument == "--help" || argument == "-h") {
            options.help = true;
            continue;
        }

        if (argument == "--version" || argument == "-v") {
            options.version = true;
            continue;
        }

        const bool joined = argument.starts_with(std::string(dataOption) + "=");

        if (argument != dataOption && !joined) {
            return Result<CommandLineOptions>::failure({"command_line_unknown", "The command line carries an option the product does not declare", argument});
        }

        if (!joined && (index + 1 >= arguments.size() || arguments[index + 1].empty())) {
            return Result<CommandLineOptions>::failure({"command_line_value_missing", "An option was given without its value", argument});
        }

        const std::string value = joined ? argument.substr(dataOption.size() + 1) : arguments[++index];

        if (value.empty()) {
            return Result<CommandLineOptions>::failure({"command_line_value_missing", "An option was given without its value", std::string(dataOption)});
        }

        if (options.dataDirectory.has_value()) {
            return Result<CommandLineOptions>::failure({"command_line_repeated", "An option was given more than once", std::string(dataOption)});
        }

        const std::filesystem::path directory(std::u8string(value.begin(), value.end()));

        if (!directory.is_absolute()) {
            return Result<CommandLineOptions>::failure({"data_directory_relative", "The data directory must be an absolute path", value});
        }

        options.dataDirectory = directory.lexically_normal();
    }

    return Result<CommandLineOptions>::success(std::move(options));
}

std::string CommandLine::usage(std::string_view language) {
    return translate(language, "workpane.command-line.usage", {});
}

// A refusal names the option or the value it refused, and one the catalog does not know keeps its diagnostic.
std::string CommandLine::refusal(const Error& error, std::string_view language) {
    for (const auto& [code, key] : refusalKeys) {
        if (code == error.code) {
            return translate(language, key, {error.detail});
        }
    }

    return error.message + "\n" + error.detail;
}

std::string CommandLine::translate(std::string_view language, std::string_view key, const std::vector<std::string>& arguments) {
    localization::Localization localization;
    std::ignore = localization.registerCatalog(localization::Localization::coreOwner, localization::CoreCatalog::catalog());
    std::ignore = localization.selectLanguage(std::string(language));

    return localization.translate(key, arguments);
}

} // namespace workpane::app
