#pragma once

#include "Error.h"
#include "Result.h"
#include "app/CommandLineOptions.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::app {

// The command line accepts only what the product declares, so a mistyped option is refused by name instead of ignored.
// Its help and its refusals speak the language given, which is the language of the system since nothing stored is read before the product opens.
class CommandLine final {
  public:
    [[nodiscard]] static Result<CommandLineOptions> parse(const std::vector<std::string>& arguments);
    [[nodiscard]] static std::string usage(std::string_view language);
    [[nodiscard]] static std::string refusal(const Error& error, std::string_view language);

  private:
    static constexpr std::string_view dataOption{"--data-dir"};
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 4> refusalKeys{{{"command_line_unknown", "workpane.command-line.unknown"}, {"command_line_value_missing", "workpane.command-line.value-missing"}, {"command_line_repeated", "workpane.command-line.repeated"}, {"data_directory_relative", "workpane.command-line.relative"}}};

    [[nodiscard]] static std::string translate(std::string_view language, std::string_view key, const std::vector<std::string>& arguments);
};

} // namespace workpane::app
