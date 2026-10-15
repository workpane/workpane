#pragma once

namespace workpane::ui {

enum class FontFace { Regular, SemiBold, Italic, SemiBoldItalic, Monospace, MonospaceBold, MonospaceItalic, MonospaceBoldItalic, Icon };

struct FontRole final {
    FontFace face{FontFace::Regular};
    float size{12.0F};
};

} // namespace workpane::ui
