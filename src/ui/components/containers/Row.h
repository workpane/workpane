#pragma once

#include "ui/components/containers/LinearContainer.h"
#include "ui/model/NodeId.h"

#include <string_view>

namespace workpane::ui {

class Row final : public LinearContainer {
  public:
    explicit Row(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
};

} // namespace workpane::ui
