#pragma once

#include <imgui_internal.h>

#include <memory>
#include <string>

namespace workpane::ui {

enum class TextureState { Loading, Ready, Failed };

struct Texture final {
    TextureState state{TextureState::Loading};
    std::unique_ptr<ImTextureData> data;
    int width{0};
    int height{0};
    std::string failure;

    [[nodiscard]] ImTextureRef reference() const;
};

} // namespace workpane::ui
