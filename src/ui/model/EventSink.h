#pragma once

#include "ui/model/NodeId.h"
#include "ui/model/UiEvent.h"

#include <vector>

namespace workpane::ui {

// Events are collected while a frame is drawn and handed to Lua once it ends, so a handler never runs in the middle of a layout.
class EventSink final {
  public:
    void emit(UiEvent event);
    [[nodiscard]] std::vector<UiEvent> take();
    [[nodiscard]] bool empty() const;

  private:
    std::vector<UiEvent> m_events;
};

} // namespace workpane::ui
