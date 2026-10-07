#include "ui/FontScope.h"

#include <imgui.h>

namespace workpane::ui {

FontScope::FontScope(const Fonts& fonts, FontRole role) : FontScope(fonts, role.face, role.size) {}

FontScope::FontScope(const Fonts& fonts, FontFace face, float size) {
    ImGui::PushFont(fonts.face(face), fonts.size(face, size));
}

FontScope::FontScope(const Fonts& fonts, FontFace face, float size, std::string_view family) {
    ImGui::PushFont(fonts.face(face, family), fonts.size(face, size, family));
}

FontScope::~FontScope() {
    ImGui::PopFont();
}

} // namespace workpane::ui
