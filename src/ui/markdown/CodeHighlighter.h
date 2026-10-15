#pragma once

#include "ui/markdown/Run.h"

#include <TextEditor.h>

#include <optional>
#include <string_view>
#include <vector>

namespace workpane::ui::markdown {

class CodeHighlighter final : private TextEditor {
  public:
    [[nodiscard]] static std::vector<std::vector<Run>> lines(std::string_view code, std::string_view language);

  private:
    [[nodiscard]] static std::optional<ThemeColor> tone(TextEditor::Color color);
};

} // namespace workpane::ui::markdown
