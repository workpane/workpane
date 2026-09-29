#include "ui/Fonts.h"

#include <imgui_freetype.h>

#include <array>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace workpane::ui {

// ImGui asks FreeType for the height from the descender to the ascender, so a face answers how many of those heights one em spans.
Result<float> Fonts::span(FT_Face face, const std::string& name) {
    if (face->units_per_EM == 0 || face->ascender <= face->descender) {
        return Result<float>::failure({"font_unreadable", "A font declares no vertical metrics", name});
    }

    return Result<float>::success(static_cast<float>(face->ascender - face->descender) / static_cast<float>(face->units_per_EM));
}

Result<float> Fonts::span(const std::filesystem::path& file) {
    FT_Library library{nullptr};

    if (FT_Init_FreeType(&library) != 0) {
        return Result<float>::failure({"font_unreadable", "FreeType could not start", file.string()});
    }

    const std::unique_ptr<FT_LibraryRec_, decltype(&FT_Done_FreeType)> ownedLibrary(library, &FT_Done_FreeType);
    const std::string path = file.string();
    FT_Face face{nullptr};

    if (FT_New_Face(library, path.c_str(), 0, &face) != 0) {
        return Result<float>::failure({"font_unreadable", "A bundled font file could not be read", path});
    }

    const std::unique_ptr<FT_FaceRec_, decltype(&FT_Done_Face)> ownedFace(face, &FT_Done_Face);
    return span(face, path);
}

// A file may hold several faces, so the one of the family that is neither bold nor slanted is chosen, and a face of the family comes before an upright face of another one.
Result<Fonts::Chosen> Fonts::choose(const std::vector<unsigned char>& data, const std::string& family) {
    FT_Library library{nullptr};

    if (FT_Init_FreeType(&library) != 0) {
        return Result<Chosen>::failure({"font_unreadable", "FreeType could not start", family});
    }

    const std::unique_ptr<FT_LibraryRec_, decltype(&FT_Done_FreeType)> ownedLibrary(library, &FT_Done_FreeType);
    const auto size = static_cast<FT_Long>(data.size());
    FT_Long faces = 1;
    int best = -1;
    Chosen chosen;

    for (FT_Long index = 0; index < faces; ++index) {
        FT_Face face{nullptr};

        if (FT_New_Memory_Face(library, data.data(), size, index, &face) != 0) {
            return Result<Chosen>::failure({"font_unreadable", "An installed font could not be read", family});
        }

        const std::unique_ptr<FT_FaceRec_, decltype(&FT_Done_Face)> ownedFace(face, &FT_Done_Face);
        faces = face->num_faces;
        const int rank = (face->family_name != nullptr && family == face->family_name ? 4 : 0) + ((face->style_flags & FT_STYLE_FLAG_BOLD) == 0 ? 2 : 0) + ((face->style_flags & FT_STYLE_FLAG_ITALIC) == 0 ? 1 : 0);
        const auto measured = span(face, family);

        if (rank <= best || !measured.hasValue()) {
            continue;
        }

        best = rank;
        chosen = {static_cast<unsigned int>(index), measured.value()};
    }

    if (best < 0) {
        return Result<Chosen>::failure({"font_unreadable", "An installed font declares no vertical metrics", family});
    }

    return Result<Chosen>::success(chosen);
}

Result<Fonts::Face> Fonts::add(ImFontAtlas& atlas, const std::filesystem::path& file, unsigned int flags) {
    std::error_code error;

    if (!std::filesystem::is_regular_file(file, error)) {
        return Result<Face>::failure({"font_missing", "A bundled font file is missing", file.string()});
    }

    const auto measured = span(file);

    if (!measured.hasValue()) {
        return Result<Face>::failure(measured.error());
    }

    ImFontConfig config;
    config.FontLoaderFlags = flags;
    const std::string path = file.string();
    ImFont* font = atlas.AddFontFromFileTTF(path.c_str(), 0.0F, &config);

    if (font == nullptr) {
        return Result<Face>::failure({"font_unreadable", "A bundled font file could not be read", path});
    }

    return Result<Face>::success({font, measured.value()});
}

// Light hinting snaps glyphs only vertically, which keeps the spacing of the face the way the platform text it replaces reads, and the rasterizer slants the italic faces and emboldens the bold monospaced ones.
unsigned int Fonts::loaderFlags(FontFace face) {
    const unsigned int upright = ImGuiFreeTypeLoaderFlags_LightHinting;
    const unsigned int oblique = ImGuiFreeTypeLoaderFlags_Oblique;
    const unsigned int bold = ImGuiFreeTypeLoaderFlags_Bold;
    const bool slanted = face == FontFace::Italic || face == FontFace::SemiBoldItalic || face == FontFace::MonospaceItalic || face == FontFace::MonospaceBoldItalic;
    const bool emboldened = face == FontFace::MonospaceBold || face == FontFace::MonospaceBoldItalic;
    return upright | (slanted ? oblique : 0U) | (emboldened ? bold : 0U);
}

// The italic faces are the upright ones slanted by the rasterizer, and the bold monospace faces are the regular one emboldened by it, so the product ships no separate files for them.
Result<void> Fonts::load(ImFontAtlas& atlas, const std::filesystem::path& directory) {
    const unsigned int upright = loaderFlags(FontFace::Regular);
    const unsigned int slanted = loaderFlags(FontFace::Italic);
    const unsigned int bold = loaderFlags(FontFace::MonospaceBold);
    const std::filesystem::path regular = directory / "Inter-Regular.ttf";
    const std::filesystem::path semiBold = directory / "Inter-SemiBold.ttf";
    const std::filesystem::path monospace = directory / "JetBrainsMono-Regular.ttf";
    const std::filesystem::path icons = directory / "Lucide.ttf";
    const std::array<std::pair<Face*, Result<Face>>, 9> faces{{{&m_regular, add(atlas, regular, upright)}, {&m_semiBold, add(atlas, semiBold, upright)}, {&m_italic, add(atlas, regular, slanted)}, {&m_semiBoldItalic, add(atlas, semiBold, slanted)}, {&m_monospace, add(atlas, monospace, upright)}, {&m_monospaceBold, add(atlas, monospace, bold)}, {&m_monospaceItalic, add(atlas, monospace, slanted)}, {&m_monospaceBoldItalic, add(atlas, monospace, bold | ImGuiFreeTypeLoaderFlags_Oblique)}, {&m_icon, add(atlas, icons, upright)}}};

    for (const auto& [target, loaded] : faces) {
        if (!loaded.hasValue()) {
            return Result<void>::failure(loaded.error());
        }

        *target = loaded.value();
    }

    return Result<void>::success();
}

const Fonts::Face& Fonts::entry(FontFace face) const {
    switch (face) {
    case FontFace::Regular:
        return m_regular;
    case FontFace::SemiBold:
        return m_semiBold;
    case FontFace::Italic:
        return m_italic;
    case FontFace::SemiBoldItalic:
        return m_semiBoldItalic;
    case FontFace::Monospace:
        return m_monospace;
    case FontFace::MonospaceBold:
        return m_monospaceBold;
    case FontFace::MonospaceItalic:
        return m_monospaceItalic;
    case FontFace::MonospaceBoldItalic:
        return m_monospaceBoldItalic;
    case FontFace::Icon:
        return m_icon;
    }

    return m_regular;
}

// A family is kept with the bytes of its file, which the atlas reads from for as long as the product runs, and its four monospaced faces share them.
Result<void> Fonts::adopt(ImFontAtlas& atlas, const std::string& family, std::vector<unsigned char> data) {
    if (has(family)) {
        return Result<void>::success();
    }

    const auto chosen = choose(data, family);

    if (!chosen.hasValue()) {
        return Result<void>::failure(chosen.error());
    }

    Family& adopted = m_families.emplace(family, Family{std::move(data), {}, {}, {}, {}}).first->second;
    const std::array<std::pair<Face*, FontFace>, 4> faces{{{&adopted.regular, FontFace::Monospace}, {&adopted.bold, FontFace::MonospaceBold}, {&adopted.italic, FontFace::MonospaceItalic}, {&adopted.boldItalic, FontFace::MonospaceBoldItalic}}};

    for (const auto& [target, face] : faces) {
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        config.FontNo = chosen.value().index;
        config.FontLoaderFlags = loaderFlags(face);
        ImFont* font = atlas.AddFontFromMemoryTTF(adopted.data.data(), static_cast<int>(adopted.data.size()), 0.0F, &config);

        // The faces added before the one that failed read from the bytes of the family, so they leave the atlas before those bytes go.
        if (font == nullptr) {
            for (const auto& added : faces) {
                if (added.first->font != nullptr) {
                    atlas.RemoveFont(added.first->font);
                }
            }

            m_families.erase(family);
            return Result<void>::failure({"font_unreadable", "An installed font could not be read", family});
        }

        *target = {font, chosen.value().span};
    }

    return Result<void>::success();
}

bool Fonts::has(std::string_view family) const {
    return m_families.contains(family);
}

ImFont* Fonts::face(FontFace face) const {
    return entry(face).font;
}

ImFont* Fonts::face(FontFace face, std::string_view family) const {
    const Face* found = familyEntry(face, family);
    return found != nullptr ? found->font : entry(face).font;
}

// The monospaced faces of a family that was read replace the bundled ones, and every other face and every family not read yet keep the bundled face.
const Fonts::Face* Fonts::familyEntry(FontFace face, std::string_view family) const {
    const auto found = m_families.find(family);

    if (found == m_families.end()) {
        return nullptr;
    }

    switch (face) {
    case FontFace::Monospace:
        return &found->second.regular;
    case FontFace::MonospaceBold:
        return &found->second.bold;
    case FontFace::MonospaceItalic:
        return &found->second.italic;
    case FontFace::MonospaceBoldItalic:
        return &found->second.boldItalic;
    case FontFace::Regular:
    case FontFace::SemiBold:
    case FontFace::Italic:
    case FontFace::SemiBoldItalic:
    case FontFace::Icon:
        return nullptr;
    }

    return nullptr;
}

// A size is the em size, as in every other text renderer, while ImGui sizes a face by the height it spans from the descender to the ascender.
float Fonts::size(FontFace face, float em) const {
    return em * entry(face).span;
}

float Fonts::size(FontFace face, float em, std::string_view family) const {
    const Face* found = familyEntry(face, family);
    return em * (found != nullptr ? found->span : entry(face).span);
}

} // namespace workpane::ui
