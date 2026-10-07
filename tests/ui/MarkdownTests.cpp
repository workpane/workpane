#include "support/InterfaceHarness.h"
#include "ui/markdown/Block.h"
#include "ui/markdown/Layout.h"
#include "ui/markdown/LayoutBuilder.h"
#include "ui/markdown/Parser.h"
#include "ui/markdown/Span.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <gtest/gtest.h>

#include <string>
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
    EXPECT_EQ(plain(Parser::inlines("line  \nbreak")), "line  \nbreak");
    EXPECT_EQ(plain(Parser::parse("hard  \nbreak", false).front().spans), "hard\nbreak");
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

} // namespace workpane::ui::markdown
