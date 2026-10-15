#pragma once

namespace workpane::platform {

// Settles how the signals of the system reach the product, which answers a write to a peer that went away as a failed write instead of ending.
class ProcessSignals final {
  public:
    static void ignoreBrokenPipes();
};

} // namespace workpane::platform
