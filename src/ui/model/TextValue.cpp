#include "ui/model/TextValue.h"

#include "localization/Localization.h"

#include <utility>
#include <vector>

namespace workpane::ui {

Result<TextValue> TextValue::parse(const json::Json& value, std::string_view context) {
    if (value.is_string()) {
        return Result<TextValue>::success(literal(value.get<std::string>()));
    }

    std::string key;
    const json::Json* values = &json::ObjectReader::emptyList();
    json::ObjectReader reader(value, std::string(context));
    reader.readText("key", key).readArray("args", values, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return Result<TextValue>::failure(finished.error());
    }

    if (!localization::Localization::validKey(key)) {
        return Result<TextValue>::failure({"ui_text_key_invalid", "A text names an invalid translation key", key});
    }

    std::vector<localization::TextArgument> arguments;

    for (const auto& entry : *values) {
        auto argument = localization::TextArgument::parse(entry, std::string(context) + ".args");

        if (!argument.hasValue()) {
            return Result<TextValue>::failure(argument.error());
        }

        arguments.push_back(std::move(argument.value()));
    }

    return Result<TextValue>::success(translated(std::move(key), std::move(arguments)));
}

TextValue TextValue::literal(std::string text) {
    TextValue value;
    value.m_text = std::move(text);

    return value;
}

TextValue TextValue::translated(std::string key, std::vector<localization::TextArgument> arguments) {
    TextValue value;
    value.m_key = std::move(key);
    value.m_arguments = std::move(arguments);

    return value;
}

const std::string& TextValue::resolve(const localization::Localization& localization) const {
    if (m_key.empty()) {
        return m_text;
    }

    // The translated sentence is kept until the catalogs or the language change, so drawing a frame never formats it again.
    if (m_generation != localization.generation()) {
        std::vector<std::string> arguments;

        for (const auto& argument : m_arguments) {
            arguments.push_back(argument.resolve(localization));
        }

        m_resolved = localization.translate(m_key, arguments);
        m_generation = localization.generation();
    }

    return m_resolved;
}

bool TextValue::empty() const {
    return m_key.empty() && m_text.empty();
}

} // namespace workpane::ui
