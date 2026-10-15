# Lets the face of color emoji take any size, which the text library cannot give a face holding only fixed strikes of pictures.
# The em of such a face takes the requested size, the smallest strike reaching it is selected, and its glyphs and their metrics are scaled from that strike.
# The configure step fails when a passage this patch replaces is missing, so a new pin of the library cannot skip it silently, and a passage already replaced is left as it is.
function(replace_passage anchor replacement)
    file(READ "${SOURCE}/misc/freetype/imgui_freetype.cpp" content)
    string(FIND "${content}" "${replacement}" patched)
    string(FIND "${content}" "${anchor}" found)

    if(NOT patched EQUAL -1)
        return()
    endif()

    if(found EQUAL -1)
        message(FATAL_ERROR "The rasterizer of the text library no longer holds the passage the strike patch replaces: ${anchor}")
    endif()

    string(REPLACE "${anchor}" "${replacement}" content "${content}")
    file(WRITE "${SOURCE}/misc/freetype/imgui_freetype.cpp" "${content}")
endfunction()

replace_passage([=[    FT_Size     FtSize;             // This represent a FT_Face with a given size.
    ImGui_ImplFreeType_FontSrcBakedData() { memset((void*)this, 0, sizeof(*this)); }
]=] [=[    FT_Size     FtSize;             // This represent a FT_Face with a given size.
    float       BitmapScale;        // Scale from the selected strike of a color bitmap face to the requested size.
    ImGui_ImplFreeType_FontSrcBakedData() { memset((void*)this, 0, sizeof(*this)); }
]=])

replace_passage([=[
    // Output
]=] [=[
    // A color bitmap face only holds fixed strikes, so its em takes the requested size, the smallest strike reaching it is selected and its glyphs are scaled to it.
    bd_baked_data->BitmapScale = 1.0f;
    FT_Face strike_face = bd_font_data->FtFace;
    if (FT_HAS_COLOR(strike_face) && FT_HAS_FIXED_SIZES(strike_face) && !FT_IS_SCALABLE(strike_face))
    {
        const float em = (float)req.height / 64.0f;
        int strike = 0;
        for (int n = 1; n < strike_face->num_fixed_sizes; n++)
        {
            const float best = (float)strike_face->available_sizes[strike].y_ppem / 64.0f;
            const float candidate = (float)strike_face->available_sizes[n].y_ppem / 64.0f;
            if ((best < em && candidate > best) || (candidate >= em && candidate < best))
                strike = n;
        }
        if (FT_Select_Size(strike_face, strike) == 0)
            bd_baked_data->BitmapScale = em / ((float)strike_face->available_sizes[strike].y_ppem / 64.0f);
    }

    // Output
]=])

replace_passage([=[        FT_Size_Metrics metrics = bd_baked_data->FtSize->metrics;
        const float scale = 1.0f / (rasterizer_density * src->ExtraSizeScale);
        baked->Ascent     = (float)FT_CEIL(metrics.ascender) * scale;       // The pixel extents above the baseline in pixels (typically positive).
]=] [=[        FT_Size_Metrics metrics = bd_baked_data->FtSize->metrics;
        const float scale = bd_baked_data->BitmapScale / (rasterizer_density * src->ExtraSizeScale);
        baked->Ascent     = (float)FT_CEIL(metrics.ascender) * scale;       // The pixel extents above the baseline in pixels (typically positive).
]=])

replace_passage([=[    FT_GlyphSlot slot = face->glyph;
    const float rasterizer_density = src->RasterizerDensity * baked->RasterizerDensity;

    // Load metrics only mode
]=] [=[    FT_GlyphSlot slot = face->glyph;
    const float rasterizer_density = src->RasterizerDensity * baked->RasterizerDensity;
    const float bitmap_scale = ((ImGui_ImplFreeType_FontSrcBakedData*)loader_data_for_baked_src)->BitmapScale;

    // Load metrics only mode
]=])

replace_passage([=[    // Load metrics only mode
    const float advance_x = (slot->advance.x / FT_SCALEFACTOR) / rasterizer_density;
    if (out_advance_x != NULL)
]=] [=[    // Load metrics only mode
    const float advance_x = (slot->advance.x / FT_SCALEFACTOR) / rasterizer_density * bitmap_scale;
    if (out_advance_x != NULL)
]=])

replace_passage([=[    if (error != 0 || ft_bitmap == nullptr)
        return false;

    const int w = (int)ft_bitmap->width;
]=] [=[    if (error != 0 || ft_bitmap == nullptr)
        return false;

    // A glyph of a strike is averaged down to the requested size, pixel areas taken whole in premultiplied color, or repeated up when no strike reaches it.
    FT_Bitmap scaled_bitmap = *ft_bitmap;
    ImVector<unsigned char> scaled_pixels;
    float bitmap_left = (float)slot->bitmap_left;
    float bitmap_top = (float)slot->bitmap_top;
    if (bitmap_scale != 1.0f && ft_bitmap->pixel_mode == FT_PIXEL_MODE_BGRA && ft_bitmap->width != 0 && ft_bitmap->rows != 0 && ft_bitmap->pitch > 0)
    {
        const unsigned int src_w = ft_bitmap->width;
        const unsigned int src_h = ft_bitmap->rows;
        const unsigned int dst_w = ImMax(1u, (unsigned int)(src_w * bitmap_scale + 0.5f));
        const unsigned int dst_h = ImMax(1u, (unsigned int)(src_h * bitmap_scale + 0.5f));
        scaled_pixels.resize((int)(dst_w * dst_h * 4));
        for (unsigned int dy = 0; dy < dst_h; dy++)
            for (unsigned int dx = 0; dx < dst_w; dx++)
            {
                const unsigned int x0 = ImMin(src_w - 1, dx * src_w / dst_w);
                const unsigned int x1 = ImMax(x0 + 1, ImMin(src_w, (dx + 1) * src_w / dst_w));
                const unsigned int y0 = ImMin(src_h - 1, dy * src_h / dst_h);
                const unsigned int y1 = ImMax(y0 + 1, ImMin(src_h, (dy + 1) * src_h / dst_h));
                unsigned int sum[4] = { 0, 0, 0, 0 };
                for (unsigned int sy = y0; sy < y1; sy++)
                    for (unsigned int sx = x0; sx < x1; sx++)
                        for (int c = 0; c < 4; c++)
                            sum[c] += ft_bitmap->buffer[sy * (unsigned int)ft_bitmap->pitch + sx * 4 + c];
                const unsigned int count = (x1 - x0) * (y1 - y0);
                for (int c = 0; c < 4; c++)
                    scaled_pixels[(int)((dy * dst_w + dx) * 4 + c)] = (unsigned char)((sum[c] + count / 2) / count);
            }
        scaled_bitmap.width = dst_w;
        scaled_bitmap.rows = dst_h;
        scaled_bitmap.pitch = (int)(dst_w * 4);
        scaled_bitmap.buffer = scaled_pixels.Data;
        bitmap_left *= bitmap_scale;
        bitmap_top *= bitmap_scale;
        ft_bitmap = &scaled_bitmap;
    }

    const int w = (int)ft_bitmap->width;
]=])

replace_passage([=[        // Register glyph
        float glyph_off_x = (float)face->glyph->bitmap_left;
        float glyph_off_y = (float)-face->glyph->bitmap_top;
        out_glyph->X0 = glyph_off_x * recip_h + font_off_x;
]=] [=[        // Register glyph
        float glyph_off_x = bitmap_left;
        float glyph_off_y = -bitmap_top;
        out_glyph->X0 = glyph_off_x * recip_h + font_off_x;
]=])
