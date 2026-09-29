#include "localization/Localization.h"
#include "support/InterfaceHarness.h"
#include "support/Resources.h"
#include "support/TemporaryDirectory.h"
#include "text/Utf8.h"
#include "ui/Color.h"
#include "ui/Fonts.h"
#include "ui/IconCatalog.h"
#include "ui/Painter.h"
#include "ui/TextCase.h"
#include "ui/TextMatcher.h"
#include "ui/Widgets.h"
#include "ui/components/inputs/NumericInput.h"
#include "ui/model/ContentFontSize.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"
#include "ui/shell/KeyChords.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeCatalog.h"
#include "ui/theme/ThemeColorNames.h"
#include "ui/theme/ThemeManager.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <clocale>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::ui {

TEST(Color, WritesHexAndDerivesShadesTheWayQtDoes) {
    const Color accent = Color::rgb(31, 155, 93);
    const ImVec4 darker = accent.darker(140).vector();
    const ImVec4 lighter = accent.lighter(150).vector();

    EXPECT_EQ(accent.hex(), "#1f9b5d");
    EXPECT_NEAR(darker.x * 255.0F, 22.0F, 1.0F);
    EXPECT_NEAR(darker.y * 255.0F, 110.0F, 1.0F);
    EXPECT_NEAR(darker.z * 255.0F, 66.0F, 1.0F);
    EXPECT_GT(lighter.y, accent.vector().y);
    EXPECT_NEAR(accent.withAlpha(0.5F).vector().w, 0.5F, 1.0F / 255.0F);
    EXPECT_EQ(Color::blend(Color::rgb(0, 0, 0), Color::rgb(200, 100, 50), 0.5F).hex(), "#643219");
}

TEST(Color, MeasuresTheContrastOfTwoColorsTheWayTheAccessibilityGuidelinesDo) {
    const Color black = Color::rgb(0, 0, 0);
    const Color white = Color::rgb(255, 255, 255);
    const ThemeCatalog catalog;
    const Theme& theme = catalog.defaultTheme();

    EXPECT_NEAR(black.contrast(white), 21.0, 0.001);
    EXPECT_NEAR(white.contrast(black), 21.0, 0.001);
    EXPECT_NEAR(white.contrast(white), 1.0, 0.001);

    for (const ThemeColor tone : {ThemeColor::Danger, ThemeColor::Warning, ThemeColor::Information}) {
        EXPECT_GT(theme.color(tone).contrast(theme.color(ThemeColor::Window)), theme.color(tone).contrast(theme.color(ThemeColor::Text)));
        EXPECT_GE(theme.color(tone).contrast(theme.color(ThemeColor::Window)), 4.5);
    }
}

TEST(Painter, CentersTheCrossOfACloseCircleOnTheCircle) {
    tests::InterfaceHarness harness;
    harness.frame();
    ImDrawList list(ImGui::GetDrawListSharedData());
    list._ResetForNewFrame();
    list.PushClipRectFullScreen();
    const Color cross = Color::rgb(255, 255, 255);
    Painter::closeCircle(list, ImVec2(20.0F, 30.0F), 16.0F, Color::rgb(221, 91, 95), cross, true);
    std::optional<ImRect> bounds;

    for (const ImDrawVert& vertex : list.VtxBuffer) {
        if ((vertex.col & IM_COL32_A_MASK) == 0 || (vertex.col | IM_COL32_A_MASK) != cross.packed()) {
            continue;
        }

        bounds = bounds.has_value() ? ImRect(ImMin(bounds->Min, vertex.pos), ImMax(bounds->Max, vertex.pos)) : ImRect(vertex.pos, vertex.pos);
    }

    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->GetCenter().x, 20.0F, 0.01F);
    EXPECT_NEAR(bounds->GetCenter().y, 30.0F, 0.01F);
}

// A divider is one point of whole pixels, so it stays one pixel at the first scale and grows to two at the second.
TEST(Painter, DrawsLinesOfWholePixelsAtEveryScale) {
    tests::InterfaceHarness harness;
    harness.frame();
    ImDrawList list(ImGui::GetDrawListSharedData());
    list._ResetForNewFrame();
    list.PushClipRectFullScreen();
    Painter::horizontalDivider(list, ImVec2(3.4F, 10.6F), 50.0F, 2.0F, Color::rgb(1, 2, 3));
    Painter::verticalDivider(list, ImVec2(100.2F, 0.0F), 20.0F, 1.2F, Color::rgb(1, 2, 3));
    std::vector<ImVec2> points;

    for (const ImDrawVert& vertex : list.VtxBuffer) {
        points.push_back(vertex.pos);
    }

    ASSERT_GE(points.size(), 8U);
    EXPECT_EQ(points[0].y, 10.0F);
    EXPECT_EQ(points[2].y, 12.0F);
    EXPECT_EQ(points[4].x, 100.0F);
    EXPECT_EQ(points[5].x, 101.0F);
}

TEST(Theme, OffersTheThreeWorkpaneThemesWithTheirExactColors) {
    const ThemeCatalog catalog;
    ASSERT_EQ(catalog.themes().size(), 3U);
    ASSERT_NE(catalog.find("green"), nullptr);
    ASSERT_NE(catalog.find("blue"), nullptr);
    ASSERT_NE(catalog.find("red"), nullptr);
    EXPECT_EQ(catalog.find("purple"), nullptr);
    EXPECT_EQ(catalog.defaultTheme().id(), "green");

    const Theme& green = *catalog.find("green");
    EXPECT_EQ(green.color(ThemeColor::Window).hex(), "#1f1f1f");
    EXPECT_FLOAT_EQ(green.font(ThemeFont::Interface).size, 12.0F);
    EXPECT_FLOAT_EQ(green.font(ThemeFont::Navigation).size, 10.0F);
    EXPECT_FLOAT_EQ(green.font(ThemeFont::Caption).size, 10.0F);
    EXPECT_FLOAT_EQ(green.font(ThemeFont::PageTitle).size, 13.0F);
    EXPECT_EQ(green.font(ThemeFont::SectionTitle).face, FontFace::SemiBold);
    EXPECT_EQ(green.font(ThemeFont::Heading).face, FontFace::SemiBold);
    EXPECT_FLOAT_EQ(green.font(ThemeFont::Heading).size, 20.0F);
    EXPECT_EQ(green.font(ThemeFont::Monospace).face, FontFace::Monospace);
    EXPECT_FLOAT_EQ(green.metric(ThemeMetric::CaretWidth), 2.0F);
    EXPECT_EQ(green.color(ThemeColor::Panel).hex(), "#262626");
    EXPECT_EQ(green.color(ThemeColor::Raised).hex(), "#2d2d2d");
    EXPECT_EQ(green.color(ThemeColor::Border).hex(), "#3f3f3f");
    EXPECT_EQ(green.color(ThemeColor::Text).hex(), "#f2f2f2");
    EXPECT_EQ(green.color(ThemeColor::TextMuted).hex(), "#aeaeae");
    EXPECT_EQ(green.color(ThemeColor::Accent).hex(), "#1f9b5d");
    EXPECT_EQ(green.color(ThemeColor::Success).hex(), "#27bf73");
    EXPECT_EQ(green.color(ThemeColor::AccentHover), green.color(ThemeColor::Accent).darker(115));
    EXPECT_EQ(green.color(ThemeColor::AccentStrong), green.color(ThemeColor::Accent).darker(140));
    EXPECT_EQ(catalog.find("blue")->color(ThemeColor::Accent).hex(), "#2d74cb");
    EXPECT_EQ(catalog.find("red")->color(ThemeColor::Accent).hex(), "#be464b");
    EXPECT_FLOAT_EQ(green.metric(ThemeMetric::SplitterWidth), 1.0F);
    EXPECT_EQ(green.color(ThemeColor::DangerHover), green.color(ThemeColor::Danger).darker(115));
    EXPECT_EQ(green.color(ThemeColor::DangerStrong), green.color(ThemeColor::Danger).darker(140));
    EXPECT_EQ(green.color(ThemeColor::TextDisabled), green.color(ThemeColor::TextMuted).darker(125));
    EXPECT_EQ(green.color(ThemeColor::ScrollbarHover), green.color(ThemeColor::BorderStrong).lighter(120));
    EXPECT_EQ(green.color(ThemeColor::ScrollbarActive), green.color(ThemeColor::BorderStrong).lighter(140));
    EXPECT_EQ(green.color(ThemeColor::Selection), green.color(ThemeColor::Accent).withAlpha(0.45F));
    EXPECT_EQ(green.color(ThemeColor::Overlay), Color::rgb(0, 0, 0).withAlpha(0.45F));
}

TEST(Theme, NamesEveryColorRoleForLua) {
    std::set<std::string_view> names;

    for (const auto& [role, name] : ThemeColorNames::all()) {
        EXPECT_EQ(ThemeColorNames::parse(name), role);
        names.insert(name);
    }

    EXPECT_EQ(names.size(), ThemeColorNames::all().size());
    EXPECT_EQ(names.size(), static_cast<std::size_t>(ThemeColor::InformationText) + 1U);
    EXPECT_FALSE(ThemeColorNames::parse("Accent").has_value());
}

// Every theme gives each fill an ink readable on it and each tone a text readable on the window and on its own background, so no plugin writes white on yellow.
TEST(Theme, GivesEveryFillAnInkAndEveryToneAReadableText) {
    const ThemeCatalog catalog;
    const std::vector<std::pair<ThemeColor, ThemeColor>> fills{{ThemeColor::Accent, ThemeColor::OnAccent}, {ThemeColor::AccentHover, ThemeColor::OnAccent}, {ThemeColor::AccentStrong, ThemeColor::OnAccent}, {ThemeColor::Success, ThemeColor::OnSuccess}, {ThemeColor::Warning, ThemeColor::OnWarning}, {ThemeColor::Danger, ThemeColor::OnDanger}, {ThemeColor::DangerHover, ThemeColor::OnDanger}, {ThemeColor::DangerStrong, ThemeColor::OnDanger}, {ThemeColor::Information, ThemeColor::OnInformation}};
    const std::vector<std::pair<ThemeColor, ThemeColor>> states{{ThemeColor::Accent, ThemeColor::AccentHover}, {ThemeColor::AccentHover, ThemeColor::AccentStrong}, {ThemeColor::Danger, ThemeColor::DangerHover}, {ThemeColor::DangerHover, ThemeColor::DangerStrong}};
    const std::vector<std::pair<ThemeColor, ThemeColor>> tones{{ThemeColor::AccentBackground, ThemeColor::AccentText}, {ThemeColor::SuccessBackground, ThemeColor::SuccessText}, {ThemeColor::WarningBackground, ThemeColor::WarningText}, {ThemeColor::DangerBackground, ThemeColor::DangerText}, {ThemeColor::InformationBackground, ThemeColor::InformationText}};

    for (const auto& theme : catalog.themes()) {
        const Color window = theme->color(ThemeColor::Window);
        const Color tooltip = theme->color(ThemeColor::Tooltip);

        EXPECT_GE(theme->color(ThemeColor::Text).contrast(window), 7.0) << theme->id();
        EXPECT_GE(theme->color(ThemeColor::TextMuted).contrast(window), 4.5) << theme->id();
        EXPECT_GE(theme->color(ThemeColor::TextDisabled).contrast(window), 3.0) << theme->id();
        EXPECT_GE(theme->color(ThemeColor::OnTooltip).contrast(tooltip), 7.0) << theme->id();
        EXPECT_GE(theme->color(ThemeColor::OnTooltipMuted).contrast(tooltip), 4.5) << theme->id();

        for (const auto& [fill, ink] : fills) {
            EXPECT_GE(theme->color(ink).contrast(theme->color(fill)), 3.0) << theme->id() << " " << static_cast<int>(ink) << " on " << static_cast<int>(fill);
        }

        // A fill under the pointer or pressed never makes its ink harder to read than the fill at rest.
        for (const auto& [rest, next] : states) {
            const ThemeColor ink = rest == ThemeColor::Accent || rest == ThemeColor::AccentHover ? ThemeColor::OnAccent : ThemeColor::OnDanger;
            EXPECT_GE(theme->color(ink).contrast(theme->color(next)), theme->color(ink).contrast(theme->color(rest))) << theme->id() << " " << static_cast<int>(next);
        }

        for (const auto& [background, text] : tones) {
            EXPECT_GE(theme->color(text).contrast(window), 4.5) << theme->id() << " " << static_cast<int>(text);
            EXPECT_GE(theme->color(text).contrast(theme->color(background)), 4.5) << theme->id() << " " << static_cast<int>(text);
        }
    }
}

TEST(ThemeManager, SelectsKnownThemesAndKeepsTheDefaultForUnknownOnes) {
    ThemeManager themes;
    themes.loadStoredTheme("unknown");
    EXPECT_EQ(themes.theme().id(), "green");
    EXPECT_TRUE(themes.selectTheme("red").hasValue());
    EXPECT_EQ(themes.theme().id(), "red");
    EXPECT_FALSE(themes.selectTheme("purple").hasValue());
    EXPECT_EQ(themes.theme().id(), "red");
}

TEST(Icons, DrawsEveryIconOfTheCatalogInsideItsSquareUnderItsOwnName) {
    tests::InterfaceHarness harness;
    harness.frame();
    const ui::Fonts& fonts = harness.context().fonts();
    std::set<std::string_view> names;

    for (const std::string_view name : IconCatalog::productNames()) {
        const auto parsed = IconCatalog::parse(name);
        ASSERT_TRUE(parsed.has_value()) << name;
        const Icon icon = *parsed;
        EXPECT_TRUE(names.insert(name).second) << name;
        EXPECT_TRUE(fonts.face(FontFace::Icon)->IsGlyphInFont(icon.glyph)) << name;

        ImDrawList list(ImGui::GetDrawListSharedData());
        list._ResetForNewFrame();
        list.PushClipRectFullScreen();
        IconCatalog::draw(fonts, list, icon, ImVec2(10.0F, 20.0F), 24.0F, Color::rgb(255, 255, 255));
        ASSERT_GT(list.VtxBuffer.Size, 0) << name;

        for (const ImDrawVert& vertex : list.VtxBuffer) {
            EXPECT_GE(vertex.pos.x, 9.0F) << name;
            EXPECT_LE(vertex.pos.x, 35.0F) << name;
            EXPECT_GE(vertex.pos.y, 19.0F) << name;
            EXPECT_LE(vertex.pos.y, 45.0F) << name;
        }
    }

    EXPECT_FALSE(IconCatalog::parse("unknown-icon").has_value());

    // Any glyph of the face answers its Lucide name, and a name of the product keeps the glyph the product chose for it.
    for (const auto* lucide : {"bird", "swords", "castle", "gamepad-2"}) {
        const auto parsed = IconCatalog::parse(lucide);
        ASSERT_TRUE(parsed.has_value()) << lucide;
        EXPECT_TRUE(fonts.face(FontFace::Icon)->IsGlyphInFont(parsed->glyph)) << lucide;
    }

    EXPECT_EQ(IconCatalog::parse("bird")->glyph, 0xE3C5);
    EXPECT_EQ(IconCatalog::parse("close"), Icon::Close);
    EXPECT_EQ(IconCatalog::parseOptional("no-such-glyph").error().code, "ui_icon_unknown");
}

TEST(Fonts, SizesEveryFaceByItsEmLikeOtherTextRenderers) {
    tests::InterfaceHarness harness;
    harness.frame();
    const ui::Fonts& fonts = harness.context().fonts();

    EXPECT_NEAR(fonts.size(FontFace::Regular, 12.0F), 12.0F * 2478.0F / 2048.0F, 0.01F);
    EXPECT_NEAR(fonts.size(FontFace::SemiBoldItalic, 12.0F), 12.0F * 2478.0F / 2048.0F, 0.01F);
    EXPECT_NEAR(fonts.size(FontFace::Monospace, 10.0F), 13.2F, 0.01F);
    EXPECT_NEAR(fonts.size(FontFace::Icon, 16.0F), 16.0F, 0.01F);

    EXPECT_NEAR(fonts.face(FontFace::Monospace)->CalcTextSizeA(fonts.size(FontFace::Monospace, 10.0F), FLT_MAX, 0.0F, "M").x, 6.0F, 0.15F);
}

// A missing face and a file that is no font are refused by name, so the product never starts with a face it cannot draw.
TEST(Fonts, RefusesAMissingOrUnreadableFace) {
    const tests::TemporaryDirectory temporary;
    const std::filesystem::path& folder = temporary.path();
    ImFontAtlas missing;
    ui::Fonts fonts;

    const auto absent = fonts.load(missing, folder);
    ASSERT_FALSE(absent.hasValue());
    EXPECT_EQ(absent.error().code, "font_missing");

    for (const auto& entry : std::filesystem::directory_iterator(tests::Resources::fonts())) {
        std::filesystem::copy_file(entry.path(), folder / entry.path().filename());
    }

    std::ofstream(folder / "Lucide.ttf", std::ios::binary | std::ios::trunc) << "not a font";
    ImFontAtlas broken;
    const auto unreadable = fonts.load(broken, folder);
    ASSERT_FALSE(unreadable.hasValue());
    EXPECT_EQ(unreadable.error().code, "font_unreadable");
}

// A number field writes and reads its numbers with a point before the decimals even while the process reads numbers with a comma, as GTK leaves it on Linux in Portuguese.
TEST(NumericInput, WritesAndReadsNumbersWithAPointWhateverTheLocale) {
    const std::string previous = std::setlocale(LC_NUMERIC, nullptr);

    for (const char* locale : {"pt_BR.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8"}) {
        if (std::setlocale(LC_NUMERIC, locale) != nullptr) {
            break;
        }
    }

    double value = 0.0;
    EXPECT_EQ(ui::NumericInput::format(1.5, 1), "1.5");
    EXPECT_EQ(ui::NumericInput::format(-0.25, 2), "-0.25");
    EXPECT_TRUE(ui::NumericInput::parse(" 2.75 ", value));
    EXPECT_DOUBLE_EQ(value, 2.75);
    EXPECT_FALSE(ui::NumericInput::parse("2,75", value));
    EXPECT_FALSE(ui::NumericInput::parse("2.75x", value));
    EXPECT_FALSE(ui::NumericInput::parse("   ", value));
    EXPECT_FALSE(ui::NumericInput::parse("inf", value));
    std::setlocale(LC_NUMERIC, previous.c_str());
}

// A single line never spills over the lines under it, so text after a line break is left out and marked with an ellipsis.
TEST(Widgets, ElidesASingleLineAtItsFirstLineBreak) {
    tests::InterfaceHarness harness;
    harness.frame();
    const ui::RenderContext& context = harness.context();
    const FontRole role = context.font(ThemeFont::Interface);

    EXPECT_EQ(Widgets::elided(context, role, "first line", 1000.0F), "first line");

    // A rectangle placed anywhere and built from the measure of its own text keeps that text whole, whatever its edges lose to rounding.
    const float measured = Widgets::textSize(context, role, "AI Tasks").x;

    for (float left = 0.0F; left < 2000.0F; left += 0.37F) {
        EXPECT_EQ(Widgets::elided(context, role, "AI Tasks", (left + measured) - left), "AI Tasks") << left;
    }

    EXPECT_EQ(Widgets::elided(context, role, "first\nsecond", 1000.0F), "first\xE2\x80\xA6");
    EXPECT_EQ(Widgets::elided(context, role, "first\r\nsecond", 1000.0F), "first\xE2\x80\xA6");
    EXPECT_EQ(Widgets::elided(context, role, "a long line of words", 40.0F).substr(0, 1), "a");
    EXPECT_LE(Widgets::textSize(context, role, Widgets::elided(context, role, "a long line of words", 40.0F)).x, 40.0F);

    // A long line is cut once at the last character that fits, never inside a character written in several bytes.
    std::string accented;

    for (int index = 0; index < 400; ++index) {
        accented += "\xC3\xA7\xC3\xA3o ";
    }

    const std::string cut = Widgets::elided(context, role, accented, 300.0F);
    EXPECT_LE(Widgets::textSize(context, role, cut).x, 300.0F);
    EXPECT_GT(Widgets::textSize(context, role, cut).x, 250.0F);
    EXPECT_TRUE(cut.ends_with("\xE2\x80\xA6"));
    EXPECT_TRUE(accented.starts_with(cut.substr(0, cut.size() - 3)));
    EXPECT_TRUE(text::Utf8::valid(cut));
}

// A paragraph measured and then drawn in one frame is broken once, a different width or face breaks it again, and the next frame forgets every break.
TEST(Widgets, BreaksAParagraphOnceAFrame) {
    tests::InterfaceHarness harness;
    harness.frame();
    ui::RenderContext& context = harness.context();
    const FontRole role = context.font(ThemeFont::Interface);
    const std::string text = "a paragraph long enough to break into several lines at a narrow width";

    const auto measured = Widgets::wrapLines(context, role, text, 80.0F);
    ASSERT_GT(measured.size(), 1U);
    ASSERT_NE(context.paragraphs().find(text, role, 80.0F), nullptr);
    EXPECT_EQ(Widgets::wrapLines(context, role, text, 80.0F), measured);
    EXPECT_EQ(context.paragraphs().find(text, role, 120.0F), nullptr);
    EXPECT_EQ(context.paragraphs().find(text, context.font(ThemeFont::Caption), 80.0F), nullptr);

    const std::string copy = text;
    const auto drawn = Widgets::wrapLines(context, role, copy, 80.0F);
    EXPECT_EQ(drawn, measured);
    EXPECT_EQ(drawn.front().data(), copy.data());

    harness.frame();
    EXPECT_EQ(context.paragraphs().find(text, role, 80.0F), nullptr);
}

TEST(TextCase, ChangesTheCaseOfAccentedLettersAndSortsLikeAReader) {
    EXPECT_EQ(TextCase::upper("configurações"), "CONFIGURAÇÕES");
    EXPECT_EQ(TextCase::lower("ÁRVORE"), "árvore");
    EXPECT_TRUE(TextCase::alphabeticalLess("árvore", "busca"));
    EXPECT_TRUE(TextCase::alphabeticalLess("Configurações", "Donate"));
    EXPECT_TRUE(TextCase::alphabeticalLess("Alpha", "beta"));
    EXPECT_FALSE(TextCase::alphabeticalLess("beta", "Alpha"));
    EXPECT_TRUE(TextCase::alphabeticalLess("e", "é"));
    EXPECT_TRUE(TextCase::alphabeticalLess("Straße", "Strasser"));
}

// A search ignores the case of ASCII letters whatever the locale, keeps accented words whole and names every match by the bytes it covers.
TEST(TextMatcher, FindsWholeWordsWithoutSplittingAccentedOnes) {
    const TextMatcher word("a", false, true);
    EXPECT_TRUE(word.find("a\xC3\xA7\xC3\xA3o", 10).empty());
    EXPECT_EQ(word.find("x A y", 10).size(), 1U);

    const TextMatcher any("Beta", false, false);
    const auto found = any.find("alpha BETA beta", 10);
    ASSERT_EQ(found.size(), 2U);
    EXPECT_EQ(found[0].start, 6U);
    EXPECT_EQ(found[1].end, 15U);
    EXPECT_TRUE(TextMatcher("Beta", true, false).find("beta", 10).empty());
}

// Colors cross from Lua as a number sign and six hexadecimal digits, and nothing else passes for one.
TEST(Color, ReadsOnlySixHexadecimalDigits) {
    EXPECT_EQ(Color::parse("#1A2b3C"), Color::rgb(0x1A, 0x2B, 0x3C));
    EXPECT_FALSE(Color::parse("#-f0000").has_value());
    EXPECT_FALSE(Color::parse("#-1-1-1").has_value());
    EXPECT_FALSE(Color::parse("#12345").has_value());
    EXPECT_FALSE(Color::parse("123456").has_value());
}

TEST(TextValue, ResolvesLiteralsAndTranslatedKeysWithTheirArguments) {
    localization::Localization localization;
    ASSERT_TRUE(localization.registerCatalog("sample", {{"en", {{"sample.view.count", "%1 items"}}}, {"pt", {{"sample.view.count", "%1 itens"}}}}).hasValue());

    const auto literal = TextValue::parse("plain", "sample");
    const auto translated = TextValue::parse({{"key", "sample.view.count"}, {"args", {"3"}}}, "sample");
    ASSERT_TRUE(literal.hasValue());
    ASSERT_TRUE(translated.hasValue());
    EXPECT_EQ(literal.value().resolve(localization), "plain");
    EXPECT_EQ(translated.value().resolve(localization), "3 items");

    const auto counted = TextValue::parse({{"key", "sample.view.count"}, {"args", {{{"number", 1500.25}, {"decimals", 1}}}}}, "sample");
    ASSERT_TRUE(counted.hasValue());
    EXPECT_EQ(counted.value().resolve(localization), "1,500.3 items");

    ASSERT_TRUE(localization.selectLanguage("pt").hasValue());
    EXPECT_EQ(translated.value().resolve(localization), "3 itens");
    EXPECT_EQ(counted.value().resolve(localization), "1.500,3 itens");
    EXPECT_FALSE(TextValue::parse({{"key", "sample.view.count"}, {"args", {true}}}, "sample").hasValue());
    EXPECT_FALSE(TextValue::parse({{"key", "not a key"}}, "sample").hasValue());
    EXPECT_FALSE(TextValue::parse(3, "sample").hasValue());
}

TEST(ContentFontSize, StepsWithinItsRange) {
    EXPECT_GT(ContentFontSize::stepped(ContentFontSize::standard, FontStep::Increase), ContentFontSize::standard);
    EXPECT_LT(ContentFontSize::stepped(ContentFontSize::standard, FontStep::Decrease), ContentFontSize::standard);
    EXPECT_DOUBLE_EQ(ContentFontSize::stepped(20.0, FontStep::Reset), ContentFontSize::standard);
    EXPECT_DOUBLE_EQ(ContentFontSize::stepped(ContentFontSize::maximum, FontStep::Increase), ContentFontSize::maximum);
    EXPECT_DOUBLE_EQ(ContentFontSize::stepped(ContentFontSize::minimum, FontStep::Decrease), ContentFontSize::minimum);
}

TEST(KeyChords, ReadsTheCombinationsPluginsDeclare) {
    EXPECT_EQ(KeyChords::parse("mod+r"), ImGuiMod_Ctrl | ImGuiKey_R);
    EXPECT_EQ(KeyChords::parse("mod+shift+n"), ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N);
    EXPECT_EQ(KeyChords::parse("alt+down"), ImGuiMod_Alt | ImGuiKey_DownArrow);
    EXPECT_EQ(KeyChords::parse("mod+shift+rightbracket"), ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_RightBracket);
    EXPECT_EQ(KeyChords::parse("f5"), ImGuiKey_F5);
    EXPECT_EQ(KeyChords::parse("shift+f12"), ImGuiMod_Shift | ImGuiKey_F12);

    // A plain character or a lone shift would take what the reader types, and malformed combinations name nothing.
    EXPECT_FALSE(KeyChords::parse("r").has_value());
    EXPECT_FALSE(KeyChords::parse("shift+r").has_value());
    EXPECT_FALSE(KeyChords::parse("mod+mod+r").has_value());
    EXPECT_FALSE(KeyChords::parse("mod+r+s").has_value());
    EXPECT_FALSE(KeyChords::parse("mod+").has_value());
    EXPECT_FALSE(KeyChords::parse("mod+unknown").has_value());
    EXPECT_FALSE(KeyChords::parse("f13").has_value());
    EXPECT_FALSE(KeyChords::parse("").has_value());
}

TEST(KeyChords, KeepsTheCoreShortcutsForTheCore) {
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiKey_Q));
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiKey_1));
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiKey_9));
    EXPECT_FALSE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiKey_0));
    EXPECT_FALSE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_1));
    EXPECT_FALSE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiKey_R));

    // The chords a text field edits with stay with the field, because plugin shortcuts are answered while it has the keyboard.
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiKey_V));
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z));
    EXPECT_FALSE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V));
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiKey_LeftArrow));
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_End));
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiKey_Backspace));
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Alt | ImGuiMod_Shift | ImGuiKey_RightArrow));
    EXPECT_TRUE(KeyChords::reserved(ImGuiMod_Alt | ImGuiKey_Backspace));
    EXPECT_FALSE(KeyChords::reserved(ImGuiMod_Alt | ImGuiKey_DownArrow));
    EXPECT_FALSE(KeyChords::reserved(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_LeftArrow));
}

} // namespace workpane::ui
