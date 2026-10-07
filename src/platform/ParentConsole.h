#pragma once

namespace workpane::platform {

// Hands what the product prints for its command line to the terminal that started it.
class ParentConsole final {
  public:
    static void attach();
};

} // namespace workpane::platform
