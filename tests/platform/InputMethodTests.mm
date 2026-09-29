#include "platform/InputMethod.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <gtest/gtest.h>

#import <Cocoa/Cocoa.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace workpane::platform {

// A hidden window of the window library with the input method of the product attached, driven through the text input protocol AppKit speaks to it.
class InputMethodTest : public ::testing::Test {
  protected:
    void SetUp() override {
        glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
        ASSERT_EQ(glfwInit(), GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(400, 300, "Workpane", nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        // clang-format off
        m_method = std::make_unique<InputMethod>(m_window, [this](const std::string& text, std::size_t caret) { m_compositions.emplace_back(text, caret); });
        glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int, int action, int) {
            if (action == GLFW_PRESS) {
                static_cast<InputMethodTest*>(glfwGetWindowUserPointer(window))->m_keys.push_back(key);
            }
        });

        glfwSetCharCallback(m_window, [](GLFWwindow* window, unsigned int character) { static_cast<InputMethodTest*>(glfwGetWindowUserPointer(window))->m_characters.push_back(static_cast<char32_t>(character)); });
        // clang-format on
    }

    void TearDown() override {
        m_method.reset();
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    [[nodiscard]] NSView<NSTextInputClient>* view() const {
        return (NSView<NSTextInputClient>*)glfwGetCocoaView(m_window);
    }

    void backspace() const {
        NSWindow* window = glfwGetCocoaWindow(m_window);
        NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil characters:@"\x7f" charactersIgnoringModifiers:@"\x7f" isARepeat:NO keyCode:51];
        [view() keyDown:event];
    }

    GLFWwindow* m_window{nullptr};
    std::unique_ptr<InputMethod> m_method;
    std::vector<std::pair<std::string, std::size_t>> m_compositions;
    std::vector<int> m_keys;
    std::u32string m_characters;
};

// The text being composed reaches the product with the byte offset of its caret, and committing it hands over the characters and ends the composition.
TEST_F(InputMethodTest, HandsOverWhatIsComposedUntilItIsCommitted) {
    [view() setMarkedText:@"かな" selectedRange:NSMakeRange(1, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
    ASSERT_EQ(m_compositions.size(), 1U);
    EXPECT_EQ(m_compositions[0], std::make_pair(std::string("かな"), std::size_t{3}));

    [view() insertText:@"仮名" replacementRange:NSMakeRange(NSNotFound, 0)];
    EXPECT_EQ(m_compositions.back(), std::make_pair(std::string(), std::size_t{0}));
    EXPECT_EQ(m_characters, U"仮名");
}

// The candidate window opens at the caret the product placed, and a caret that went away ends what was being composed.
TEST_F(InputMethodTest, OpensTheCandidatesAtTheCaretAndEndsWithIt) {
    m_method->place(true, 30.0F, 40.0F, 1.0F, 16.0F);
    NSWindow* window = glfwGetCocoaWindow(m_window);
    const NSRect content = [window contentRectForFrameRect:window.frame];
    const NSRect caret = [view() firstRectForCharacterRange:NSMakeRange(0, 0) actualRange:nil];
    EXPECT_DOUBLE_EQ(caret.origin.x, content.origin.x + 30.0);
    EXPECT_DOUBLE_EQ(caret.origin.y, content.origin.y + content.size.height - 40.0 - 16.0);
    EXPECT_DOUBLE_EQ(caret.size.height, 16.0);

    [view() setMarkedText:@"´" selectedRange:NSMakeRange(1, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
    m_method->place(false, 30.0F, 40.0F, 1.0F, 16.0F);
    EXPECT_FALSE([view() hasMarkedText]);
    EXPECT_EQ(m_compositions.back(), std::make_pair(std::string(), std::size_t{0}));
    EXPECT_TRUE(m_characters.empty());
}

// A key pressed while text is being composed belongs to the input method, so the product never deletes or submits what the reader is still composing.
TEST_F(InputMethodTest, LeavesTheKeysToTheInputMethodWhileComposing) {
    backspace();
    EXPECT_EQ(m_keys, std::vector<int>{GLFW_KEY_BACKSPACE});

    [view() setMarkedText:@"´" selectedRange:NSMakeRange(1, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
    backspace();
    EXPECT_EQ(m_keys, std::vector<int>{GLFW_KEY_BACKSPACE});
}

} // namespace workpane::platform
