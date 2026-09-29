#include "localization/Localization.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace workpane::localization {

bool Localization::lowercaseWord(std::string_view text, bool allowHyphen) {
    if (text.empty() || text.front() == '-' || text.back() == '-') {
        return false;
    }

    // clang-format off
    return std::ranges::all_of(text, [allowHyphen](char character) { return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || (allowHyphen && character == '-'); });
    // clang-format on
}

bool Localization::validOwner(std::string_view owner) {
    return lowercaseWord(owner, true);
}

std::string Localization::baseLanguage(std::string_view locale) {
    return std::string(locale.substr(0, locale.find('-')));
}

Result<void> Localization::registerCatalog(std::string_view owner, const TranslationCatalog& catalog) {
    if (!validOwner(owner)) {
        return Result<void>::failure({"translation_owner_invalid", "A translation owner identifier is invalid", std::string(owner)});
    }

    if (m_owners.contains(owner)) {
        return Result<void>::failure({"translation_owner_duplicate", "A translation owner registered a second catalog", std::string(owner)});
    }

    for (const auto& language : languages()) {
        if (!catalog.contains(language.id)) {
            return Result<void>::failure({"translation_language_missing", "A catalog does not declare every selectable language", std::string(owner) + ":" + language.id});
        }
    }

    const auto& english = catalog.find("en")->second;
    const std::string prefix = std::string(owner) + ".";

    // Every locale is validated before anything is merged, so a refused catalog leaves no partial trace.
    for (const auto& [locale, entries] : catalog) {
        if (!validLocale(locale)) {
            return Result<void>::failure({"translation_locale_invalid", "A catalog declares an invalid locale", locale});
        }

        for (const auto& [key, text] : entries) {
            if (!validKey(key)) {
                return Result<void>::failure({"translation_key_invalid", "A translation key is invalid", key});
            }

            if (!key.starts_with(prefix)) {
                return Result<void>::failure({"translation_key_foreign", "A translation key belongs to another owner", key});
            }

            if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
                return Result<void>::failure({"translation_value_empty", "A translation value is empty", locale + ":" + key});
            }

            const auto source = english.find(key);

            if (source == english.end()) {
                return Result<void>::failure({"translation_key_unknown", "A translated key does not exist in English", locale + ":" + key});
            }

            if (placeholders(text) != placeholders(source->second)) {
                return Result<void>::failure({"translation_arguments_mismatch", "A translation declares a different number of arguments than English", locale + ":" + key});
            }
        }

        for (const auto& [key, text] : english) {
            if (!entries.contains(key)) {
                return Result<void>::failure({"translation_key_missing", "A language does not spell every key English declares", locale + ":" + key});
            }
        }
    }

    for (const auto& [locale, entries] : catalog) {
        auto& merged = m_locales[locale];
        merged.insert(entries.begin(), entries.end());
    }

    m_owners.emplace(owner, prefix);
    ++m_generation;

    return Result<void>::success();
}

// Removes every sentence an owner registered, which is what a plugin that failed to start leaves behind.
void Localization::unregisterCatalog(std::string_view owner) {
    const auto found = m_owners.find(owner);

    if (found == m_owners.end()) {
        return;
    }

    const std::string prefix = found->second;

    for (auto& [locale, entries] : m_locales) {
        // clang-format off
        std::erase_if(entries, [&prefix](const auto& entry) { return entry.first.starts_with(prefix); });
        // clang-format on
    }

    m_owners.erase(found);
    ++m_generation;
}

Result<void> Localization::selectLanguage(std::string_view languageId) {
    if (!supportedLanguage(languageId)) {
        return Result<void>::failure({"application_language_invalid", "The application language is unsupported", std::string(languageId)});
    }

    m_language = std::string(languageId);
    ++m_generation;

    return Result<void>::success();
}

const std::string& Localization::language() const {
    return m_language;
}

std::string Localization::translate(std::string_view key) const {
    const std::string* text = lookup(key);
    return text == nullptr ? std::string(key) : *text;
}

std::string Localization::translate(std::string_view key, std::span<const std::string> arguments) const {
    const std::string* text = lookup(key);
    return text == nullptr ? std::string(key) : substitute(*text, arguments);
}

bool Localization::contains(std::string_view key) const {
    return lookup(key) != nullptr;
}

// Every change of language or catalog advances the generation, so a cached sentence knows when it is stale.
std::uint64_t Localization::generation() const {
    return m_generation;
}

// A number is rounded half away from zero to its decimals and written with the separators of the current language, grouping its whole part by thousands.
std::string Localization::formatNumber(double value, int decimals) const {
    // clang-format off
    const auto language = std::ranges::find_if(languages(), [this](const Language& candidate) { return candidate.id == m_language; });
    // clang-format on
    const double scale = std::pow(10.0, decimals);
    const double rounded = std::round(std::fabs(value) * scale);
    const std::string plain = std::format("{:.{}f}", rounded / scale, decimals);
    const std::size_t point = plain.find('.');
    const std::string whole = plain.substr(0, point);
    std::string written = value < 0.0 && rounded != 0.0 ? "-" : "";

    for (std::size_t index = 0; index < whole.size(); ++index) {
        if (index > 0 && (whole.size() - index) % 3 == 0) {
            written += language->groupSeparator;
        }

        written += whole[index];
    }

    if (point != std::string::npos) {
        written += language->decimalSeparator;
        written += plain.substr(point + 1);
    }

    return written;
}

const std::vector<Language>& Localization::languages() {
    static const std::vector<Language> selectable{{"en", "workpane.language.english", '.', ','}, {"pt", "workpane.language.portuguese", ',', '.'}};
    return selectable;
}

bool Localization::supportedLanguage(std::string_view languageId) {
    // clang-format off
    return std::ranges::any_of(languages(), [languageId](const Language& language) { return language.id == languageId; });
    // clang-format on
}

std::string Localization::normalizeLocale(std::string_view locale) {
    const std::string_view withoutEncoding = locale.substr(0, locale.find_first_of(".@"));
    std::string normalized;
    normalized.reserve(withoutEncoding.size());

    for (const char character : withoutEncoding) {
        normalized.push_back(character == '_' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }

    return normalized;
}

// The complete system locale is tried first, then its base language and then English.
std::string Localization::resolveLanguage(std::string_view systemLocale) {
    const std::string normalized = normalizeLocale(systemLocale);

    if (supportedLanguage(normalized)) {
        return normalized;
    }

    const std::string base = baseLanguage(normalized);
    return supportedLanguage(base) ? base : std::string("en");
}

bool Localization::validKey(std::string_view key) {
    const auto first = key.find('.');
    const auto second = first == std::string_view::npos ? std::string_view::npos : key.find('.', first + 1);

    if (first == std::string_view::npos || second == std::string_view::npos || key.find('.', second + 1) != std::string_view::npos) {
        return false;
    }

    return lowercaseWord(key.substr(0, first), true) && lowercaseWord(key.substr(first + 1, second - first - 1), true) && lowercaseWord(key.substr(second + 1), true);
}

bool Localization::validLocale(std::string_view locale) {
    const auto separator = locale.find('-');
    const std::string_view language = locale.substr(0, separator);

    if (language.size() < 2 || language.size() > 3 || !lowercaseWord(language, false)) {
        return false;
    }

    if (separator == std::string_view::npos) {
        return true;
    }

    const std::string_view region = locale.substr(separator + 1);
    return region.size() >= 2 && region.size() <= 8 && lowercaseWord(region, false);
}

// Answers the numbers of the arguments a text writes, each once, so two languages are compared by the arguments they spell rather than by how many.
std::set<int> Localization::placeholders(std::string_view text) {
    std::set<int> numbers;

    for (std::size_t index = 0; index + 1 < text.size(); ++index) {
        if (text[index] == '%' && text[index + 1] >= '1' && text[index + 1] <= '9') {
            numbers.insert(text[index + 1] - '0');
        }
    }

    return numbers;
}

std::string Localization::substitute(std::string_view text, std::span<const std::string> arguments) {
    std::string result;
    result.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        const bool placeholder = text[index] == '%' && index + 1 < text.size() && text[index + 1] >= '1' && text[index + 1] <= '9';
        const auto argument = placeholder ? static_cast<std::size_t>(text[index + 1] - '1') : std::size_t{0};

        if (placeholder && argument < arguments.size()) {
            result += arguments[argument];
            ++index;
            continue;
        }

        result.push_back(text[index]);
    }

    return result;
}

// The selected language is tried first, then its base language and then English, and a key nobody declares answers nothing.
const std::string* Localization::lookup(std::string_view key) const {
    const std::string base = baseLanguage(m_language);

    for (const std::string_view candidate : {std::string_view(m_language), std::string_view(base), std::string_view("en")}) {
        const auto locale = m_locales.find(candidate);

        if (locale == m_locales.end()) {
            continue;
        }

        const auto entry = locale->second.find(key);

        if (entry != locale->second.end()) {
            return &entry->second;
        }
    }

    return nullptr;
}

} // namespace workpane::localization
