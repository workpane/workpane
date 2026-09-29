#include "platform/InputMethod.h"
#include "support/InputMethodProbe.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <cstdlib>

namespace workpane::platform {

// A hidden window of the window library with the input method of the product attached, on a machine whose input method the test can ask.
class InputMethodTest : public ::testing::Test {
  protected:
    void SetUp() override {
#if !defined(_WIN32)
        if (std::getenv("DISPLAY") == nullptr) {
            GTEST_SKIP() << "No X display runs";
        }

        // The input method Xlib carries answers what it was told without a server, which ibus never does, so the test does not depend on the input method of the machine.
        setenv("XMODIFIERS", "@im=none", 1);
#endif

        ASSERT_EQ(glfwInit(), GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(400, 300, "Workpane", nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);
    }

    void TearDown() override {
        if (m_window != nullptr) {
            glfwDestroyWindow(m_window);
            glfwTerminate();
        }
    }

    GLFWwindow* m_window{nullptr};
};

// The input method of Windows or X11 opens its window at the caret the product placed, without covering the line of the caret.
TEST_F(InputMethodTest, TellsTheInputMethodWhereTheCaretStands) {
    if (!tests::InputMethodProbe::running(m_window)) {
        GTEST_SKIP() << "No input method runs";
    }

    InputMethod method(m_window, {});
    method.place(true, 30.0F, 40.0F, 1.0F, 16.0F);
    const auto placement = tests::InputMethodProbe::placement(m_window);

    ASSERT_TRUE(placement.has_value());
    EXPECT_EQ(placement->x, 30);
    EXPECT_EQ(placement->bottom, 56);
}

} // namespace workpane::platform
