# Draws nothing and takes no room for a character Unicode declares default ignorable, such as a variation selector after an emoji, when no face holds it, instead of the fallback glyph.
# The configure step fails when a passage this patch replaces is missing, so a new pin of the library cannot skip it silently, and a passage already replaced is left as it is.
function(replace_passage anchor replacement)
    file(READ "${SOURCE}/imgui_draw.cpp" content)
    string(FIND "${content}" "${replacement}" patched)
    string(FIND "${content}" "${anchor}" found)

    if(NOT patched EQUAL -1)
        return()
    endif()

    if(found EQUAL -1)
        message(FATAL_ERROR "The text library no longer holds the passage the ignorable character patch replaces: ${anchor}")
    endif()

    string(REPLACE "${anchor}" "${replacement}" content "${content}")
    file(WRITE "${SOURCE}/imgui_draw.cpp" "${content}")
endfunction()

replace_passage([=[
static ImFontGlyph* ImFontBaked_BuildLoadGlyph(ImFontBaked* baked, ImWchar codepoint, float* only_load_advance_x)
]=] [=[
// The characters Unicode declares default ignorable, such as variation selectors, joiners and bidirectional marks, which draw nothing when no font shows them.
static bool ImFontBaked_IsDefaultIgnorable(ImWchar codepoint)
{
    static const ImWchar ranges[][2] = { { 0x00AD, 0x00AD }, { 0x034F, 0x034F }, { 0x061C, 0x061C }, { 0x115F, 0x1160 }, { 0x17B4, 0x17B5 }, { 0x180B, 0x180F }, { 0x200B, 0x200F }, { 0x202A, 0x202E }, { 0x2060, 0x206F }, { 0x3164, 0x3164 }, { 0xFE00, 0xFE0F }, { 0xFEFF, 0xFEFF }, { 0xFFA0, 0xFFA0 }, { 0xFFF0, 0xFFF8 }, { 0x1BCA0, 0x1BCA3 }, { 0x1D173, 0x1D17A }, { 0xE0000, 0xE0FFF } };
    for (const ImWchar* range : ranges)
        if (codepoint >= range[0] && codepoint <= range[1])
            return true;
    return false;
}

static ImFontGlyph* ImFontBaked_BuildLoadGlyph(ImFontBaked* baked, ImWchar codepoint, float* only_load_advance_x)
]=])

replace_passage([=[
    if (baked->LoadNoFallback)
        return NULL;
    if (baked->FallbackGlyphIndex == -1)
        ImFontAtlasBuildSetupFontBakedFallback(baked);
]=] [=[
    if (baked->LoadNoFallback)
        return NULL;

    // A default ignorable character no source holds takes no room and draws nothing instead of the fallback glyph, while a font asked whether it holds one still answers that it does not.
    if (ImFontBaked_IsDefaultIgnorable(codepoint))
    {
        if (only_load_advance_x != NULL)
        {
            *only_load_advance_x = 0.0f;
            ImFontAtlasBakedAddFontGlyphAdvancedX(atlas, baked, NULL, codepoint, 0.0f);
            return NULL;
        }
        ImFontGlyph glyph_buf;
        glyph_buf.Codepoint = src_codepoint;
        return ImFontAtlasBakedAddFontGlyph(atlas, baked, NULL, &glyph_buf);
    }

    if (baked->FallbackGlyphIndex == -1)
        ImFontAtlasBuildSetupFontBakedFallback(baked);
]=])
