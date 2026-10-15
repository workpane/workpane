#include "ui/components/containers/Column.h"

namespace workpane::ui {

Column::Column(NodeId id) : LinearContainer(id, Axis::Vertical) {}

std::string_view Column::kind() const {
    return "column";
}

} // namespace workpane::ui
