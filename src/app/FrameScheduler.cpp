#include "app/FrameScheduler.h"

#include <algorithm>

namespace workpane::app {

// Zero means draw at once and infinity means sleep until the window system has an event.
// A window that is not shown, such as a minimized one, draws nothing, so only the work that runs without drawing wakes it.
double FrameScheduler::waitSeconds(double now, double workSeconds, bool dialogPending, bool queuedWork, bool shown) const {
    if (shown && (m_pending > 0 || m_animating || queuedWork)) {
        return 0.0;
    }

    double wait = workSeconds;

    if (dialogPending) {
        wait = std::min(wait, dialogTickSeconds);
    }

    return shown ? std::min(wait, std::max(0.0, m_deadline - now)) : wait;
}

// A frame is drawn for input, for a change of state, for an animation and for a deadline that has passed, and a few more follow input and changes so hovering and new content settle.
bool FrameScheduler::shouldDraw(const FrameDemand& demand, double now) {
    // ImGui learns the size of new content only in the frame that draws it, so a change is followed by frames that place its scroll bars.
    if (demand.input || demand.changed) {
        m_pending = settleFrames;
    }

    return m_pending > 0 || m_animating || demand.changed || demand.animating || now >= m_deadline;
}

// Input that waits in the queue of ImGui keeps the frames coming until it is drawn, since no event of the window will arrive for it.
void FrameScheduler::drew(const FrameDemand& next) {
    m_pending = std::max(next.input ? 1 : 0, m_pending - 1);
    m_animating = next.animating;
    m_deadline = next.deadline;
}

} // namespace workpane::app
