#pragma once

#include "Result.h"
#include "ui/theme/FontRole.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <imgui.h>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// The bundled faces are loaded once into the shared atlas and rasterized on demand at whatever em size a role asks for.
// A monospaced family of the machine joins them once it was read, and a monospaced face asked for with its name comes from it, emboldened and slanted by the rasterizer like the bundled one.
// The bundled monospaced family answers to its own name, so naming it never reads a file of the machine.
class Fonts final {
  public:
    static constexpr std::string_view bundledMonospace{"JetBrains Mono"};

    [[nodiscard]] Result<void> load(ImFontAtlas& atlas, const std::filesystem::path& directory);
    [[nodiscard]] Result<void> adopt(ImFontAtlas& atlas, const std::string& family, std::vector<unsigned char> data);
    [[nodiscard]] bool has(std::string_view family) const;
    [[nodiscard]] ImFont* face(FontFace face) const;
    [[nodiscard]] ImFont* face(FontFace face, std::string_view family) const;
    [[nodiscard]] float size(FontFace face, float em) const;
    [[nodiscard]] float size(FontFace face, float em, std::string_view family) const;

  private:
    struct Face final {
        ImFont* font{nullptr};
        float span{1.0F};
    };

    struct Chosen final {
        unsigned int index{0};
        float span{1.0F};
    };

    struct Family final {
        std::vector<unsigned char> data;
        Face regular;
        Face bold;
        Face italic;
        Face boldItalic;
    };

    [[nodiscard]] static unsigned int loaderFlags(FontFace face);
    [[nodiscard]] static Result<Face> add(ImFontAtlas& atlas, const std::filesystem::path& file, unsigned int flags);
    [[nodiscard]] static Result<float> span(const std::filesystem::path& file);
    [[nodiscard]] static Result<float> span(FT_Face face, const std::string& name);
    [[nodiscard]] static Result<Chosen> choose(const std::vector<unsigned char>& data, const std::string& family);
    [[nodiscard]] const Face& entry(FontFace face) const;
    [[nodiscard]] const Face* familyEntry(FontFace face, std::string_view family) const;

    Face m_regular;
    Face m_semiBold;
    Face m_italic;
    Face m_semiBoldItalic;
    Face m_monospace;
    Face m_monospaceBold;
    Face m_monospaceItalic;
    Face m_monospaceBoldItalic;
    Face m_icon;
    std::map<std::string, Family, std::less<>> m_families;
};

} // namespace workpane::ui
