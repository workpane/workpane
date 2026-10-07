#pragma once

#include <string_view>

namespace workpane::process {

// A program running beside the product, written to and read on a thread of its own.
// Destroying it waits for the program to end, so the owner destroys it away from the interface thread.
class Process {
  public:
    virtual ~Process() = default;

    [[nodiscard]] virtual bool write(std::string_view bytes) = 0;
    virtual void stop() = 0;
};

} // namespace workpane::process
