#include "platform/ApplicationMenu.h"
#include "platform/NativeViews.h"
#include "support/InterfaceHarness.h"
#include "support/TemporaryDirectory.h"
#include "ui/NativeViewHost.h"
#include "ui/NativeWebView.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <gtest/gtest.h>
#include <imgui.h>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace workpane::platform {

class NativeBrowserInputTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_harness = std::make_unique<tests::InterfaceHarness>();
        ImGui::GetIO().ConfigMacOSXBehaviors = true;
        glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
        ASSERT_EQ(glfwInit(), GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(800, 600, "Workpane", nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);
        m_host = NativeViews::create(m_window, false, m_data.path() / "web", m_data.path() / "downloads");
        auto created = m_host->createWebView();
        ASSERT_TRUE(created.hasValue());
        m_view = std::move(created.value());
        m_view->place(ImRect(0.0F, 40.0F, 800.0F, 600.0F), true);
        NSWindow* window = glfwGetCocoaWindow(m_window);
        m_browser = window.contentView.subviews.lastObject.subviews.firstObject;
        ASSERT_TRUE([m_browser isKindOfClass:[WKWebView class]]);
        ASSERT_TRUE([window makeFirstResponder:m_browser]);
        m_host->keyboard().shortcuts({ImGuiMod_Ctrl | ImGuiKey_W, ImGuiMod_Ctrl | ImGuiKey_R});
        m_harness->frames(2);
    }

    void TearDown() override {
        m_view.reset();
        m_host.reset();

        if (m_window != nullptr) {
            glfwDestroyWindow(m_window);
            glfwTerminate();
        }

        m_harness.reset();

        if (m_clipboard != nil) {
            [NSPasteboard.generalPasteboard clearContents];
            [NSPasteboard.generalPasteboard writeObjects:m_clipboard];
            NSApp.mainMenu = m_menu;
            [NSApp setActivationPolicy:m_activation];
        }
    }

    void key(NSString* character, unsigned short code, bool down) {
        NSWindow* window = glfwGetCocoaWindow(m_window);
        const NSEventModifierFlags flags = down ? NSEventModifierFlagCommand : 0;
        NSEvent* event = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp location:NSZeroPoint modifierFlags:flags timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:window.windowNumber context:nil characters:character charactersIgnoringModifiers:character isARepeat:NO keyCode:code];
        [NSApp postEvent:event atStart:NO];
        glfwPollEvents();
        m_harness->frames(2);
    }

    void editing() {
        m_menu = NSApp.mainMenu;
        m_activation = NSApp.activationPolicy;
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSMutableArray<NSPasteboardItem*>* clipboard = [[NSMutableArray alloc] init];

        for (NSPasteboardItem* original in NSPasteboard.generalPasteboard.pasteboardItems) {
            NSPasteboardItem* copied = [[NSPasteboardItem alloc] init];

            for (NSPasteboardType type in original.types) {
                NSData* data = [original dataForType:type];

                if (data != nil) {
                    [copied setData:data forType:type];
                }
            }

            [clipboard addObject:copied];
        }

        m_clipboard = clipboard;
        ApplicationMenu::install({"About Workpane", "Services", "Hide Workpane", "Hide Others", "Show All", "Quit Workpane", "Window", "Minimize", "Zoom", "Bring All to Front", "Edit", "Undo", "Redo", "Cut", "Copy", "Paste", "Select All"});
        glfwShowWindow(m_window);
        glfwFocusWindow(m_window);
        glfwPollEvents();
        ASSERT_TRUE([glfwGetCocoaWindow(m_window) makeFirstResponder:m_browser]);
    }

    [[nodiscard]] bool edit(NSString* character, bool shift = false) {
        if (!ready(character, shift)) {
            return false;
        }

        NSWindow* window = glfwGetCocoaWindow(m_window);
        const NSEventModifierFlags flags = NSEventModifierFlagCommand | (shift ? NSEventModifierFlagShift : 0);
        NSString* characters = shift ? character.uppercaseString : character;
        NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:flags timestamp:0 windowNumber:window.windowNumber context:nil characters:characters charactersIgnoringModifiers:characters isARepeat:NO keyCode:0];
        return [NSApp.mainMenu performKeyEquivalent:event];
    }

    [[nodiscard]] bool ready(NSString* character, bool shift = false) {
        NSMenu* menu = [NSApp.mainMenu itemAtIndex:1].submenu;
        NSMenuItem* found = nil;

        for (NSMenuItem* item in menu.itemArray) {
            const bool shifted = (item.keyEquivalentModifierMask & NSEventModifierFlagShift) != 0 || ![item.keyEquivalent isEqualToString:item.keyEquivalent.lowercaseString];

            if ([item.keyEquivalent.lowercaseString isEqualToString:character] && shifted == shift) {
                found = item;
                break;
            }
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (found != nil && std::chrono::steady_clock::now() < deadline) {
            [menu update];

            if (found.enabled) {
                return true;
            }

            glfwPollEvents();
            m_harness->frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return false;
    }

    [[nodiscard]] std::optional<std::string> evaluate(NSString* script) {
        __block NSString* result = nil;
        __block bool finished = false;
        [(WKWebView*)m_browser evaluateJavaScript:script
                                completionHandler:^(id value, NSError* failure) {
                                  result = failure == nil && value != nil ? [value description] : nil;
                                  finished = true;
                                }];
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (!finished && std::chrono::steady_clock::now() < deadline) {
            glfwPollEvents();
            m_harness->frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return result != nil ? std::optional<std::string>(result.UTF8String) : std::nullopt;
    }

    [[nodiscard]] bool waitFor(NSString* script, std::string_view expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < deadline) {
            const auto value = evaluate(script);

            if (value.has_value() && *value == expected) {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] std::string editingState() {
        NSWindow* window = glfwGetCocoaWindow(m_window);
        const auto document = evaluate(@"JSON.stringify({ focus: document.hasFocus(), active: document.activeElement.id, value: document.getElementById('field').value, start: document.getElementById('field').selectionStart, end: document.getElementById('field').selectionEnd })");
        const bool replacement = [[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] isEqualToString:@"Replacement text"];
        const char* responder = window.firstResponder != nil ? NSStringFromClass(window.firstResponder.class).UTF8String : "None";

        return document.value_or("The document could not be inspected") + " App active " + std::to_string(NSApp.active) + " Key window " + std::to_string(window.keyWindow) + " Responder " + responder + " Replacement clipboard " + std::to_string(replacement);
    }

    [[nodiscard]] bool waitForNativeFocus() {
        NSWindow* window = glfwGetCocoaWindow(m_window);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < deadline) {
            NSResponder* responder = window.firstResponder;

            if (NSApp.active && window.keyWindow && [responder isKindOfClass:[NSView class]] && [(NSView*)responder isDescendantOf:m_browser]) {
                return true;
            }

            glfwPollEvents();
            m_harness->frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return false;
    }

    [[nodiscard]] bool waitForClipboard(NSString* expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < deadline) {
            if ([[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] isEqualToString:expected]) {
                return true;
            }

            glfwPollEvents();
            m_harness->frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return false;
    }

    tests::TemporaryDirectory m_data;
    std::unique_ptr<tests::InterfaceHarness> m_harness;
    GLFWwindow* m_window{nullptr};
    std::unique_ptr<ui::NativeViewHost> m_host;
    std::unique_ptr<ui::NativeWebView> m_view;
    NSView* m_browser{nil};
    NSArray<NSPasteboardItem*>* m_clipboard{nil};
    NSMenu* m_menu{nil};
    NSApplicationActivationPolicy m_activation{NSApplicationActivationPolicyProhibited};
};

TEST_F(NativeBrowserInputTest, EditsPageTextThroughTheNativeMenuAndClipboard) {
    editing();
    ASSERT_TRUE(m_view->setHtml("<input id='field' value='Original text'><script>document.getElementById('field').focus()</script>").hasValue());
    ASSERT_TRUE(waitFor(@"document.activeElement.id", "field")) << editingState();
    ASSERT_TRUE(waitForNativeFocus()) << editingState();

    NSWindow* window = glfwGetCocoaWindow(m_window);
    ASSERT_TRUE(NSApp.active);
    ASSERT_TRUE(window.keyWindow);
    ASSERT_TRUE([window.firstResponder isKindOfClass:[NSView class]]);
    ASSERT_TRUE([(NSView*)window.firstResponder isDescendantOf:m_browser]);

    ASSERT_TRUE(edit(@"a"));
    ASSERT_TRUE(waitFor(@"String(document.getElementById('field').selectionEnd - document.getElementById('field').selectionStart)", "13")) << editingState();
    ASSERT_TRUE(edit(@"c"));
    ASSERT_TRUE(waitForClipboard(@"Original text")) << editingState();
    [NSPasteboard.generalPasteboard clearContents];
    ASSERT_TRUE([NSPasteboard.generalPasteboard setString:@"Replacement text" forType:NSPasteboardTypeString]);
    ASSERT_TRUE(edit(@"v"));
    ASSERT_TRUE(waitFor(@"document.getElementById('field').value", "Replacement text")) << editingState();
    ASSERT_TRUE(edit(@"z"));
    ASSERT_TRUE(waitFor(@"document.getElementById('field').value", "Original text")) << editingState();
    ASSERT_TRUE(edit(@"z", true));
    ASSERT_TRUE(waitFor(@"document.getElementById('field').value", "Replacement text")) << editingState();
    ASSERT_TRUE(edit(@"a"));
    ASSERT_TRUE(waitFor(@"String(document.getElementById('field').selectionEnd - document.getElementById('field').selectionStart)", "16")) << editingState();
    ASSERT_TRUE(edit(@"x"));
    ASSERT_TRUE(waitFor(@"document.getElementById('field').value", "")) << editingState();
    ASSERT_TRUE(waitForClipboard(@"Replacement text")) << editingState();
}

TEST_F(NativeBrowserInputTest, RelaysCloseAndReloadWhileThePageHasTheKeyboard) {
    key(@"w", 13, true);
    EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_W));
    EXPECT_TRUE(ImGui::GetIO().KeyCtrl);
    key(@"w", 13, false);
    EXPECT_FALSE(ImGui::IsKeyDown(ImGuiKey_W));
    key(@"r", 15, true);
    EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_R));
    key(@"r", 15, false);
    EXPECT_FALSE(ImGui::IsKeyDown(ImGuiKey_R));
    key(@"c", 8, true);
    EXPECT_FALSE(ImGui::IsKeyDown(ImGuiKey_C));
    key(@"c", 8, false);
}

TEST_F(NativeBrowserInputTest, ReturnsNativeFocusWhenAProductFieldRequestsIt) {
    NSWindow* window = glfwGetCocoaWindow(m_window);
    EXPECT_NE(window.firstResponder, window.contentView);
    m_host->focusProduct();
    EXPECT_EQ(window.firstResponder, window.contentView);
}

TEST_F(NativeBrowserInputTest, LeavesTheTabStripOutsideTheNativePage) {
    NSWindow* window = glfwGetCocoaWindow(m_window);
    EXPECT_EQ([window.contentView hitTest:NSMakePoint(50, 580)], window.contentView);
    EXPECT_NE([window.contentView hitTest:NSMakePoint(50, 100)], window.contentView);
    m_harness->moveTo(ImVec2(50.0F, 20.0F));
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    m_harness->frame();
    m_host->endFrame({});
    EXPECT_EQ(window.firstResponder, window.contentView);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    m_harness->frame();
}

} // namespace workpane::platform
