#pragma once

#include <QString>

namespace workpane::plugins::terminalplugin {

inline constexpr auto sessionDragMimeType = "application/x-workpane-terminal-session-id";

enum class SessionDropTarget { None, Slot, Shelf };

struct SessionDropDestination final {
    SessionDropTarget target{SessionDropTarget::None};
    int slotIndex{-1};
};

class SessionDragSource {
  public:
    virtual ~SessionDragSource() = default;

    [[nodiscard]] virtual QString draggedSessionId() const = 0;
    virtual void setDropDestination(SessionDropDestination destination) = 0;
};

} // namespace workpane::plugins::terminalplugin
