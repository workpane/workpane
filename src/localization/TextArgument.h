#pragma once

#include "Result.h"
#include "json/ObjectReader.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::localization {

class Localization;

// An argument of a translated sentence: a text written as it is, a number written in the language being read or another translated sentence.
class TextArgument final {
  public:
    static constexpr int maximumDepth{4};

    [[nodiscard]] static Result<TextArgument> parse(const json::Json& value, std::string_view context, int depth = 0);

    [[nodiscard]] std::string resolve(const Localization& localization) const;

  private:
    static constexpr int maximumDecimals{6};

    [[nodiscard]] static TextArgument literal(std::string text);
    [[nodiscard]] static TextArgument number(double value, int decimals);

    std::string m_text;
    std::optional<double> m_number;
    int m_decimals{0};
    std::string m_key;
    std::vector<TextArgument> m_arguments;
};

} // namespace workpane::localization
