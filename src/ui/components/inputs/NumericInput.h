#pragma once

#include "Result.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace workpane::ui {

// The text form, the reading and the range rules the numeric inputs share.
class NumericInput final {
  public:
    static constexpr double largestMagnitude{1.0e15};
    static constexpr double stepTolerance{1.0e-9};

    [[nodiscard]] static std::string format(double value, std::int64_t decimals);
    [[nodiscard]] static bool parse(const std::string& text, double& value);
    [[nodiscard]] static Result<void> validateRange(double value, double minimum, double maximum, double step, std::int64_t decimals, std::string_view kind);
    [[nodiscard]] static double rounded(double value, std::int64_t decimals);
};

} // namespace workpane::ui
