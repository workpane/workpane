#include "support/InterfaceHarness.h"
#include "ui/markdown/Block.h"
#include "ui/markdown/CodeHighlighter.h"
#include "ui/markdown/Layout.h"
#include "ui/markdown/LayoutBuilder.h"
#include "ui/markdown/Parser.h"
#include "ui/markdown/Span.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace workpane::ui::markdown {

// Joins the text of the spans a paragraph was read into, which is what most parser expectations compare.
class MarkdownParserTest : public ::testing::Test {
  protected:
    static std::string plain(const std::vector<Span>& spans) {
        std::string text;

        for (const auto& span : spans) {
            text += span.text;
        }

        return text;
    }
};

TEST_F(MarkdownParserTest, ReadsHeadingsParagraphsAndRules) {
    const auto blocks = Parser::parse("# Title #\n\nFirst line\ncontinues here.\n\n---\n### Third level\n#hashtag", false);

    ASSERT_EQ(blocks.size(), 5U);
    EXPECT_EQ(blocks[0].kind, BlockKind::Heading);
    EXPECT_EQ(blocks[0].level, 1);
    EXPECT_EQ(plain(blocks[0].spans), "Title");
    EXPECT_EQ(blocks[1].kind, BlockKind::Paragraph);
    EXPECT_EQ(plain(blocks[1].spans), "First line continues here.");
    EXPECT_EQ(blocks[2].kind, BlockKind::Rule);
    EXPECT_EQ(blocks[3].level, 3);
    EXPECT_EQ(blocks[4].kind, BlockKind::Paragraph);
    EXPECT_EQ(plain(blocks[4].spans), "#hashtag");
}

TEST_F(MarkdownParserTest, ReadsNestedBulletsNumberedItemsAndContinuations) {
    const auto blocks = Parser::parse("- first\n  - nested\n* second\n  continued\n\n3. third\n4) fourth", false);

    ASSERT_EQ(blocks.size(), 5U);
    EXPECT_EQ(blocks[0].kind, BlockKind::Bullet);
    EXPECT_EQ(blocks[0].level, 0);
    EXPECT_EQ(blocks[1].level, 1);
    EXPECT_EQ(plain(blocks[2].spans), "second continued");
    EXPECT_EQ(blocks[3].kind, BlockKind::Numbered);
    EXPECT_EQ(blocks[3].ordinal, 3);
    EXPECT_EQ(blocks[4].ordinal, 4);
}

TEST_F(MarkdownParserTest, ReadsQuotesAndFencedCodeWithoutMarkupInside) {
    const auto blocks = Parser::parse("> quoted\n> text\n\n```lua\nlocal value = **1**\n\n  print(value)\n```\n~~~\nunterminated", false);

    ASSERT_EQ(blocks.size(), 3U);
    EXPECT_EQ(blocks[0].kind, BlockKind::Quote);
    EXPECT_EQ(plain(blocks[0].spans), "quoted text");
    EXPECT_EQ(blocks[1].kind, BlockKind::Code);
    EXPECT_EQ(blocks[1].language, "lua");
    EXPECT_EQ(blocks[1].code, "local value = **1**\n\n  print(value)");
    EXPECT_EQ(blocks[2].code, "unterminated");
}

TEST_F(MarkdownParserTest, ReadsEmphasisStrongCodeAndLinks) {
    const auto spans = Parser::inlines("plain *em* **strong** ***both*** `a *b*` [site](https://example.com \"title\") <https://auto.test> ![alt](image.png)");

    std::vector<std::string> emphasized;
    std::vector<std::string> strong;
    std::vector<std::string> code;
    std::vector<std::string> links;

    for (const auto& span : spans) {
        if (span.emphasis && !span.strong) {
            emphasized.push_back(span.text);
        }

        if (span.strong) {
            strong.push_back(span.text);
        }

        if (span.code) {
            code.push_back(span.text);
        }

        if (!span.link.empty()) {
            links.push_back(span.link);
        }
    }

    EXPECT_EQ(emphasized, (std::vector<std::string>{"em"}));
    EXPECT_EQ(strong, (std::vector<std::string>{"strong", "both"}));
    EXPECT_EQ(code, (std::vector<std::string>{"a *b*"}));
    EXPECT_EQ(links, (std::vector<std::string>{"https://example.com", "https://auto.test"}));
    EXPECT_NE(plain(spans).find("alt"), std::string::npos);
}

TEST_F(MarkdownParserTest, KeepsLiteralsThatAreNotMarkup) {
    EXPECT_EQ(plain(Parser::inlines("snake_case_name and 2 * 3 * 4")), "snake_case_name and 2 * 3 * 4");
    EXPECT_EQ(plain(Parser::inlines("\\*escaped\\* and `unclosed")), "*escaped* and `unclosed");
    EXPECT_EQ(plain(Parser::inlines("[no destination] and **open")), "[no destination] and **open");
    EXPECT_EQ(plain(Parser::inlines("line  \nbreak")), "line\nbreak");
    EXPECT_EQ(plain(Parser::parse("hard  \nbreak", false).front().spans), "hard\nbreak");
}

TEST_F(MarkdownParserTest, KeepsImageSourcesEmptyAlternativeTextAndNestedFormatting) {
    const auto spans = Parser::inlines("![a **bold** &amp; *soft*](picture.png) ![](empty.png) [![linked][picture]](https://example.com)\n\n[picture]: https://example.com/a.png?x=1&y=2");
    std::vector<Span> images;

    for (const auto& span : spans) {
        if (!span.image.empty()) {
            images.push_back(span);
        }
    }

    ASSERT_EQ(images.size(), 3U);
    EXPECT_EQ(images[0].image, "picture.png");
    EXPECT_EQ(images[0].text, "a bold & soft");
    EXPECT_EQ(images[1].image, "empty.png");
    EXPECT_TRUE(images[1].text.empty());
    EXPECT_EQ(images[2].image, "https://example.com/a.png?x=1&y=2");
    EXPECT_EQ(images[2].link, "https://example.com");
}

TEST(MarkdownLayout, FitsImagesIntoLinesAndTableCellsWithoutChangingCopiedText) {
    tests::InterfaceHarness harness;
    harness.frame();
    const FontRole body = harness.context().font(ThemeFont::Interface);
    const LayoutBuilder::ImageSizes sizes = {{"wide.png", ImVec2(1000.0F, 500.0F)}, {"pixel.png", ImVec2(1.0F, 1.0F)}};
    const Layout inlineImage = LayoutBuilder(harness.context(), body, 160.0F, sizes).build(Parser::parse("before ![small](pixel.png) after", false));
    EXPECT_EQ(inlineImage.text, "before small after");
    const Layout fitted = LayoutBuilder(harness.context(), body, 160.0F, sizes).build(Parser::parse("![wide](wide.png)\n\n| Picture | Caption |\n|---|---|\n| ![wide](wide.png) | description |", false));
    std::size_t images = 0;

    for (const auto& run : fitted.runs) {
        if (run.image.empty()) {
            continue;
        }

        ++images;
        EXPECT_LE(run.offset.x + run.width, 160.5F);
        EXPECT_FLOAT_EQ(run.height, run.width / 2.0F);

        if (run.clip >= 0) {
            const ImRect& cell = fitted.shapes[static_cast<std::size_t>(run.clip)].rect;
            EXPECT_GE(run.offset.x, cell.Min.x);
            EXPECT_LE(run.offset.x + run.width, cell.Max.x);
            EXPECT_LE(run.offset.y + run.height, cell.Max.y);
        }
    }

    EXPECT_EQ(images, 2U);
    EXPECT_GT(fitted.size.y, 80.0F);
}

// A chat keeps every line break a writer typed inside a paragraph, while code fences and blank lines still read as blocks.
TEST_F(MarkdownParserTest, KeepsEveryLineBreakWhenAskedTo) {
    const auto blocks = Parser::parse("first line\nsecond line\n\n```\ncode\n```\nafter", true);
    ASSERT_EQ(blocks.size(), 3U);
    EXPECT_EQ(plain(blocks[0].spans), "first line\nsecond line");
    EXPECT_EQ(blocks[1].code, "code");
    EXPECT_EQ(plain(blocks[2].spans), "after");
    EXPECT_EQ(plain(Parser::parse("first line\nsecond line", false).front().spans), "first line second line");
}

TEST(MarkdownLayout, WrapsWordsWithinTheWidthAndHangsListMarkers) {
    tests::InterfaceHarness harness;
    harness.frame();
    const FontRole body = harness.context().font(ThemeFont::Interface);
    const auto blocks = Parser::parse("A paragraph long enough to wrap over several lines when the width is narrow.\n\n- item that also wraps over lines", false);
    const Layout wide = LayoutBuilder(harness.context(), body, 2000.0F).build(blocks);
    const Layout narrow = LayoutBuilder(harness.context(), body, 160.0F).build(blocks);

    EXPECT_LE(narrow.size.x, 160.0F);
    EXPECT_GT(narrow.size.y, wide.size.y);

    float itemLeft = 0.0F;

    for (const auto& run : narrow.runs) {
        EXPECT_LE(run.offset.x + run.width, 160.5F) << run.text;

        if (run.text.starts_with("item")) {
            itemLeft = run.offset.x;
        }
    }

    EXPECT_GT(itemLeft, 0.0F);
}

TEST(MarkdownLayout, PreservesWhitespaceInsideInlineCode) {
    tests::InterfaceHarness harness;
    harness.frame();
    const FontRole body = harness.context().font(ThemeFont::Interface);
    const auto blocks = Parser::parse("Before `first  second` after.\n\n`   `\n\n| Code |\n|---|\n| `first  second` |", false);
    const Layout layout = LayoutBuilder(harness.context(), body, 2000.0F).build(blocks);
    std::vector<std::string> code;

    for (const auto& run : layout.runs) {
        if (run.code) {
            code.push_back(run.text);
        }
    }

    EXPECT_EQ(code, (std::vector<std::string>{"first  second", "   ", "first  second"}));
    EXPECT_NE(layout.text.find("Before first  second after."), std::string::npos);
    EXPECT_NE(layout.text.find("\n   \n"), std::string::npos);

    const Layout wrapped = LayoutBuilder(harness.context(), body, 65.0F).build(Parser::parse("`first  second`", false));
    std::string wrappedCode;

    for (const auto& run : wrapped.runs) {
        EXPECT_LE(run.offset.x + run.width, 65.5F);
        wrappedCode += run.text;
    }

    EXPECT_EQ(wrappedCode, "first  second");
}

TEST_F(MarkdownParserTest, ReadsTablesTasksStrikethroughReferencesAndEntities) {
    const auto blocks = Parser::parse("| Name | Value |\n| :--- | ---: |\n| **first** | ~~old~~ &amp; &#x1F37A; |\n\n- [x] done\n- [ ] pending\n\n[reference][place]\n\n[place]: https://example.com/?a=1&b=2", false);
    ASSERT_EQ(blocks.size(), 4U);
    ASSERT_EQ(blocks[0].kind, BlockKind::Table);
    ASSERT_EQ(blocks[0].rows.size(), 2U);
    EXPECT_EQ(blocks[0].rows[0][1].align, TextAlign::End);
    EXPECT_TRUE(blocks[0].rows[0][0].spans[0].strong);
    EXPECT_TRUE(blocks[0].rows[1][1].spans[0].strike);
    EXPECT_EQ(plain(blocks[0].rows[1][1].spans), "old & \xF0\x9F\x8D\xBA");
    EXPECT_EQ(blocks[1].checked, true);
    EXPECT_EQ(blocks[2].checked, false);
    EXPECT_EQ(blocks[3].spans[0].link, "https://example.com/?a=1&b=2");
}

TEST_F(MarkdownParserTest, ReadsSetextHeadingsIndentedCodeAndNestedQuotes) {
    const auto blocks = Parser::parse("Heading\n=======\n\n    indented\n    code\n\n> outer\n>\n> > inner", false);
    ASSERT_EQ(blocks.size(), 4U);
    EXPECT_EQ(blocks[0].kind, BlockKind::Heading);
    EXPECT_EQ(blocks[0].level, 1);
    EXPECT_EQ(blocks[1].code, "indented\ncode");
    EXPECT_EQ(blocks[2].quoteDepth, 1);
    EXPECT_EQ(blocks[3].quoteDepth, 2);
}

TEST(MarkdownLayout, ColorsDeclaredCodeAndKeepsUnknownCodeReadable) {
    const auto lines = CodeHighlighter::lines("local answer = 42 -- result", "lua");
    ASSERT_EQ(lines.size(), 1U);
    bool keyword = false;
    bool number = false;
    bool comment = false;

    for (const auto& run : lines[0]) {
        keyword = keyword || run.color == ThemeColor::AccentText;
        number = number || run.color == ThemeColor::WarningText;
        comment = comment || run.color == ThemeColor::TextMuted;
    }

    EXPECT_TRUE(keyword);
    EXPECT_TRUE(number);
    EXPECT_TRUE(comment);

    for (const auto& [language, source] : std::vector<std::pair<std::string, std::string>>{{"js", "const answer = 42 // result"}, {"ts", "interface Result { value: number }"}, {"rust", "let answer = 42;"}, {"go", "func main() {}"}, {"bash", "if true; then echo 'result'; fi"}}) {
        const auto colored = CodeHighlighter::lines(source, language);
        bool hasKeyword = false;

        for (const auto& line : colored) {
            for (const auto& run : line) {
                hasKeyword = hasKeyword || run.color == ThemeColor::AccentText;
            }
        }

        EXPECT_TRUE(hasKeyword) << language;
    }

    const auto unknown = CodeHighlighter::lines("uncolored text", "unknown");
    ASSERT_EQ(unknown.size(), 1U);
    ASSERT_EQ(unknown[0].size(), 1U);
    EXPECT_EQ(unknown[0][0].text, "uncolored text");
    EXPECT_FALSE(unknown[0][0].color.has_value());
}

TEST(MarkdownLayout, KeepsNestedContainersAndTableCellsWithinTheirBounds) {
    tests::InterfaceHarness harness;
    harness.frame();
    const FontRole body = harness.context().font(ThemeFont::Interface);
    const auto blocks = Parser::parse("- first\n\n  continuation\n\n  > > nested\n\n  ```lua\n  return 42\n  ```\n\n| A | B | C |\n|---|---|---|\n| longword | **styled** | value |", false);
    ASSERT_EQ(blocks.size(), 5U);
    EXPECT_EQ(blocks[1].listDepth, 1);
    EXPECT_EQ(blocks[2].quoteDepth, 2);
    EXPECT_EQ(blocks[3].listDepth, 1);
    const Layout layout = LayoutBuilder(harness.context(), body, 150.0F).build(blocks);
    bool indented = false;
    bool clipped = false;

    for (const auto& run : layout.runs) {
        if (run.text.starts_with("continuation")) {
            indented = run.offset.x > 0.0F;
        }

        if (run.clip >= 0 && layout.shapes[static_cast<std::size_t>(run.clip)].kind == ShapeKind::Clip) {
            const auto& bounds = layout.shapes[static_cast<std::size_t>(run.clip)].rect;
            EXPECT_LE(bounds.Max.x, 150.5F);
            EXPECT_GT(bounds.GetHeight(), 0.0F);
            clipped = true;
        }
    }

    EXPECT_TRUE(indented);
    EXPECT_TRUE(clipped);
}

} // namespace workpane::ui::markdown
