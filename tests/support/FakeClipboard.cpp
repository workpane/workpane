#include "support/FakeClipboard.h"

#include <imgui_internal.h>

namespace workpane::tests {

void FakeClipboard::install(ImGuiContext& context) {
    context.PlatformIO.Platform_GetClipboardTextFn = &FakeClipboard::read;
    context.PlatformIO.Platform_SetClipboardTextFn = &FakeClipboard::write;
    context.PlatformIO.Platform_ClipboardUserData = this;
}

const char* FakeClipboard::read(ImGuiContext* context) {
    const auto& clipboard = *static_cast<FakeClipboard*>(context->PlatformIO.Platform_ClipboardUserData);

    return clipboard.m_text.c_str();
}

void FakeClipboard::write(ImGuiContext* context, const char* text) {
    auto& clipboard = *static_cast<FakeClipboard*>(context->PlatformIO.Platform_ClipboardUserData);
    clipboard.m_text = text;
}

} // namespace workpane::tests
