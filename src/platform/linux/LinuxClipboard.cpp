#include "platform/linux/LinuxClipboard.h"

#include <gtk/gtk.h>
#include <imgui_internal.h>

#include <memory>

namespace workpane::platform {

LinuxClipboard::LinuxClipboard(ImGuiContext& context) : m_context(context), m_reader(context.PlatformIO.Platform_GetClipboardTextFn), m_writer(context.PlatformIO.Platform_SetClipboardTextFn), m_userdata(context.PlatformIO.Platform_ClipboardUserData) {
    auto& platform = m_context.PlatformIO;
    platform.Platform_GetClipboardTextFn = &LinuxClipboard::read;
    platform.Platform_SetClipboardTextFn = &LinuxClipboard::write;
    platform.Platform_ClipboardUserData = this;
}

LinuxClipboard::~LinuxClipboard() {
    auto& platform = m_context.PlatformIO;
    platform.Platform_GetClipboardTextFn = m_reader;
    platform.Platform_SetClipboardTextFn = m_writer;
    platform.Platform_ClipboardUserData = m_userdata;
}

const char* LinuxClipboard::read(ImGuiContext* context) {
    auto& clipboard = *static_cast<LinuxClipboard*>(context->PlatformIO.Platform_ClipboardUserData);
    const std::unique_ptr<gchar, decltype(&g_free)> text(gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)), g_free);
    clipboard.m_text = text != nullptr ? text.get() : "";

    return text != nullptr ? clipboard.m_text.c_str() : nullptr;
}

void LinuxClipboard::write(ImGuiContext*, const char* text) {
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    gtk_clipboard_set_can_store(clipboard, nullptr, 0);
}

} // namespace workpane::platform
