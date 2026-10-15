#pragma once

#include <imgui.h>

#include <string>

namespace workpane::tests {

class FakeClipboard final {
  public:
    void install(ImGuiContext& context);

  private:
    [[nodiscard]] static const char* read(ImGuiContext* context);
    static void write(ImGuiContext* context, const char* text);

    std::string m_text;
};

} // namespace workpane::tests
