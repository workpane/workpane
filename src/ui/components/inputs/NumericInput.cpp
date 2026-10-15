#include "ui/components/inputs/NumericInput.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace workpane::ui {

// A number is written and read with a point before its decimals whatever the locale of the process, so a field always reads back what it wrote.
std::string NumericInput::format(double value, std::int64_t decimals) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(static_cast<int>(decimals)) << value;

    return stream.str();
}

bool NumericInput::parse(const std::string& text, double& value) {
    const auto first = text.find_first_not_of(' ');
    const auto last = text.find_last_not_of(' ');

    if (first == std::string::npos) {
        return false;
    }

    std::istringstream stream(text.substr(first, last - first + 1));
    stream.imbue(std::locale::classic());
    stream >> value;

    return !stream.fail() && stream.peek() == std::char_traits<char>::eof() && std::isfinite(value);
}

// A step finer than the decimals of its field could never move the value it writes, so it is refused with the bounds.
Result<void> NumericInput::validateRange(double value, double minimum, double maximum, double step, std::int64_t decimals, std::string_view kind) {
    if (minimum > maximum || step <= 0.0) {
        return Result<void>::failure({"ui_number_bounds", "A number declares bounds that do not form a range or a step that does not advance", std::string(kind)});
    }

    if (std::abs(rounded(step, decimals) - step) > stepTolerance * std::max(1.0, step)) {
        return Result<void>::failure({"ui_number_step", "A number steps by a value its decimals can write", std::string(kind)});
    }

    if (value < minimum || value > maximum) {
        return Result<void>::failure({"ui_number_range", "A number carries a value outside its bounds", std::string(kind)});
    }

    // Bounds and a value the decimals cannot write would round outside the range or away from what the plugin gave.
    for (const double written : {minimum, maximum, value}) {
        if (std::abs(rounded(written, decimals) - written) > stepTolerance * std::max(1.0, std::abs(written))) {
            return Result<void>::failure({"ui_number_decimals", "A number carries a bound or a value its decimals cannot write", std::string(kind)});
        }
    }

    return Result<void>::success();
}

// A value is kept with the decimals its field writes, so arithmetic on steps never leaves a trail of binary noise.
double NumericInput::rounded(double value, std::int64_t decimals) {
    const double scale = std::pow(10.0, static_cast<double>(decimals));
    return std::round(value * scale) / scale;
}

} // namespace workpane::ui
