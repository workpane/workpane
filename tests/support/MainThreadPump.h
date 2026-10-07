#pragma once

#include "execution/MainThreadQueue.h"

namespace workpane::tests {

// Runs the work queued for the main thread until a flag says the answer a test waits for arrived.
class MainThreadPump final {
  public:
    static void until(execution::MainThreadQueue& queue, const bool& finished);
};

} // namespace workpane::tests
