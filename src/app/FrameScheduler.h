#pragma once

#include "app/FrameDemand.h"

namespace workpane::app {

// Decides how long the frame loop sleeps, so an idle window costs nothing while anything that moves still draws at the display rate.
class FrameScheduler final {
  public:
    static constexpr double hiddenTickSeconds{0.05};
    static constexpr double dialogTickSeconds{0.1};

    [[nodiscard]] double waitSeconds(double now, double workSeconds, bool dialogPending, bool queuedWork, bool shown) const;
    [[nodiscard]] bool shouldDraw(const FrameDemand& demand, double now);
    void drew(const FrameDemand& next);

  private:
    static constexpr int settleFrames{2};

    int m_pending{settleFrames};
    bool m_animating{true};
    double m_deadline{0.0};
};

} // namespace workpane::app
