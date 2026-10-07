#include "ui/Texture.h"

#include <imgui.h>

namespace workpane::ui {

ImTextureRef Texture::reference() const {
    return data->GetTexRef();
}

} // namespace workpane::ui
