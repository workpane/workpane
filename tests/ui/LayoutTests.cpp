#include "json/ObjectReader.h"
#include "support/ComponentSpecs.h"
#include "support/InterfaceHarness.h"
#include "support/Probe.h"
#include "ui/WheelScale.h"
#include "ui/model/Component.h"
#include "ui/model/ComponentRegistry.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/SurfaceStore.h"
#include "ui/theme/Theme.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <cfloat>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::ui {

using nlohmann::json;

class LayoutTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // clang-format off
        m_harness.registry().add("probe", [](NodeId id) { return std::make_unique<tests::Probe>(id); });
        // clang-format on
        tests::Probe::placed().clear();
    }

    static json probe(std::uint64_t id, json props = json::object()) {
        return tests::ComponentSpecs::node(id, "probe", std::move(props));
    }

    ImRect layout(const json& tree) {
        const auto mounted = m_harness.mount(tree);
        EXPECT_TRUE(mounted.hasValue()) << (mounted.hasValue() ? "" : mounted.error().code + " " + mounted.error().detail);
        m_harness.frame();

        return tests::Probe::placed().at(2);
    }

    // A part of a component is found by the color it was painted in, since every color comes from one role of the theme.
    static bool painted(ImU32 color) {
        for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
            for (const ImDrawVert& vertex : list->VtxBuffer) {
                if (vertex.col == color) {
                    return true;
                }
            }
        }

        return false;
    }

    tests::InterfaceHarness m_harness;
};

TEST_F(LayoutTest, AColumnSharesItsFreeHeightByGrowthAndTakesBackWhatItLacks) {
    constexpr float height = tests::InterfaceHarness::height;
    layout(tests::ComponentSpecs::node(1, "column", {{"spacing", 10}}, {probe(2, {{"contentHeight", 50}}), probe(3, {{"grow", 1}}), probe(4, {{"grow", 3}})}));

    EXPECT_FLOAT_EQ(tests::Probe::placed().at(2).GetHeight(), 50.0F);
    EXPECT_NEAR(tests::Probe::placed().at(3).GetHeight(), 10.0F + (height - 50.0F - 20.0F - 20.0F) / 4.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(4).GetHeight(), 10.0F + (height - 50.0F - 20.0F - 20.0F) * 3.0F / 4.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(4).Max.y, height, 1.0F);

    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    layout(tests::ComponentSpecs::node(1, "column", json::object(), {probe(2, {{"contentHeight", 5000}, {"grow", 1}, {"minHeight", 100}}), probe(3, {{"contentHeight", 40}})}));

    EXPECT_NEAR(tests::Probe::placed().at(2).GetHeight(), height - 40.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(3).Max.y, height, 1.0F);
}

TEST_F(LayoutTest, ARowGivesGrowingChildrenWhatIsLeftWithinTheirBounds) {
    constexpr float width = tests::InterfaceHarness::width;
    layout(tests::ComponentSpecs::node(1, "row", {{"spacing", 8}}, {probe(2, {{"contentWidth", 100}}), probe(3, {{"grow", 1}, {"maxWidth", 200}}), probe(4, {{"grow", 1}})}));

    EXPECT_FLOAT_EQ(tests::Probe::placed().at(2).GetWidth(), 100.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(3).GetWidth(), 200.0F);
    EXPECT_NEAR(tests::Probe::placed().at(4).GetWidth(), (width - 100.0F - 16.0F) / 2.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(3).Min.x, 108.0F, 1.0F);
}

// A growing child held at its minimum takes what it needs from the share of the other growing children, so the row never overflows while there is room.
TEST_F(LayoutTest, ARowPassesAShortfallOnOnceAChildStopsAtItsMinimum) {
    constexpr float width = tests::InterfaceHarness::width;
    const float wide = width * 0.75F;
    layout(tests::ComponentSpecs::node(1, "row", {{"padding", 0}, {"spacing", 0}}, {probe(2, {{"grow", 1}, {"minWidth", wide}}), probe(3, {{"grow", 1}}), probe(4, {{"grow", 2}})}));

    EXPECT_FLOAT_EQ(tests::Probe::placed().at(2).GetWidth(), wide);
    EXPECT_NEAR(tests::Probe::placed().at(3).GetWidth(), (width - wide) / 3.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(4).GetWidth(), (width - wide) * 2.0F / 3.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(4).Max.x, width, 1.0F);
}

// A row without room for every child hides its collapsible children, the highest collapse first, and only as many as it must.
TEST_F(LayoutTest, ARowHidesItsCollapsibleChildrenWhenItLacksRoom) {
    // clang-format off
    const auto row = [](float width) { return tests::ComponentSpecs::node(7, "column", json::object(), {tests::ComponentSpecs::node(1, "row", {{"width", width}, {"align", "start"}}, {probe(2, {{"grow", 1}, {"minWidth", 40}}), probe(3, {{"contentWidth", 100}, {"collapse", 3}}), probe(4, {{"contentWidth", 100}, {"collapse", 2}}), probe(5, {{"contentWidth", 100}, {"collapse", 1}}), probe(6, {{"contentWidth", 30}})})}); };
    // clang-format on

    layout(row(400.0F));
    EXPECT_EQ(tests::Probe::placed().size(), 5U);

    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    tests::Probe::placed().clear();
    layout(row(300.0F));
    EXPECT_FALSE(tests::Probe::placed().contains(3));
    EXPECT_TRUE(tests::Probe::placed().contains(4));
    EXPECT_TRUE(tests::Probe::placed().contains(5));

    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    tests::Probe::placed().clear();
    layout(row(150.0F));
    EXPECT_FALSE(tests::Probe::placed().contains(3));
    EXPECT_FALSE(tests::Probe::placed().contains(4));
    EXPECT_FALSE(tests::Probe::placed().contains(5));
    EXPECT_TRUE(tests::Probe::placed().contains(6));
    EXPECT_GE(tests::Probe::placed().at(2).GetWidth(), 40.0F);
    EXPECT_FALSE(m_harness.patch(1, {{"collapse", -1}}).hasValue());
}

// A scroll area turned sideways keeps its content at the width it asks for, makes room for its scroll bar and moves sideways with the wheel as far as the system scrolls.
TEST_F(LayoutTest, AHorizontalScrollKeepsItsContentWideAndMovesWithTheWheel) {
    json cells = json::array();

    for (std::uint64_t index = 0; index < 10; ++index) {
        cells.push_back(probe(10 + index, {{"contentWidth", 100}, {"contentHeight", 20}}));
    }

    const auto scroll = tests::ComponentSpecs::node(2, "scroll", {{"orientation", "horizontal"}, {"width", 300}, {"align", "start"}}, {tests::ComponentSpecs::node(3, "row", json::object(), cells)});
    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "column", json::object(), {scroll})).hasValue());
    m_harness.frames(2);
    const ImVec2 size = m_harness.node(2)->measure(m_harness.context(), tests::InterfaceHarness::width);
    const float before = tests::Probe::placed().at(12).Min.x;

    // The cells past the width of the area are laid out but not drawn until they scroll into view.
    EXPECT_FLOAT_EQ(size.x, 300.0F);
    EXPECT_FLOAT_EQ(size.y, 20.0F + ImGui::GetStyle().ScrollbarSize);
    EXPECT_NEAR(before - tests::Probe::placed().at(10).Min.x, 200.0F, 1.0F);
    EXPECT_FALSE(tests::Probe::placed().contains(19));

    m_harness.moveTo(ImVec2(100.0F, 10.0F));
    const ImVec2 step = ui::WheelScale::units(ImVec2(0.0F, -1.0F), 40.0F);
    ImGui::GetIO().AddMouseWheelEvent(step.x, step.y);
    m_harness.frames(2);
    EXPECT_NEAR(before - tests::Probe::placed().at(12).Min.x, 40.0F * m_harness.context().scale(), 1.0F);
    EXPECT_FALSE(m_harness.patch(2, {{"orientation", "diagonal"}}).hasValue());
}

// A sideways scroll area inside a vertical one takes the wheel while it can move, so a turn over it moves it alone, and a turn beside it still moves the page.
TEST_F(LayoutTest, ASidewaysScrollAreaTakesTheWheelFromTheAreaAroundIt) {
    json cells = json::array();
    json rows = json::array();

    for (std::uint64_t index = 0; index < 10; ++index) {
        cells.push_back(probe(10 + index, {{"contentWidth", 100}, {"contentHeight", 20}}));
    }

    rows.push_back(tests::ComponentSpecs::node(3, "scroll", {{"orientation", "horizontal"}, {"width", 300}, {"align", "start"}}, {tests::ComponentSpecs::node(4, "row", json::object(), cells)}));

    for (std::uint64_t index = 0; index < 10; ++index) {
        rows.push_back(probe(30 + index, {{"contentWidth", 300}, {"contentHeight", 40}}));
    }

    const auto page = tests::ComponentSpecs::node(2, "scroll", {{"height", 200}}, {tests::ComponentSpecs::node(5, "column", json::object(), rows)});
    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "column", json::object(), {page})).hasValue());
    m_harness.frames(2);
    const float scale = m_harness.context().scale();
    const ImVec2 step = ui::WheelScale::units(ImVec2(0.0F, -1.0F), 40.0F);
    const float across = tests::Probe::placed().at(12).Min.x;
    const float down = tests::Probe::placed().at(30).Min.y;

    m_harness.moveTo(ImVec2(100.0F, 10.0F));
    m_harness.frame();
    ImGui::GetIO().AddMouseWheelEvent(step.x, step.y);
    m_harness.frames(2);
    EXPECT_NEAR(across - tests::Probe::placed().at(12).Min.x, 40.0F * scale, 1.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(30).Min.y, down);

    m_harness.moveTo(ImVec2(100.0F, 150.0F));
    m_harness.frames(2);
    ImGui::GetIO().AddMouseWheelEvent(step.x, step.y);
    m_harness.frames(2);
    EXPECT_NEAR(down - tests::Probe::placed().at(30).Min.y, 40.0F * scale, 1.0F);
}

// A step of the wheel moves a scroll area as far as the system scrolls for it on each axis, because the step reaches ImGui in the units ImGui scrolls by.
TEST_F(LayoutTest, AScrollAreaMovesAsFarAsTheSystemScrolls) {
    json cells = json::array();

    for (std::uint64_t index = 0; index < 20; ++index) {
        cells.push_back(probe(10 + index, {{"contentWidth", 900}, {"contentHeight", 40}}));
    }

    const auto scroll = tests::ComponentSpecs::node(2, "scroll", {{"height", 200}}, {tests::ComponentSpecs::node(3, "column", json::object(), cells)});
    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "column", json::object(), {scroll})).hasValue());
    m_harness.frames(2);
    const float scale = m_harness.context().scale();
    m_harness.moveTo(ImVec2(100.0F, 50.0F));

    for (const float points : {10.0F, 30.0F, 45.0F}) {
        const ImVec2 before = tests::Probe::placed().at(12).Min;
        const ImVec2 step = ui::WheelScale::units(ImVec2(0.0F, -1.0F), points);
        ImGui::GetIO().AddMouseWheelEvent(step.x, step.y);
        m_harness.frames(2);
        EXPECT_NEAR(before.y - tests::Probe::placed().at(12).Min.y, points * scale, 1.0F) << points;
    }
}

// A sideways scroll area gives a growing child its own width, since that child has no width of its own to scroll through.
TEST_F(LayoutTest, AHorizontalScrollGivesAGrowingChildItsWidth) {
    const auto row = tests::ComponentSpecs::node(3, "row", json::object(), {probe(4, {{"contentWidth", 100}, {"contentHeight", 20}}), probe(5, {{"grow", 1}, {"contentHeight", 20}})});
    const auto scroll = tests::ComponentSpecs::node(2, "scroll", {{"orientation", "horizontal"}, {"width", 300}, {"align", "start"}}, {row});
    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "column", json::object(), {scroll})).hasValue());
    m_harness.frames(2);

    EXPECT_FLOAT_EQ(tests::Probe::placed().at(5).GetWidth(), 200.0F);
    EXPECT_FLOAT_EQ(m_harness.node(2)->measure(m_harness.context(), tests::InterfaceHarness::width).y, 20.0F);
}

// A shortfall a growing child cannot give back past its minimum passes to the other growing children, so the column never overflows.
TEST_F(LayoutTest, AColumnPassesTheRestOfAShortfallOnOnceAChildStopsAtItsMinimum) {
    constexpr float height = tests::InterfaceHarness::height;
    layout(tests::ComponentSpecs::node(1, "column", {{"padding", 0}, {"spacing", 0}}, {probe(2, {{"grow", 1}, {"contentHeight", 20}, {"minHeight", 10}}), probe(3, {{"grow", 1}, {"contentHeight", 1000}})}));

    EXPECT_FLOAT_EQ(tests::Probe::placed().at(2).GetHeight(), 10.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(3).GetHeight(), height - 10.0F);
}

// A stretched child keeps within its own bounds across the container, in a column and in a row.
TEST_F(LayoutTest, AStretchedChildKeepsWithinItsBounds) {
    EXPECT_FLOAT_EQ(layout(tests::ComponentSpecs::node(1, "column", {{"padding", 0}}, {probe(2, {{"contentWidth", 50}, {"contentHeight", 20}, {"maxWidth", 200}})})).GetWidth(), 200.0F);
    EXPECT_FLOAT_EQ(layout(tests::ComponentSpecs::node(1, "row", {{"padding", 0}}, {probe(2, {{"contentWidth", 50}, {"contentHeight", 20}, {"align", "stretch"}, {"maxHeight", 60}})})).GetHeight(), 60.0F);
}

// A splitter whose panes sit side by side stands as tall as its taller pane rather than as its minimum widths.
TEST_F(LayoutTest, ASideBySideSplitterStandsAsTallAsItsTallerPane) {
    const ImRect first = layout(tests::ComponentSpecs::node(1, "column", {{"padding", 0}}, {tests::ComponentSpecs::node(4, "splitter", {{"orientation", "horizontal"}}, {probe(2, {{"contentHeight", 40}}), probe(3, {{"contentHeight", 70}})})}));

    EXPECT_FLOAT_EQ(first.GetHeight(), 70.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(3).GetHeight(), 70.0F);
}

TEST_F(LayoutTest, ARowThatGrowsNothingJustifiesItsChildren) {
    constexpr float width = tests::InterfaceHarness::width;

    EXPECT_NEAR(layout(tests::ComponentSpecs::node(1, "row", {{"justify", "end"}}, {probe(2, {{"contentWidth", 100}})})).Max.x, width, 1.0F);
    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    EXPECT_NEAR(layout(tests::ComponentSpecs::node(1, "row", {{"justify", "center"}}, {probe(2, {{"contentWidth", 100}})})).GetCenter().x, width / 2.0F, 1.0F);
    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    layout(tests::ComponentSpecs::node(1, "row", {{"justify", "space-between"}}, {probe(2, {{"contentWidth", 100}}), probe(3, {{"contentWidth", 100}})}));
    EXPECT_NEAR(tests::Probe::placed().at(3).Max.x, width, 1.0F);
}

TEST_F(LayoutTest, AColumnAlignsEachChildAcrossItsWidth) {
    constexpr float width = tests::InterfaceHarness::width;
    layout(tests::ComponentSpecs::node(1, "column", {{"padding", 20}}, {probe(2, {{"contentWidth", 100}, {"align", "start"}}), probe(3, {{"contentWidth", 100}, {"align", "center"}}), probe(4, {{"contentWidth", 100}, {"align", "end"}}), probe(5, {{"contentWidth", 100}, {"align", "stretch"}})}));

    EXPECT_NEAR(tests::Probe::placed().at(2).Min.x, 20.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(3).GetCenter().x, width / 2.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(4).Max.x, width - 20.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(5).GetWidth(), width - 40.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(2).Min.y, 20.0F, 1.0F);
}

TEST_F(LayoutTest, AGridPlacesChildrenInEqualColumnsAndAStackDrawsOnlyItsCurrentChild) {
    layout(tests::ComponentSpecs::node(1, "grid", {{"columns", 3}, {"columnSpacing", 10}, {"rowSpacing", 5}}, {probe(2), probe(3), probe(4), probe(5)}));

    const float column = (tests::InterfaceHarness::width - 20.0F) / 3.0F;
    EXPECT_NEAR(tests::Probe::placed().at(3).Min.x, column + 10.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(5).Min.x, 0.0F, 1.0F);
    EXPECT_NEAR(tests::Probe::placed().at(5).Min.y, tests::Probe::placed().at(2).Max.y + 5.0F, 1.0F);

    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    tests::Probe::placed().clear();
    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "stack", {{"current", 1}}, {probe(2), probe(3)})).hasValue());
    m_harness.frame();

    EXPECT_FALSE(tests::Probe::placed().contains(2));
    EXPECT_TRUE(tests::Probe::placed().contains(3));
}

TEST_F(LayoutTest, AFixedSizeAndHiddenChildrenAreRespected) {
    layout(tests::ComponentSpecs::node(1, "column", json::object(), {probe(2, {{"width", 120}, {"height", 30}, {"align", "start"}}), probe(3, {{"visible", false}}), probe(4)}));

    EXPECT_FLOAT_EQ(tests::Probe::placed().at(2).GetWidth(), 120.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(2).GetHeight(), 30.0F);
    EXPECT_FALSE(tests::Probe::placed().contains(3));
    EXPECT_NEAR(tests::Probe::placed().at(4).Min.y, 30.0F, 1.0F);
}

// A splitter divides its rectangle between two panes, and a hidden pane leaves the whole rectangle to the other one.
// The buttons of a settings form start where its captions start, because they take the inset of the form they sit in.
TEST_F(LayoutTest, SettingsActionsTakeTheInsetOfTheirForm) {
    const ImRect button = layout(tests::ComponentSpecs::node(1, "settingsForm", json::object(), {tests::ComponentSpecs::node(4, "settingsRow", {{"label", "Row"}}, {probe(3)}), tests::ComponentSpecs::node(5, "settingsActions", json::object(), {probe(2)})}));

    EXPECT_FLOAT_EQ(button.Min.x, m_harness.context().metric(ThemeMetric::SettingsHorizontalPadding));
}

// A hint under a narrow control such as a toggle reads at the width every hint of the form takes, rather than one word per line.
TEST_F(LayoutTest, AHintUnderANarrowControlTakesTheReadableWidth) {
    const std::string hint = "Wrap long lines at the editor width instead of scrolling horizontally";
    layout(tests::ComponentSpecs::node(1, "settingsForm", json::object(), {tests::ComponentSpecs::node(4, "settingsRow", {{"label", "Word wrap"}, {"hint", hint}}, {probe(2, {{"contentWidth", 40}, {"contentHeight", 20}})})}));

    const ImVec2 row = m_harness.node(4)->measure(m_harness.context(), tests::InterfaceHarness::width);
    EXPECT_LT(row.y, m_harness.context().metric(ThemeMetric::ControlHeight) * 3.0F);
}

// The hint of a settings row ends where its control ends, under a narrow control as under a wide one, with each line right aligned inside the readable width.
TEST_F(LayoutTest, AHintEndsWhereItsControlEnds) {
    const std::string hint = "Wrap long lines at the editor width instead of scrolling horizontally";

    for (const float width : {40.0F, 220.0F}) {
        const ImRect control = layout(tests::ComponentSpecs::node(1, "settingsForm", json::object(), {tests::ComponentSpecs::node(4, "settingsRow", {{"label", "Word wrap"}, {"hint", hint}}, {probe(2, {{"contentWidth", width}, {"contentHeight", 20}})})}));
        const ImU32 muted = m_harness.context().color(ThemeColor::TextMuted).packed();
        ImRect written(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));

        for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
            for (const ImDrawVert& vertex : list->VtxBuffer) {
                if (vertex.col == muted) {
                    written.Add(vertex.pos);
                }
            }
        }

        EXPECT_NEAR(written.Max.x, control.Max.x, 1.0F) << width;
        EXPECT_GT(written.Min.y, control.Max.y) << width;
    }
}

// An alert keeps the padding of a control around its text and draws one rule of two points in the danger color down its left edge.
TEST_F(LayoutTest, AnAlertPadsItsTextAndDrawsOneRuleOnItsLeft) {
    const auto mounted = m_harness.mount(tests::ComponentSpecs::node(1, "column", {{"padding", 0}}, {tests::ComponentSpecs::node(2, "alert", {{"text", "The port is already in use"}})}));
    ASSERT_TRUE(mounted.hasValue());
    m_harness.frames(2);
    const RenderContext& context = m_harness.context();
    const ImVec2 size = m_harness.node(2)->measure(m_harness.context(), tests::InterfaceHarness::width);
    const ImU32 rule = context.color(ThemeColor::Danger).packed();
    const ImU32 fill = context.color(ThemeColor::DangerBackground).packed();
    ImRect drawn(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
    bool filled = false;

    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
        for (const ImDrawVert& vertex : list->VtxBuffer) {
            filled = filled || vertex.col == fill;

            if (vertex.col == rule) {
                drawn.Add(vertex.pos);
            }
        }
    }

    EXPECT_GT(size.y, context.metric(ThemeMetric::ControlVerticalPadding) * 2.0F);
    EXPECT_TRUE(filled);
    EXPECT_FLOAT_EQ(drawn.Min.x, 0.0F);
    EXPECT_FLOAT_EQ(drawn.GetWidth(), 2.0F * context.scale());
    EXPECT_FLOAT_EQ(drawn.GetHeight(), size.y);
}

// An alert of any tone lies on the background of its tone with a rule of that tone, writes in the text color of the tone, and refuses a tone it does not know.
TEST_F(LayoutTest, AnAlertTakesTheBackgroundRuleAndTextOfItsTone) {
    const std::vector<std::tuple<std::string_view, ThemeColor, ThemeColor, ThemeColor>> tones{{"neutral", ThemeColor::Pressed, ThemeColor::TextMuted, ThemeColor::Text}, {"accent", ThemeColor::AccentBackground, ThemeColor::Accent, ThemeColor::AccentText}, {"success", ThemeColor::SuccessBackground, ThemeColor::Success, ThemeColor::SuccessText}, {"warning", ThemeColor::WarningBackground, ThemeColor::Warning, ThemeColor::WarningText}, {"danger", ThemeColor::DangerBackground, ThemeColor::Danger, ThemeColor::DangerText}, {"information", ThemeColor::InformationBackground, ThemeColor::Information, ThemeColor::InformationText}};

    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "column", {{"padding", 0}}, {tests::ComponentSpecs::node(2, "alert", {{"text", "Something to see"}})})).hasValue());

    for (const auto& [tone, background, rule, ink] : tones) {
        ASSERT_TRUE(m_harness.patch(2, {{"tone", tone}}).hasValue());
        m_harness.frames(2);

        EXPECT_TRUE(painted(m_harness.context().color(background).packed())) << tone;
        EXPECT_TRUE(painted(m_harness.context().color(rule).packed())) << tone;
        EXPECT_TRUE(painted(m_harness.context().color(ink).packed())) << tone;
    }

    EXPECT_EQ(m_harness.patch(2, {{"tone", "loud"}}).error().code, "json_field_choice");
}

// A card is outlined in the border color unless it names another role of the theme, and none leaves it without an outline.
TEST_F(LayoutTest, ACardIsOutlinedInTheColorItNames) {
    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "column", {{"padding", 8}}, {tests::ComponentSpecs::node(2, "card", {{"background", "raised"}}, {tests::ComponentSpecs::label(3)})})).hasValue());
    m_harness.frames(2);
    EXPECT_TRUE(painted(m_harness.context().color(ThemeColor::Border).packed()));

    ASSERT_TRUE(m_harness.patch(2, {{"outline", "warning"}}).hasValue());
    m_harness.frames(2);
    EXPECT_TRUE(painted(m_harness.context().color(ThemeColor::Warning).packed()));
    EXPECT_FALSE(painted(m_harness.context().color(ThemeColor::Border).packed()));

    ASSERT_TRUE(m_harness.patch(2, {{"outline", "none"}}).hasValue());
    m_harness.frames(2);
    EXPECT_FALSE(painted(m_harness.context().color(ThemeColor::Warning).packed()));

    EXPECT_EQ(m_harness.patch(2, {{"outline", "loud"}}).error().code, "ui_color_unknown");
}

// A badge filled with a tone writes in the ink made for that fill, so text on yellow is dark rather than white.
TEST_F(LayoutTest, ABadgeWritesOnItsToneInTheInkOfThatTone) {
    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "row", json::object(), {tests::ComponentSpecs::node(2, "badge", {{"text", "Pending"}, {"tone", "warning"}})})).hasValue());
    m_harness.frames(2);

    EXPECT_TRUE(painted(m_harness.context().color(ThemeColor::Warning).packed()));
    EXPECT_TRUE(painted(m_harness.context().color(ThemeColor::OnWarning).packed()));
    EXPECT_FALSE(painted(m_harness.context().color(ThemeColor::OnAccent).packed()));
}

// A progress bar writes the part of its text over the fill in the ink of its tone and the rest in the text color.
TEST_F(LayoutTest, AProgressWritesOverItsFillInTheInkOfItsTone) {
    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "column", json::object(), {tests::ComponentSpecs::node(2, "progress", {{"value", 0.5}, {"text", "Copying the files of the project"}, {"tone", "warning"}, {"showText", true}})})).hasValue());
    m_harness.frames(2);

    EXPECT_TRUE(painted(m_harness.context().color(ThemeColor::Warning).packed()));
    EXPECT_TRUE(painted(m_harness.context().color(ThemeColor::OnWarning).packed()));
    EXPECT_TRUE(painted(m_harness.context().color(ThemeColor::Text).packed()));
}

// A strip of tabs alone leaves the line under it to the row that holds it, and a strip over pages draws the one line between them, so no line is ever doubled.
TEST_F(LayoutTest, ATabStripAndItsContainerDrawOneLine) {
    const json items = json::array({{{"id", "a"}, {"text", "Alpha"}}, {{"id", "b"}, {"text", "Beta"}}});
    // clang-format off
    const auto lines = [this]() {
        const ImU32 border = m_harness.context().color(ThemeColor::Border).packed();
        std::set<float> rows;

        for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
            for (int index = 0; index + 3 < list->VtxBuffer.Size; index += 4) {
                const ImDrawVert* quad = &list->VtxBuffer[index];

                if (quad[0].col == border && quad[2].pos.y - quad[0].pos.y == 1.0F && quad[1].pos.x - quad[0].pos.x > 100.0F) {
                    rows.insert(quad[0].pos.y);
                }
            }
        }

        return rows;
    };
    // clang-format on

    const auto strip = m_harness.mount(tests::ComponentSpecs::node(1, "column", {{"padding", 0}}, {tests::ComponentSpecs::node(2, "row", {{"borders", {"bottom"}}}, {tests::ComponentSpecs::node(3, "tabs", {{"items", items}, {"current", "a"}, {"grow", 1}})})}));
    ASSERT_TRUE(strip.hasValue()) << strip.error().code << " " << strip.error().detail;
    m_harness.frames(2);
    EXPECT_EQ(lines().size(), 1U);

    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    ASSERT_TRUE(m_harness.mount(tests::ComponentSpecs::node(1, "column", {{"padding", 0}}, {tests::ComponentSpecs::node(3, "tabs", {{"items", items}, {"current", "a"}}, {probe(4), probe(5)})})).hasValue());
    m_harness.frames(2);
    EXPECT_EQ(lines().size(), 1U);
}

// A form field writes its label above its control, so the control starts under the label at the left edge and takes the whole width.
TEST_F(LayoutTest, AFormFieldPutsItsLabelAboveItsControl) {
    const ImRect control = layout(tests::ComponentSpecs::node(1, "column", {{"padding", 0}}, {tests::ComponentSpecs::node(3, "formField", {{"label", "Name"}, {"hint", "Letters and numbers"}}, {probe(2, {{"contentHeight", 20}})})}));
    const ImVec2 field = m_harness.node(3)->measure(m_harness.context(), tests::InterfaceHarness::width);

    EXPECT_FLOAT_EQ(control.Min.x, 0.0F);
    EXPECT_GT(control.Min.y, m_harness.context().metric(ThemeMetric::ControlVerticalPadding));
    EXPECT_FLOAT_EQ(control.GetWidth(), tests::InterfaceHarness::width);
    EXPECT_GT(field.y, control.Max.y);
}

// A field whose control keeps its own width is only as wide as that control, so a growing field beside it in a row takes the rest.
TEST_F(LayoutTest, AFormFieldInARowLeavesTheRestToItsNeighbour) {
    const auto mounted = m_harness.mount(tests::ComponentSpecs::node(1, "row", {{"spacing", 0}}, {tests::ComponentSpecs::node(4, "formField", {{"label", "Host"}, {"grow", 1}}, {probe(2)}), tests::ComponentSpecs::node(5, "formField", {{"label", "Port"}}, {probe(3, {{"contentWidth", 90}, {"contentHeight", 20}, {"align", "start"}})})}));
    ASSERT_TRUE(mounted.hasValue()) << mounted.error().code << " " << mounted.error().detail;
    m_harness.frame();

    EXPECT_FLOAT_EQ(tests::Probe::placed().at(3).GetWidth(), 90.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(2).GetWidth(), tests::InterfaceHarness::width - 90.0F);
}

// A control given a width keeps it in a form field and in a column instead of stretching, so a growing field beside it in a row still takes the rest.
TEST_F(LayoutTest, AControlGivenAWidthKeepsItInsteadOfStretching) {
    const json row = tests::ComponentSpecs::node(6, "row", {{"spacing", 0}}, {tests::ComponentSpecs::node(4, "formField", {{"label", "Host"}, {"grow", 1}}, {probe(2)}), tests::ComponentSpecs::node(5, "formField", {{"label", "Port"}}, {probe(3, {{"width", 90}, {"contentHeight", 20}})})});
    const auto mounted = m_harness.mount(tests::ComponentSpecs::node(1, "column", {{"padding", 0}}, {row, probe(7, {{"width", 120}, {"contentHeight", 20}})}));
    ASSERT_TRUE(mounted.hasValue()) << mounted.error().code << " " << mounted.error().detail;
    m_harness.frame();

    EXPECT_FLOAT_EQ(tests::Probe::placed().at(3).GetWidth(), 90.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(2).GetWidth(), tests::InterfaceHarness::width - 90.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(7).GetWidth(), 120.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(7).Min.x, 0.0F);
}

TEST_F(LayoutTest, ASplitterGivesEverythingToTheOnlyPaneShown) {
    const ImRect first = layout(tests::ComponentSpecs::node(1, "splitter", {{"orientation", "horizontal"}, {"ratio", 0.25}, {"height", 100}}, {probe(2, {{"grow", 1}}), probe(3, {{"grow", 1}})}));
    const float total = tests::InterfaceHarness::width;

    EXPECT_NEAR(first.GetWidth(), total * 0.25F, 2.0F);
    EXPECT_GT(tests::Probe::placed().at(3).Min.x, first.Max.x);

    tests::Probe::placed().clear();
    ASSERT_TRUE(m_harness.patch(2, {{"visible", false}}).hasValue());
    m_harness.frame();

    EXPECT_FALSE(tests::Probe::placed().contains(2));
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(3).Min.x, 0.0F);
    EXPECT_FLOAT_EQ(tests::Probe::placed().at(3).GetWidth(), total);
}

} // namespace workpane::ui
