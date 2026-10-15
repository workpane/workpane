#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// A path stays editable while its selection button asks the SDK for a local or remote file or folder.
class PathField final : public Component {
  public:
    explicit PathField(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments) override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    struct Filter final {
        TextValue name;
        std::vector<std::string> patterns;
    };

    static constexpr float buttonSpacing{4.0F};
    static constexpr std::size_t maximumFilters{32};
    static constexpr std::size_t maximumPatterns{32};

    std::string m_value;
    std::string m_mode{"file"};
    TextValue m_placeholder;
    TextValue m_pickerTitle;
    std::vector<Filter> m_filters;
    bool m_readOnly{false};
    bool m_clearButton{false};
    bool m_monospace{false};
    bool m_browseEnabled{true};
    bool m_picking{false};
    bool m_reload{false};
    bool m_focus{false};
    bool m_selectAll{false};
};

} // namespace workpane::ui
