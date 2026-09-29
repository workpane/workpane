#include "support/MainThreadPump.h"

#include <chrono>
#include <thread>

namespace workpane::tests {

void MainThreadPump::until(execution::MainThreadQueue& queue, const bool& finished) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (!finished && std::chrono::steady_clock::now() < deadline) {
        queue.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace workpane::tests
