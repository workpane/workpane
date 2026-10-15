#pragma once

#include "ui/theme/FontRole.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace workpane::ui {

// Remembers the lines a paragraph broke into during one frame, by its text, face, size and width, so measuring a paragraph and drawing it break it once.
class ParagraphCache final {
  public:
    struct Line final {
        std::size_t offset{0};
        std::size_t length{0};
    };

    [[nodiscard]] const std::vector<Line>* find(std::string_view text, FontRole role, float width) const;
    void keep(std::string_view text, FontRole role, float width, std::vector<Line> lines);
    void clear();

  private:
    static constexpr std::size_t largestEntries{2048};

    struct Entry final {
        std::string text;
        FontRole role;
        float width{0.0F};
        std::vector<Line> lines;
    };

    [[nodiscard]] static std::size_t key(std::string_view text, FontRole role, float width);

    std::unordered_map<std::size_t, std::vector<Entry>> m_entries;
    std::size_t m_count{0};
};

} // namespace workpane::ui
