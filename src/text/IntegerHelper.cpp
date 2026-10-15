#include "text/IntegerHelper.h"

#include <charconv>
#include <system_error>

namespace workpane::text {

// The whole text, spaces around it aside, is the number, so a value followed by anything else is refused.
std::optional<std::int64_t> IntegerHelper::parse(std::string_view text, int base) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");

    if (first == std::string_view::npos) {
        return std::nullopt;
    }

    text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);

    if (base == 16 && (text.starts_with("0x") || text.starts_with("0X"))) {
        text.remove_prefix(2);
    }

    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);

    if (error != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }

    return value;
}

// The number that opens a text, such as the amount before the unit of a line of the system, whatever follows it.
std::optional<std::int64_t> IntegerHelper::leading(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t");

    if (first == std::string_view::npos) {
        return std::nullopt;
    }

    text.remove_prefix(first);
    const std::size_t digits = text.find_first_not_of("0123456789");

    return parse(text.substr(0, digits));
}

} // namespace workpane::text
