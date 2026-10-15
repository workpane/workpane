#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "localization/TextArgument.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::localization {
class Localization;
}

namespace workpane::ui {

// A text is either a literal or a translation key with its arguments, resolved while drawing so a language change needs no plugin work.
class TextValue final {
  public:
    [[nodiscard]] static Result<TextValue> parse(const json::Json& value, std::string_view context);
    [[nodiscard]] static TextValue literal(std::string text);
    [[nodiscard]] static TextValue translated(std::string key, std::vector<localization::TextArgument> arguments = {});

    [[nodiscard]] const std::string& resolve(const localization::Localization& localization) const;
    [[nodiscard]] bool empty() const;

  private:
    std::string m_text;
    std::string m_key;
    std::vector<localization::TextArgument> m_arguments;
    mutable std::string m_resolved;
    mutable std::uint64_t m_generation{0};
};

} // namespace workpane::ui
