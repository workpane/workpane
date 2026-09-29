#pragma once

#include "json/ObjectReader.h"
#include "ui/components/containers/LinearContainer.h"
#include "ui/model/NodeId.h"

#include <string_view>

namespace workpane::ui {

// The buttons of a settings form, which take the inset of the form they sit in.
class SettingsActions final : public LinearContainer {
  public:
    explicit SettingsActions(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;

  private:
    static constexpr float actionSpacing{6.0F};
};

} // namespace workpane::ui
