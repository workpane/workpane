#pragma once

#include "json/ObjectReader.h"
#include "ui/components/containers/LinearContainer.h"
#include "ui/model/Insets.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <string_view>

namespace workpane::ui {

// The body of a settings section, built from rows and actions so every owner produces the same shape with the same inset.
class SettingsForm final : public LinearContainer {
  public:
    explicit SettingsForm(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Insets contentInsets(RenderContext& context) const override;

  private:
    static constexpr float formRowSpacing{10.0F};
};

} // namespace workpane::ui
