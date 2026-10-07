#pragma once

#include "Result.h"
#include "localization/Language.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::localization {

using TranslationEntries = std::map<std::string, std::string, std::less<>>;
using TranslationCatalog = std::map<std::string, TranslationEntries, std::less<>>;

class Localization final {
  public:
    static constexpr std::string_view coreOwner{"workpane"};

    [[nodiscard]] Result<void> registerCatalog(std::string_view owner, const TranslationCatalog& catalog);
    void unregisterCatalog(std::string_view owner);
    [[nodiscard]] Result<void> selectLanguage(std::string_view languageId);
    [[nodiscard]] const std::string& language() const;
    [[nodiscard]] std::string translate(std::string_view key) const;
    [[nodiscard]] std::string translate(std::string_view key, std::span<const std::string> arguments) const;
    [[nodiscard]] std::string formatNumber(double value, int decimals) const;
    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::uint64_t generation() const;

    [[nodiscard]] static const std::vector<Language>& languages();
    [[nodiscard]] static bool supportedLanguage(std::string_view languageId);
    [[nodiscard]] static std::string normalizeLocale(std::string_view locale);
    [[nodiscard]] static std::string resolveLanguage(std::string_view systemLocale);
    [[nodiscard]] static bool validKey(std::string_view key);
    [[nodiscard]] static bool validLocale(std::string_view locale);
    [[nodiscard]] static std::set<int> placeholders(std::string_view text);
    [[nodiscard]] static std::string substitute(std::string_view text, std::span<const std::string> arguments);

  private:
    [[nodiscard]] static bool lowercaseWord(std::string_view text, bool allowHyphen);
    [[nodiscard]] static bool validOwner(std::string_view owner);
    [[nodiscard]] static std::string baseLanguage(std::string_view locale);

    [[nodiscard]] const std::string* lookup(std::string_view key) const;

    std::map<std::string, TranslationEntries, std::less<>> m_locales;
    std::map<std::string, std::string, std::less<>> m_owners;
    std::string m_language{"en"};
    std::uint64_t m_generation{1};
};

} // namespace workpane::localization
