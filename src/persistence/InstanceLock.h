#pragma once

#include "Result.h"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace workpane::persistence {

// Only one running product writes a data directory, and the lock is released by the platform even when the process ends abruptly.
// The lock file says the product is running until it stops cleanly, so the next start knows when the previous one did not.
class InstanceLock final {
  public:
    [[nodiscard]] static Result<InstanceLock> acquire(const std::filesystem::path& dataDirectory);

    InstanceLock(InstanceLock&& other) noexcept;
    InstanceLock& operator=(InstanceLock&& other) noexcept;
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;
    ~InstanceLock();

    [[nodiscard]] bool recovered() const;

  private:
    static constexpr const char* lockName{"workpane.lock"};
    static constexpr std::string_view runningMark{"running"};

    InstanceLock(std::intptr_t handle, bool recovered);
    void release();

    std::intptr_t m_handle{-1};
    bool m_recovered{false};
};

} // namespace workpane::persistence
