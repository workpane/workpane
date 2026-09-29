#include "ui/TextCase.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace workpane::ui {

struct TextCase::FoldRange final {
    unsigned int first{0};
    unsigned int last{0};
    std::string_view base;
};

unsigned int TextCase::upperCodepoint(unsigned int codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') {
        return codepoint - 0x20U;
    }

    if (codepoint >= 0xE0U && codepoint <= 0xFEU && codepoint != 0xF7U) {
        return codepoint - 0x20U;
    }

    if (codepoint == 0xFFU) {
        return 0x178U;
    }

    // Latin Extended-A pairs each capital with the small letter after it, apart from the few letters that have no pair.
    const bool pairedEven = (codepoint >= 0x100U && codepoint <= 0x137U) || (codepoint >= 0x14AU && codepoint <= 0x177U);
    const bool pairedOdd = (codepoint >= 0x139U && codepoint <= 0x148U) || (codepoint >= 0x179U && codepoint <= 0x17EU);

    if (pairedEven && codepoint % 2U == 1U && codepoint != 0x131U) {
        return codepoint - 1U;
    }

    if (pairedOdd && codepoint % 2U == 0U) {
        return codepoint - 1U;
    }

    return codepoint;
}

unsigned int TextCase::lowerCodepoint(unsigned int codepoint) {
    if (codepoint >= 'A' && codepoint <= 'Z') {
        return codepoint + 0x20U;
    }

    if (codepoint >= 0xC0U && codepoint <= 0xDEU && codepoint != 0xD7U) {
        return codepoint + 0x20U;
    }

    if (codepoint == 0x178U) {
        return 0xFFU;
    }

    const bool pairedEven = (codepoint >= 0x100U && codepoint <= 0x137U) || (codepoint >= 0x14AU && codepoint <= 0x177U);
    const bool pairedOdd = (codepoint >= 0x139U && codepoint <= 0x148U) || (codepoint >= 0x179U && codepoint <= 0x17EU);

    if (pairedEven && codepoint % 2U == 0U && codepoint != 0x130U) {
        return codepoint + 1U;
    }

    if (pairedOdd && codepoint % 2U == 1U) {
        return codepoint + 1U;
    }

    return codepoint;
}

void TextCase::append(std::string& text, unsigned int codepoint) {
    std::array<char, 5> buffer{};
    const int length = ImTextCharToUtf8(buffer.data(), codepoint);
    text.append(buffer.data(), static_cast<std::size_t>(length));
}

template <typename Mapping> std::string TextCase::map(std::string_view text, Mapping mapping) {
    std::string result;
    result.reserve(text.size());
    const char* cursor = text.data();
    const char* end = text.data() + text.size();

    while (cursor < end) {
        unsigned int codepoint = 0;
        cursor += ImTextCharFromUtf8(&codepoint, cursor, end);
        append(result, mapping(codepoint));
    }

    return result;
}

// Folds a text to the small plain letters a reader compares first, keeping every character without a known base letter as its small form.
std::string TextCase::fold(std::string_view text) {
    // Small letters with a diacritic and the ligatures of Latin-1 and Latin Extended-A, with the plain letters a reader sorts them among.
    static constexpr std::array<FoldRange, 37> foldRanges{{
        {0xDFU, 0xDFU, "ss"}, {0xE0U, 0xE5U, "a"}, {0xE6U, 0xE6U, "ae"}, {0xE7U, 0xE7U, "c"}, {0xE8U, 0xEBU, "e"}, {0xECU, 0xEFU, "i"}, {0xF0U, 0xF0U, "d"}, {0xF1U, 0xF1U, "n"}, {0xF2U, 0xF6U, "o"}, {0xF8U, 0xF8U, "o"}, {0xF9U, 0xFCU, "u"}, {0xFDU, 0xFDU, "y"}, {0xFEU, 0xFEU, "th"}, {0xFFU, 0xFFU, "y"}, {0x100U, 0x105U, "a"}, {0x106U, 0x10DU, "c"}, {0x10EU, 0x111U, "d"}, {0x112U, 0x11BU, "e"}, {0x11CU, 0x123U, "g"}, {0x124U, 0x127U, "h"}, {0x128U, 0x131U, "i"}, {0x132U, 0x133U, "ij"}, {0x134U, 0x135U, "j"}, {0x136U, 0x138U, "k"}, {0x139U, 0x142U, "l"}, {0x143U, 0x14BU, "n"}, {0x14CU, 0x151U, "o"}, {0x152U, 0x153U, "oe"}, {0x154U, 0x159U, "r"}, {0x15AU, 0x161U, "s"}, {0x162U, 0x167U, "t"}, {0x168U, 0x173U, "u"}, {0x174U, 0x175U, "w"}, {0x176U, 0x178U, "y"}, {0x179U, 0x17EU, "z"}, {0x17FU, 0x17FU, "s"}, {0x1E9EU, 0x1E9EU, "ss"},
    }};

    std::string result;
    result.reserve(text.size());
    const char* cursor = text.data();
    const char* end = text.data() + text.size();

    while (cursor < end) {
        unsigned int codepoint = 0;
        cursor += ImTextCharFromUtf8(&codepoint, cursor, end);
        const unsigned int small = lowerCodepoint(codepoint);
        // clang-format off
        const auto range = std::ranges::find_if(foldRanges, [small](const FoldRange& candidate) { return small >= candidate.first && small <= candidate.last; });
        // clang-format on

        if (range == foldRanges.end()) {
            append(result, small);
            continue;
        }

        result.append(range->base);
    }

    return result;
}

std::string TextCase::upper(std::string_view text) {
    return map(text, &TextCase::upperCodepoint);
}

std::string TextCase::lower(std::string_view text) {
    return map(text, &TextCase::lowerCodepoint);
}

// A reader compares base letters first, then accents and then case, so accented words sit among the plain ones instead of after the whole alphabet.
bool TextCase::alphabeticalLess(std::string_view left, std::string_view right) {
    const std::string baseLeft = fold(left);
    const std::string baseRight = fold(right);

    if (baseLeft != baseRight) {
        return baseLeft < baseRight;
    }

    const std::string foldedLeft = lower(left);
    const std::string foldedRight = lower(right);

    if (foldedLeft != foldedRight) {
        return foldedLeft < foldedRight;
    }

    return left < right;
}

} // namespace workpane::ui
