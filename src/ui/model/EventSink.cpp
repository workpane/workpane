#include "ui/model/EventSink.h"

#include <utility>

namespace workpane::ui {

void EventSink::emit(UiEvent event) {
    m_events.push_back(std::move(event));
}

std::vector<UiEvent> EventSink::take() {
    std::vector<UiEvent> events;
    events.swap(m_events);

    return events;
}

bool EventSink::empty() const {
    return m_events.empty();
}

} // namespace workpane::ui
