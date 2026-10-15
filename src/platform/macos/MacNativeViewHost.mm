#include "platform/macos/MacNativeViewHost.h"

#include "platform/macos/MacWebView.h"
#include "platform/posix/WebViewEngine.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <utility>

namespace workpane::platform {

MacNativeViewHost::MacNativeViewHost(GLFWwindow* window, bool debug, std::filesystem::path dataDirectory, std::filesystem::path downloadsDirectory) : m_window(window), m_debug(debug), m_dataDirectory(std::move(dataDirectory)), m_downloadsDirectory(std::move(downloadsDirectory)) {
    NSWindow* product = glfwGetCocoaWindow(m_window);
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged
                                                       handler:^NSEvent*(NSEvent* event) {
                                                         if (event.window != product || product.firstResponder == product.contentView) {
                                                             return event;
                                                         }

                                                         if (event.type == NSEventTypeFlagsChanged) {
                                                             [product.contentView flagsChanged:event];
                                                             return event;
                                                         }

                                                         const NSEventModifierFlags flags = event.modifierFlags;
                                                         const ImGuiKeyChord modifiers = ((flags & NSEventModifierFlagCommand) != 0 ? ImGuiMod_Super : 0) | ((flags & NSEventModifierFlagControl) != 0 ? ImGuiMod_Ctrl : 0) | ((flags & NSEventModifierFlagOption) != 0 ? ImGuiMod_Alt : 0) | ((flags & NSEventModifierFlagShift) != 0 ? ImGuiMod_Shift : 0);
                                                         NSString* characters = event.charactersIgnoringModifiers;
                                                         ImGuiKey key = characters.length == 1 ? ui::NativeKeyInput::character([characters characterAtIndex:0]) : ImGuiKey_None;

                                                         if (characters.length == 1 && [characters characterAtIndex:0] >= NSF1FunctionKey && [characters characterAtIndex:0] <= NSF12FunctionKey) {
                                                             key = static_cast<ImGuiKey>(ImGuiKey_F1 + [characters characterAtIndex:0] - NSF1FunctionKey);
                                                         }

                                                         if (event.keyCode >= 122) {
                                                             switch (event.keyCode) {
                                                             case 123:
                                                                 key = ImGuiKey_LeftArrow;
                                                                 break;
                                                             case 124:
                                                                 key = ImGuiKey_RightArrow;
                                                                 break;
                                                             case 125:
                                                                 key = ImGuiKey_DownArrow;
                                                                 break;
                                                             case 126:
                                                                 key = ImGuiKey_UpArrow;
                                                                 break;
                                                             default:
                                                                 break;
                                                             }
                                                         }

                                                         if (keyboard().relay(key, event.type == NSEventTypeKeyDown, modifiers)) {
                                                             m_tracker.reportActivity();
                                                             return nil;
                                                         }

                                                         return event;
                                                       }];
    m_keyMonitor = (__bridge_retained void*)monitor;
}

MacNativeViewHost::~MacNativeViewHost() {
    [NSEvent removeMonitor:(__bridge_transfer id)m_keyMonitor];
}

Result<std::unique_ptr<ui::NativeWebView>> MacNativeViewHost::createWebView() {
    NSWindow* product = glfwGetCocoaWindow(m_window);
    NSWindow* holder = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:YES];
    holder.releasedWhenClosed = NO;
    auto engine = WebViewEngine::create((__bridge void*)holder, m_debug, m_dataDirectory);

    if (!engine.hasValue()) {
        return Result<std::unique_ptr<ui::NativeWebView>>::failure(engine.error());
    }

    WKWebView* webView = (__bridge WKWebView*)engine.value()->controller();
    return Result<std::unique_ptr<ui::NativeWebView>>::success(std::make_unique<MacWebView>(m_tracker, product.contentView, webView, std::move(engine.value()), holder, m_downloadsDirectory));
}

bool MacNativeViewHost::pump() {
    return m_tracker.takeActivity();
}

// WebKit runs its events on the run loop the product window already waits on, so nothing more is watched.
void MacNativeViewHost::watch() {}

void MacNativeViewHost::focusProduct() {
    NSWindow* product = glfwGetCocoaWindow(m_window);
    [product makeFirstResponder:product.contentView];
    keyboard().release();
}

void MacNativeViewHost::endFrame(const std::vector<ImRect>& floating) {
    m_tracker.endFrame(floating);

    if (m_tracker.clickedOutside()) {
        focusProduct();
    }

    NSWindow* product = glfwGetCocoaWindow(m_window);

    if (product.firstResponder == product.contentView) {
        keyboard().release();
    }
}

} // namespace workpane::platform
