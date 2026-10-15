#pragma once

#include "platform/WindowGeometry.h"

#include <optional>

namespace workpane::app {

// Remembers where the window stands and says when a new place has held long enough to be written, so a drag or a resize is written once it ends rather than at every step.
class GeometryRecorder final {
  public:
    void start(const platform::WindowGeometry& written);
    void observe(const platform::WindowGeometry& geometry, double now);
    [[nodiscard]] std::optional<platform::WindowGeometry> due(double now);
    [[nodiscard]] std::optional<platform::WindowGeometry> unwritten();
    [[nodiscard]] double waitSeconds(double now) const;

  private:
    static constexpr double settleSeconds{0.5};

    platform::WindowGeometry m_written;
    platform::WindowGeometry m_seen;
    double m_changed{0.0};
    bool m_pending{false};
};

} // namespace workpane::app
