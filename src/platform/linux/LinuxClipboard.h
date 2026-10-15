#pragma once

#include <string>

struct ImGuiContext;

namespace workpane::platform {

// GTK owns clipboard transfers for the native browser and the product, so a transfer never waits on the other toolkit's event loop.
class LinuxClipboard final {
  public:
    explicit LinuxClipboard(ImGuiContext& context);
    ~LinuxClipboard();

    LinuxClipboard(const LinuxClipboard&) = delete;
    LinuxClipboard& operator=(const LinuxClipboard&) = delete;

  private:
    using Reader = const char* (*)(ImGuiContext*);
    using Writer = void (*)(ImGuiContext*, const char*);

    [[nodiscard]] static const char* read(ImGuiContext* context);
    static void write(ImGuiContext* context, const char* text);

    ImGuiContext& m_context;
    Reader m_reader;
    Writer m_writer;
    void* m_userdata;
    std::string m_text;
};

} // namespace workpane::platform
