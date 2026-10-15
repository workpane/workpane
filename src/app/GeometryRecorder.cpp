#include "app/GeometryRecorder.h"

#include <algorithm>
#include <limits>

namespace workpane::app {

void GeometryRecorder::start(const platform::WindowGeometry& written) {
    m_written = written;
    m_seen = written;
    m_pending = false;
}

// Every change starts the wait again, and coming back to the place already written leaves nothing to write.
void GeometryRecorder::observe(const platform::WindowGeometry& geometry, double now) {
    if (geometry == m_seen) {
        return;
    }

    m_seen = geometry;
    m_changed = now;
    m_pending = !(geometry == m_written);
}

std::optional<platform::WindowGeometry> GeometryRecorder::due(double now) {
    if (!m_pending || now - m_changed < settleSeconds) {
        return std::nullopt;
    }

    m_pending = false;
    m_written = m_seen;

    return m_written;
}

// The place last observed and not yet written, whether or not it has settled, which is what a closing window still has to write.
std::optional<platform::WindowGeometry> GeometryRecorder::unwritten() {
    if (!m_pending) {
        return std::nullopt;
    }

    m_pending = false;
    m_written = m_seen;

    return m_written;
}

double GeometryRecorder::waitSeconds(double now) const {
    return m_pending ? std::max(0.0, settleSeconds - (now - m_changed)) : std::numeric_limits<double>::infinity();
}

} // namespace workpane::app
