#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

// The environment a terminal starts its shell in: everything the reader configured, with the variables that describe this terminal the same way on every platform and the additions of one terminal over them.
// What a terminal of Workpane exported for its own shell never reaches another shell, so a product started from such a terminal gives its shells the configuration of the reader rather than the integration of that terminal.
class TerminalEnvironment final {
  public:
    [[nodiscard]] static std::vector<std::string> build(const std::vector<std::string>& inherited, const std::vector<std::string>& additions);
    [[nodiscard]] static std::string zshFolder(const std::vector<std::string>& inherited);
    [[nodiscard]] static std::string home(const std::vector<std::string>& inherited);

  private:
    [[nodiscard]] static std::string name(const std::string& entry);
    [[nodiscard]] static std::optional<std::string> value(const std::vector<std::string>& environment, std::string_view variable);
    [[nodiscard]] static std::optional<std::string> readerZshFolder(const std::vector<std::string>& inherited);
};

} // namespace workpane::platform
