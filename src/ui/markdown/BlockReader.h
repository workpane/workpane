#pragma once

#include "ui/markdown/Block.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui::markdown {

// Splits a document into its blocks line by line and leaves the text of every block to the inline reader.
class BlockReader final {
  public:
    BlockReader(std::string_view source, bool breaks);
    [[nodiscard]] std::vector<Block> read();

  private:
    static constexpr std::size_t codeIndent{4};
    static constexpr std::size_t fenceLength{3};
    static constexpr std::size_t longestOrdinal{9};
    static constexpr std::size_t deepestHeading{6};
    static constexpr std::size_t levelIndent{2};
    static constexpr int deepestLevel{3};

    [[nodiscard]] bool readFence(std::string_view content, std::size_t indent);
    [[nodiscard]] bool readHeading(std::string_view content);
    [[nodiscard]] bool readRule(std::string_view content);
    [[nodiscard]] bool readItem(std::string_view content, std::size_t indent);
    [[nodiscard]] bool readQuote(std::string_view content);
    void open(Block block, std::string_view text);
    void append(std::string_view text);
    void close();

    std::vector<std::string_view> m_lines;
    std::size_t m_index{0};
    std::vector<Block> m_blocks;
    std::string m_text;
    bool m_open{false};
    bool m_break{false};
    bool m_breaks{false};
};

} // namespace workpane::ui::markdown
