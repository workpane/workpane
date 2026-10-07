#include "support/ComponentSpecs.h"
#include "support/InterfaceHarness.h"
#include "support/Probe.h"
#include "ui/model/Component.h"
#include "ui/model/ComponentRegistry.h"
#include "ui/model/SurfaceStore.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace workpane::ui {

using nlohmann::json;

TEST(Components, EveryRegisteredKindHasASampleThatMountsMeasuresAndDraws) {
    tests::InterfaceHarness harness;
    const auto samples = tests::ComponentSpecs::samples();

    for (const auto& kind : harness.registry().kinds()) {
        ASSERT_TRUE(samples.contains(kind)) << "no sample for " << kind;
    }

    for (const auto& [kind, tree] : samples) {
        const auto mounted = harness.mount(tree);
        ASSERT_TRUE(mounted.hasValue()) << kind << ": " << mounted.error().code << " " << mounted.error().message << " " << mounted.error().detail;
        harness.frames(3);

        const ImVec2 size = harness.node(1)->measure(harness.context(), tests::InterfaceHarness::width);
        EXPECT_GE(size.x, 0.0F) << kind;
        EXPECT_GE(size.y, 0.0F) << kind;
        ASSERT_TRUE(harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", harness.context()).hasValue());
    }

    EXPECT_GE(harness.webViews().created, 1);
    EXPECT_FALSE(harness.webViews().documents.empty());
}

TEST(Components, RefuseUnknownPropertiesWrongTypesAndUnknownKinds) {
    tests::InterfaceHarness harness;

    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "button", {{"txt", "typo"}})).error().code, "json_field_unknown");
    EXPECT_FALSE(harness.mount(tests::ComponentSpecs::node(1, "button", {{"text", 5}})).hasValue());
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "button", {{"text", {{"key", "Not A Key"}}}})).error().code, "ui_text_key_invalid");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "button", {{"variant", "huge"}})).error().code, "json_field_choice");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "table", {{"columns", json::array()}, {"rows", json::array()}, {"selection", "loud"}})).error().code, "json_field_choice");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "icon", {{"name", "not-an-icon"}})).error().code, "ui_icon_unknown");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "label", {{"color", "pink"}})).error().code, "ui_color_unknown");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "image", {{"source", "../outside.png"}})).error().code, "ui_image_source_invalid");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "image", json::object())).error().code, "ui_image_source_missing");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "button", {{"text", "Open"}, {"menu", json::array({{{"id", "copy"}, {"text", "Copy"}}, {{"id", "copy"}, {"text", "Copy again"}}})}})).error().code, "ui_item_duplicate");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "table", {{"columns", json::array({{{"id", "name"}, {"title", "Name"}}})}, {"rows", json::array({{{"id", "a"}, {"cells", json::array({"A"})}, {"actions", json::array({{{"id", "remove"}, {"icon", "close"}}, {{"id", "remove"}, {"icon", "trash"}}})}}})}})).error().code, "ui_action_duplicate");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "tree", {{"items", json::array({{{"id", "a"}, {"text", "A"}, {"children", json::array({{{"id", "b"}, {"text", "B"}}})}}})}, {"selected", "c"}})).error().code, "ui_item_unknown");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "colorField", {{"value", "#-f0000"}})).error().code, "ui_color_invalid");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "colorField", json::object())).error().code, "json_field_missing");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "scroll", {{"orientation", "horizontal"}})).error().code, "ui_scroll_content");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "stack", {{"current", 2}}, {tests::ComponentSpecs::label(2), tests::ComponentSpecs::label(3)})).error().code, "ui_stack_current");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "unknownKind")).error().code, "ui_kind_unknown");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "button", json::object(), {tests::ComponentSpecs::label(2)})).error().code, "ui_children_refused");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "column", json::object(), {tests::ComponentSpecs::label(2), tests::ComponentSpecs::label(2)})).error().code, "ui_node_duplicate");
    EXPECT_EQ(harness.mount(tests::ComponentSpecs::node(1, "settingsRow", {{"label", "Row"}})).error().code, "ui_settings_row_control");
    EXPECT_FALSE(harness.surfaces().contains(tests::InterfaceHarness::surface));
}

// A patch changes a node only when every property of it holds against the node and its children, and a refused patch leaves every property as it was, the common ones as well.
TEST(Components, ApplyAPatchOnlyWhenEveryPropertyOfItIsValid) {
    tests::InterfaceHarness harness;
    // clang-format off
    harness.registry().add("probe", [](ui::NodeId id) { return std::make_unique<tests::Probe>(id); });
    // clang-format on
    tests::Probe::placed().clear();
    ASSERT_TRUE(harness.mount(tests::ComponentSpecs::node(1, "stack", {{"current", 0}}, {tests::ComponentSpecs::node(2, "probe"), tests::ComponentSpecs::node(3, "probe")})).hasValue());

    EXPECT_FALSE(harness.patch(1, {{"current", 1}, {"visible", false}, {"surplus", true}}).hasValue());
    EXPECT_EQ(harness.patch(1, {{"current", 2}}).error().code, "ui_stack_current");
    EXPECT_EQ(harness.patch(1, {{"visible", false}, {"current", 7}}).error().code, "ui_stack_current");
    EXPECT_FALSE(harness.patch(99, {{"current", 1}}).hasValue());
    harness.frame();
    EXPECT_TRUE(tests::Probe::placed().contains(2));
    EXPECT_FALSE(tests::Probe::placed().contains(3));

    tests::Probe::placed().clear();
    ASSERT_TRUE(harness.patch(1, {{"current", 1}}).hasValue());
    harness.frame();
    EXPECT_FALSE(tests::Probe::placed().contains(2));
    EXPECT_TRUE(tests::Probe::placed().contains(3));
}

TEST(Components, ReplaceChildrenAndBoundTheDepthOfATree) {
    tests::InterfaceHarness harness;
    ASSERT_TRUE(harness.mount(tests::ComponentSpecs::node(1, "column", json::object(), {tests::ComponentSpecs::label(2)})).hasValue());
    ASSERT_TRUE(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, json::array({tests::ComponentSpecs::label(3), tests::ComponentSpecs::label(4)}), json::object(), harness.context()).hasValue());

    EXPECT_EQ(harness.node(2), nullptr);
    EXPECT_NE(harness.node(4), nullptr);
    EXPECT_FALSE(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "someone-else", 1, json::array(), json::object(), harness.context()).hasValue());

    json deep = tests::ComponentSpecs::label(1000);

    for (std::uint64_t id = 999; id > 900; --id) {
        deep = tests::ComponentSpecs::node(id, "column", json::object(), {deep});
    }

    ASSERT_TRUE(harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", harness.context()).hasValue());
    EXPECT_EQ(harness.mount(deep).error().code, "ui_tree_too_deep");
}

// Children replaced below a deep node, or a kept subtree moved deeper, count their depth from the root of the surface.
TEST(Components, BoundTheDepthOfChildrenReplacedBelowTheRoot) {
    tests::InterfaceHarness harness;
    ASSERT_TRUE(harness.mount(tests::ComponentSpecs::node(1, "column", json::object())).hasValue());

    for (std::uint64_t id = 1; id <= ui::SurfaceStore::maximumDepth; ++id) {
        ASSERT_TRUE(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", id, json::array({tests::ComponentSpecs::node(id + 1, "column", json::object())}), json::object(), harness.context()).hasValue()) << id;
    }

    constexpr std::uint64_t deepest = ui::SurfaceStore::maximumDepth + 1;
    EXPECT_EQ(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", deepest, json::array({tests::ComponentSpecs::label(deepest + 1)}), json::object(), harness.context()).error().code, "ui_tree_too_deep");

    const json wrapped = json::array({tests::ComponentSpecs::node(900, "column", json::object(), {{{"id", 2}, {"kept", true}}})});
    EXPECT_EQ(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, wrapped, json::object(), harness.context()).error().code, "ui_tree_too_deep");
    EXPECT_EQ(harness.node(1)->children().front()->id(), 2U);
}

// The bound on the nodes of a surface counts the nodes that stay beside the children a replacement builds.
TEST(Components, BoundTheNodesOfASurfaceAcrossReplacements) {
    tests::InterfaceHarness harness;
    constexpr std::uint64_t half = ui::SurfaceStore::maximumNodes / 2 + 1;
    json first = json::array();
    json second = json::array();

    for (std::uint64_t index = 0; index < half; ++index) {
        first.push_back(tests::ComponentSpecs::label(10 + index));
        second.push_back(tests::ComponentSpecs::label(10 + half + index));
    }

    ASSERT_TRUE(harness.mount(tests::ComponentSpecs::node(1, "column", json::object(), {tests::ComponentSpecs::node(2, "column", json::object(), first), tests::ComponentSpecs::node(3, "column", json::object())})).hasValue());

    EXPECT_EQ(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 3, second, json::object(), harness.context()).error().code, "ui_tree_too_large");
    EXPECT_TRUE(harness.node(3)->children().empty());
}

// A child the new set keeps is the same component with its native page, and a refused set leaves every child where it was.
TEST(Components, KeepTheChildrenAReplacementNamesAsKept) {
    tests::InterfaceHarness harness;
    const json page = tests::ComponentSpecs::node(3, "webView", {{"url", "https://example.com/"}, {"height", 200}});
    ASSERT_TRUE(harness.mount(tests::ComponentSpecs::node(1, "splitter", json::object(), {tests::ComponentSpecs::label(2), page})).hasValue());
    harness.frames(2);
    const ui::Component* kept = harness.node(3);
    const int created = harness.webViews().created;

    const json swapped = json::array({{{"id", 3}, {"kept", true}}, tests::ComponentSpecs::label(4)});
    ASSERT_TRUE(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, swapped, json::object(), harness.context()).hasValue());
    harness.frames(2);

    EXPECT_EQ(harness.node(3), kept);
    EXPECT_EQ(harness.node(2), nullptr);
    EXPECT_EQ(harness.node(1)->children().front()->id(), 3U);
    EXPECT_EQ(harness.webViews().created, created);

    // A splitter refuses one child, so the refused set puts the kept page back in its place.
    EXPECT_FALSE(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, json::array({{{"id", 3}, {"kept", true}}}), json::object(), harness.context()).hasValue());
    EXPECT_EQ(harness.node(1)->children().size(), 2U);
    EXPECT_EQ(harness.node(1)->children().front()->id(), 3U);
    EXPECT_EQ(harness.node(1)->children().back()->id(), 4U);

    EXPECT_EQ(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, json::array({{{"id", 9}, {"kept", true}}, tests::ComponentSpecs::label(5)}), json::object(), harness.context()).error().code, "ui_child_not_kept");
    EXPECT_EQ(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, json::array({{{"id", 3}, {"kept", true}}, {{"id", 3}, {"kept", true}}}), json::object(), harness.context()).error().code, "ui_child_not_kept");
    EXPECT_EQ(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, json::array({{{"id", 3}, {"kept", true}}, tests::ComponentSpecs::label(3)}), json::object(), harness.context()).error().code, "ui_node_duplicate");
}

// A node deep inside the replaced children moves into a new container with its state, and the containers it left are released.
TEST(Components, MoveAKeptNodeIntoANewContainerAnywhereInsideTheReplacedChildren) {
    tests::InterfaceHarness harness;
    const json page = tests::ComponentSpecs::node(4, "webView", {{"url", "https://example.com/"}, {"height", 200}});
    const json holder = tests::ComponentSpecs::node(3, "column", json::object(), {page, tests::ComponentSpecs::label(5)});
    ASSERT_TRUE(harness.mount(tests::ComponentSpecs::node(1, "column", json::object(), {tests::ComponentSpecs::node(2, "row", json::object(), {holder})})).hasValue());
    harness.frames(2);
    const ui::Component* kept = harness.node(4);
    const int created = harness.webViews().created;
    const json moved = json::array({tests::ComponentSpecs::node(6, "card", json::object(), {{{"id", 4}, {"kept", true}}}), tests::ComponentSpecs::label(3)});

    ASSERT_TRUE(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, moved, json::object(), harness.context()).hasValue());
    harness.frames(2);
    EXPECT_EQ(harness.node(4), kept);
    EXPECT_EQ(harness.node(6)->children().front().get(), kept);
    EXPECT_EQ(harness.node(2), nullptr);
    EXPECT_EQ(harness.node(5), nullptr);
    EXPECT_EQ(harness.node(3)->kind(), "label");
    EXPECT_EQ(harness.webViews().created, created);

    // A splitter refuses one child, so the kept node goes back into the card it came from.
    const json refused = json::array({tests::ComponentSpecs::node(7, "splitter", json::object(), {{{"id", 4}, {"kept", true}}})});
    EXPECT_FALSE(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, refused, json::object(), harness.context()).hasValue());
    EXPECT_EQ(harness.node(6)->children().front().get(), kept);
    EXPECT_EQ(harness.node(7), nullptr);

    // A node inside another kept node, and a node outside the replaced children, cannot move on their own.
    const json nested = json::array({{{"id", 6}, {"kept", true}}, tests::ComponentSpecs::node(8, "card", json::object(), {{{"id", 4}, {"kept", true}}})});
    EXPECT_EQ(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, nested, json::object(), harness.context()).error().code, "ui_child_not_kept");
    EXPECT_EQ(harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 6, json::array({{{"id", 3}, {"kept", true}}}), json::object(), harness.context()).error().code, "ui_child_not_kept");
    EXPECT_EQ(harness.node(6)->children().front().get(), kept);
}

} // namespace workpane::ui
