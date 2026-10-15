#include "ui/components/containers/Row.h"

namespace workpane::ui {

Row::Row(NodeId id) : LinearContainer(id, Axis::Horizontal) {}

std::string_view Row::kind() const {
    return "row";
}

} // namespace workpane::ui
