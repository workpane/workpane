#include "ui/ParagraphCache.h"

#include <bit>
#include <cstdint>
#include <functional>
#include <utility>

namespace workpane::ui {

const std::vector<ParagraphCache::Line>* ParagraphCache::find(std::string_view text, FontRole role, float width) const {
    const auto found = m_entries.find(key(text, role, width));

    if (found == m_entries.end()) {
        return nullptr;
    }

    for (const Entry& entry : found->second) {
        if (entry.text == text && entry.role.face == role.face && entry.role.size == role.size && entry.width == width) {
            return &entry.lines;
        }
    }

    return nullptr;
}

// A frame that breaks more paragraphs than the bound keeps the first ones, so a long table never grows the cache without end.
void ParagraphCache::keep(std::string_view text, FontRole role, float width, std::vector<Line> lines) {
    if (m_count >= largestEntries) {
        return;
    }

    m_entries[key(text, role, width)].push_back({std::string(text), role, width, std::move(lines)});
    ++m_count;
}

void ParagraphCache::clear() {
    m_entries.clear();
    m_count = 0;
}

std::size_t ParagraphCache::key(std::string_view text, FontRole role, float width) {
    const std::size_t measure = (static_cast<std::size_t>(std::bit_cast<std::uint32_t>(width)) << 1U) ^ static_cast<std::size_t>(std::bit_cast<std::uint32_t>(role.size)) ^ static_cast<std::size_t>(role.face);
    return std::hash<std::string_view>{}(text) ^ (measure * 0x9E3779B97F4A7C15ULL);
}

} // namespace workpane::ui
