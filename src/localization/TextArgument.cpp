#include "localization/TextArgument.h"

#include "localization/Localization.h"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace workpane::localization {

Result<TextArgument> TextArgument::parse(const json::Json& value, std::string_view context, int depth) {
    if (value.is_string()) {
        return Result<TextArgument>::success(literal(value.get<std::string>()));
    }

    // A sentence may be an argument of another one, up to a small depth, so a size or a frequency composes into a longer line.
    if (value.is_object() && value.contains("key")) {
        TextArgument sentence;
        const json::Json* values = &json::ObjectReader::emptyList();
        json::ObjectReader reader(value, std::string(context));
        reader.readText("key", sentence.m_key).readArray("args", values, json::Presence::Optional);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return Result<TextArgument>::failure(finished.error());
        }

        if (depth >= maximumDepth || !Localization::validKey(sentence.m_key)) {
            return Result<TextArgument>::failure({"translation_argument_invalid", "A sentence argument nests too deep or names an invalid key", sentence.m_key});
        }

        for (const auto& entry : *values) {
            auto argument = parse(entry, context, depth + 1);

            if (!argument.hasValue()) {
                return argument;
            }

            sentence.m_arguments.push_back(std::move(argument.value()));
        }

        return Result<TextArgument>::success(std::move(sentence));
    }

    double amount = 0.0;
    std::int64_t decimals = 0;
    json::ObjectReader reader(value, std::string(context));
    reader.readNumber("number", amount, std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max()).readInteger("decimals", decimals, 0, maximumDecimals, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return Result<TextArgument>::failure(finished.error());
    }

    return Result<TextArgument>::success(number(amount, static_cast<int>(decimals)));
}

TextArgument TextArgument::literal(std::string text) {
    TextArgument argument;
    argument.m_text = std::move(text);

    return argument;
}

TextArgument TextArgument::number(double value, int decimals) {
    TextArgument argument;
    argument.m_number = value;
    argument.m_decimals = decimals;

    return argument;
}

std::string TextArgument::resolve(const Localization& localization) const {
    if (!m_key.empty()) {
        std::vector<std::string> arguments;

        for (const auto& argument : m_arguments) {
            arguments.push_back(argument.resolve(localization));
        }

        return localization.translate(m_key, arguments);
    }

    return m_number.has_value() ? localization.formatNumber(*m_number, m_decimals) : m_text;
}

} // namespace workpane::localization
