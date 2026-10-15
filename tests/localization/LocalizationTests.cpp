#include "localization/CoreCatalog.h"
#include "localization/Localization.h"
#include "localization/TextArgument.h"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <clocale>
#include <set>
#include <string>
#include <vector>

namespace workpane::localization {

TEST(Localization, ValidatesKeysAndLocales) {
    EXPECT_TRUE(Localization::validKey("workpane.settings.title"));
    EXPECT_TRUE(Localization::validKey("code-editor.find.find-all"));
    EXPECT_FALSE(Localization::validKey("workpane.settings"));
    EXPECT_FALSE(Localization::validKey("workpane.settings.title.extra"));
    EXPECT_FALSE(Localization::validKey("Workpane.settings.title"));
    EXPECT_FALSE(Localization::validKey("workpane..title"));
    EXPECT_TRUE(Localization::validLocale("en"));
    EXPECT_TRUE(Localization::validLocale("pt-br"));
    EXPECT_FALSE(Localization::validLocale("pt-BR"));
    EXPECT_FALSE(Localization::validLocale("english"));
}

TEST(Localization, ResolvesTheSystemLocaleToAnOfferedLanguage) {
    EXPECT_EQ(Localization::normalizeLocale("pt_BR.UTF-8"), "pt-br");
    EXPECT_EQ(Localization::normalizeLocale("en_US@calendar=gregorian"), "en-us");
    EXPECT_EQ(Localization::resolveLanguage("pt_BR.UTF-8"), "pt");
    EXPECT_EQ(Localization::resolveLanguage("en_GB"), "en");
    EXPECT_EQ(Localization::resolveLanguage("ja_JP"), "en");
    EXPECT_EQ(Localization::resolveLanguage(""), "en");
    EXPECT_TRUE(Localization::supportedLanguage("pt"));
    EXPECT_FALSE(Localization::supportedLanguage("fr"));
}

TEST(Localization, SubstitutesNumberedArguments) {
    const std::vector<std::string> arguments{"Logs", "3"};

    EXPECT_EQ(Localization::placeholders("%2 has %1 entries of %2"), (std::set<int>{1, 2}));
    EXPECT_TRUE(Localization::placeholders("No arguments").empty());
    EXPECT_EQ(Localization::substitute("%1 has %2 entries", arguments), "Logs has 3 entries");
    EXPECT_EQ(Localization::substitute("%2 before %1", arguments), "3 before Logs");
}

TEST(Localization, LooksUpTheLanguageThenEnglishThenTheKey) {
    Localization localization;
    const TranslationCatalog catalog{{"en", {{"sample.view.title", "Title"}, {"sample.view.only-english", "Only English"}}}, {"pt", {{"sample.view.title", "Título"}, {"sample.view.only-english", "Somente inglês"}}}};

    ASSERT_TRUE(localization.registerCatalog("sample", catalog).hasValue());
    ASSERT_TRUE(localization.selectLanguage("pt").hasValue());
    EXPECT_EQ(localization.translate("sample.view.title"), "Título");
    EXPECT_EQ(localization.translate("sample.view.missing"), "sample.view.missing");
    EXPECT_EQ(localization.language(), "pt");

    const auto before = localization.generation();
    ASSERT_TRUE(localization.selectLanguage("en").hasValue());
    EXPECT_GT(localization.generation(), before);
    EXPECT_EQ(localization.translate("sample.view.title"), "Title");
    EXPECT_FALSE(localization.selectLanguage("fr").hasValue());

    localization.unregisterCatalog("sample");
    EXPECT_FALSE(localization.contains("sample.view.title"));
}

TEST(Localization, RefusesCatalogsThatBreakTheRules) {
    Localization localization;
    // clang-format off
    const auto refusal = [&localization](const TranslationCatalog& catalog) { return localization.registerCatalog("sample", catalog).error().code; };
    // clang-format on

    EXPECT_EQ(refusal({{"en", {{"other.view.title", "Title"}}}, {"pt", {{"other.view.title", "Título"}}}}), "translation_key_foreign");
    EXPECT_EQ(refusal({{"en", {{"sample.view.title", "Title"}}}, {"pt", {}}}), "translation_key_missing");
    EXPECT_EQ(refusal({{"en", {{"sample.view.title", "%1 items"}}}, {"pt", {{"sample.view.title", "itens"}}}}), "translation_arguments_mismatch");
    EXPECT_EQ(refusal({{"en", {{"sample.view.title", "%1 of %2"}}}, {"pt", {{"sample.view.title", "%2 itens"}}}}), "translation_arguments_mismatch");
    EXPECT_EQ(refusal({{"en", {{"sample.view.title", " "}}}, {"pt", {{"sample.view.title", "Título"}}}}), "translation_value_empty");
    EXPECT_EQ(refusal({{"en", {{"sample.view", "Title"}}}, {"pt", {{"sample.view", "Título"}}}}), "translation_key_invalid");
}

TEST(Localization, WritesNumbersWithTheSeparatorsOfTheLanguageBeingRead) {
    Localization localization;

    EXPECT_EQ(localization.formatNumber(1234567.891, 2), "1,234,567.89");
    EXPECT_EQ(localization.formatNumber(-0.004, 2), "0.00");
    EXPECT_EQ(localization.formatNumber(-1234.5, 1), "-1,234.5");
    EXPECT_EQ(localization.formatNumber(999.5, 0), "1,000");
    EXPECT_EQ(localization.formatNumber(12, 0), "12");

    ASSERT_TRUE(localization.selectLanguage("pt").hasValue());
    EXPECT_EQ(localization.formatNumber(1234567.891, 2), "1.234.567,89");
    EXPECT_EQ(localization.formatNumber(16, 2), "16,00");

    // The sign never depends on the locale of the C library, which a toolkit may set to one whose decimal separator is a comma.
    if (std::setlocale(LC_ALL, "pt_BR.UTF-8") != nullptr) {
        EXPECT_EQ(localization.formatNumber(-0.5, 2), "-0,50");
        std::setlocale(LC_ALL, "C");
    }
}

TEST(TextArgument, ReadsTextsAndNumbersAndRefusesEverythingElse) {
    Localization localization;
    ASSERT_TRUE(localization.selectLanguage("pt").hasValue());

    EXPECT_EQ(TextArgument::parse("word", "test").value().resolve(localization), "word");
    EXPECT_EQ(TextArgument::parse(nlohmann::json{{"number", 3.456}, {"decimals", 1}}, "test").value().resolve(localization), "3,5");
    EXPECT_EQ(TextArgument::parse(nlohmann::json{{"number", 42}}, "test").value().resolve(localization), "42");
    EXPECT_FALSE(TextArgument::parse(nlohmann::json{{"number", 1}, {"decimals", 7}}, "test").hasValue());
    EXPECT_FALSE(TextArgument::parse(nlohmann::json{{"number", "1"}}, "test").hasValue());
    EXPECT_FALSE(TextArgument::parse(nlohmann::json{{"number", 1}, {"unit", "b"}}, "test").hasValue());
    EXPECT_FALSE(TextArgument::parse(nlohmann::json(7), "test").hasValue());

    ASSERT_TRUE(localization.registerCatalog("sample", {{"en", {{"sample.unit.gigahertz", "%1 GHz"}, {"sample.core.line", "Core %1 · %2"}}}, {"pt", {{"sample.unit.gigahertz", "%1 GHz"}, {"sample.core.line", "Núcleo %1 · %2"}}}}).hasValue());
    const nlohmann::json frequency{{"key", "sample.unit.gigahertz"}, {"args", {{{"number", 3.2}, {"decimals", 2}}}}};
    const auto line = TextArgument::parse(nlohmann::json{{"key", "sample.core.line"}, {"args", {"0", frequency}}}, "test");
    ASSERT_TRUE(line.hasValue());
    EXPECT_EQ(line.value().resolve(localization), "Núcleo 0 · 3,20 GHz");

    nlohmann::json deep = "leaf";

    for (int level = 0; level <= TextArgument::maximumDepth; ++level) {
        deep = nlohmann::json{{"key", "sample.core.line"}, {"args", {"0", deep}}};
    }

    EXPECT_FALSE(TextArgument::parse(deep, "test").hasValue());
    EXPECT_FALSE(TextArgument::parse(nlohmann::json{{"key", "not a key"}}, "test").hasValue());
}

TEST(CoreCatalog, SpellsEveryKeyInEveryLanguageWithTheSameArguments) {
    const TranslationCatalog catalog = CoreCatalog::catalog();

    ASSERT_TRUE(catalog.contains("en"));
    ASSERT_TRUE(catalog.contains("pt"));

    for (const auto& [key, english] : catalog.at("en")) {
        EXPECT_TRUE(Localization::validKey(key)) << key;
        ASSERT_TRUE(catalog.at("pt").contains(key)) << key;
        EXPECT_EQ(Localization::placeholders(english), Localization::placeholders(catalog.at("pt").at(key))) << key;
    }

    EXPECT_EQ(catalog.at("en").size(), catalog.at("pt").size());

    for (const auto& language : Localization::languages()) {
        EXPECT_TRUE(catalog.at("en").contains(language.titleKey)) << language.titleKey;
    }

    Localization localization;
    EXPECT_TRUE(localization.registerCatalog(Localization::coreOwner, catalog).hasValue());
}

} // namespace workpane::localization
