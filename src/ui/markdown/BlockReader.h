#pragma once

#include "ui/markdown/Block.h"
#include "ui/markdown/Span.h"

#include <md4c.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui::markdown {

class BlockReader final {
  public:
    BlockReader(std::string_view source, bool breaks);
    [[nodiscard]] std::vector<Block> read();

  private:
    struct List final {
        bool ordered{false};
        int ordinal{0};
        bool pending{false};
        std::optional<bool> checked;
    };

    static int enterBlock(MD_BLOCKTYPE type, void* detail, void* context);
    static int leaveBlock(MD_BLOCKTYPE type, void* detail, void* context);
    static int enterSpan(MD_SPANTYPE type, void* detail, void* context);
    static int leaveSpan(MD_SPANTYPE type, void* detail, void* context);
    static int text(MD_TEXTTYPE type, const MD_CHAR* value, MD_SIZE size, void* context);
    [[nodiscard]] static std::string attribute(const MD_ATTRIBUTE& value);
    [[nodiscard]] static std::string entity(std::string_view value);
    [[nodiscard]] static std::string encoded(unsigned int point);
    void open(BlockKind kind);
    void append(std::string value);

    std::string_view m_source;
    bool m_breaks{false};
    bool m_code{false};
    int m_quotes{0};
    int m_current{-1};
    int m_table{-1};
    int m_cell{-1};
    int m_imageDepth{0};
    Span m_image;
    std::vector<Block> m_blocks;
    std::vector<List> m_lists;
    std::vector<Span> m_styles{Span{}};
};

} // namespace workpane::ui::markdown
